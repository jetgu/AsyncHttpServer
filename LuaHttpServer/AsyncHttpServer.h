#pragma once


#include <string>
#include <map>
#include <sstream>
#include <stdexcept>
#include <functional>
#include <memory>
#include <thread>
#include <vector>
#include <atomic>
#include <cctype>
#include <chrono>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include "ServerLogger.h"

// ---------------------------------------------------------------------------
// AsyncHttpRequest ?parsed inbound request
// ---------------------------------------------------------------------------
struct AsyncHttpRequest
{
    std::string method;
    std::string path;
    std::string query;
    std::string version;
    std::map<std::string, std::string> headers;
    std::string body;
    std::string remote_address;
};

// ---------------------------------------------------------------------------
// AsyncHttpResponse ?built by the handler and sent back to the client
// ---------------------------------------------------------------------------
struct AsyncHttpResponse
{
    int                                status_code    = 200;
    std::string                        status_message = "OK";
    std::map<std::string, std::string> headers;
    std::string                        body;

    AsyncHttpResponse& set_status(int code, const std::string& msg = "")
    {
        status_code    = code;
        status_message = msg.empty() ? default_status_message(code) : msg;
        return *this;
    }

    AsyncHttpResponse& set_body(const std::string& b,
                                 const std::string& content_type = "text/plain")
    {
        body                    = b;
        headers["Content-Type"] = content_type;
        return *this;
    }

    AsyncHttpResponse& set_header(const std::string& k, const std::string& v)
    {
        headers[k] = v;
        return *this;
    }

    std::string serialize(bool keep_alive) const
    {
        std::ostringstream out;
        out << "HTTP/1.1 " << status_code << " " << status_message << "\r\n";

        // Copy headers and add Content-Length / Connection
        auto hdrs = headers;
        hdrs["Content-Length"] = std::to_string(body.size());
        hdrs["Connection"]     = keep_alive ? "keep-alive" : "close";

        for (const auto& kv : hdrs)
            out << kv.first << ": " << kv.second << "\r\n";

        out << "\r\n" << body;
        return out.str();
    }

private:
    static std::string default_status_message(int code)
    {
        switch (code)
        {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 500: return "Internal Server Error";
        default:  return "Unknown";
        }
    }
};

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
class AsyncHttpServer;
class AsyncHttpConnection;

// ---------------------------------------------------------------------------
// AsyncHttpConnection ?one connection, fully async
// ---------------------------------------------------------------------------
class AsyncHttpConnection
    : public std::enable_shared_from_this<AsyncHttpConnection>
{
public:
    using tcp      = boost::asio::ip::tcp;
    using Handler  = std::function<void(const AsyncHttpRequest&, AsyncHttpResponse&)>;

    static std::shared_ptr<AsyncHttpConnection>
    create(tcp::socket socket, Handler handler)
    {
        return std::shared_ptr<AsyncHttpConnection>(
            new AsyncHttpConnection(std::move(socket), std::move(handler)));
    }

    void start()
    {
        remote_address_ = socket_.remote_endpoint().address().to_string();
        ServerLogger::instance().log_connection(remote_address_, true);
        do_read_headers();
    }

private:
    explicit AsyncHttpConnection(tcp::socket socket, Handler handler)
        : socket_(std::move(socket))
        , handler_(std::move(handler))
    {}

    // --- Read headers until \r\n\r\n ---
    void do_read_headers()
    {
        auto self = shared_from_this();
        boost::asio::async_read_until(
            socket_, read_buf_, "\r\n\r\n",
            [this, self](boost::system::error_code ec, std::size_t bytes_transferred)
            {
                if (ec)
                    return; // connection closed or error

                // Start timing after headers are received
                request_start_time_ = std::chrono::steady_clock::now();

                // Extract header block
                std::string header_data(
                    boost::asio::buffers_begin(read_buf_.data()),
                    boost::asio::buffers_begin(read_buf_.data()) + bytes_transferred);
                read_buf_.consume(bytes_transferred);

                if (!parse_headers(header_data))
                    return;

                // Check for body
                auto it = request_.headers.find("content-length");
                if (it != request_.headers.end())
                {
                    std::size_t content_length = 0;
                    try { content_length = std::stoul(it->second); }
                    catch (...) { return; }

                    if (content_length > 0)
                    {
                        do_read_body(content_length);
                        return;
                    }
                }

                // No body ?dispatch immediately
                dispatch_and_respond();
            });
    }

    // --- Read exactly content_length bytes for body ---
    void do_read_body(std::size_t content_length)
    {
        // Some body bytes may already be in read_buf_ from async_read_until
        std::size_t already = read_buf_.size();
        if (already >= content_length)
        {
            // All body bytes already buffered
            request_.body.assign(
                boost::asio::buffers_begin(read_buf_.data()),
                boost::asio::buffers_begin(read_buf_.data()) + content_length);
            read_buf_.consume(content_length);
            dispatch_and_respond();
            return;
        }

        // Copy what we have
        request_.body.assign(
            boost::asio::buffers_begin(read_buf_.data()),
            boost::asio::buffers_end(read_buf_.data()));
        read_buf_.consume(already);

        std::size_t remaining = content_length - already;

        auto self = shared_from_this();
        boost::asio::async_read(
            socket_, read_buf_,
            boost::asio::transfer_exactly(remaining),
            [this, self, remaining](boost::system::error_code ec, std::size_t /*bytes*/)
            {
                if (ec)
                    return;

                // Append remaining body
                request_.body.append(
                    boost::asio::buffers_begin(read_buf_.data()),
                    boost::asio::buffers_begin(read_buf_.data()) + remaining);
                read_buf_.consume(remaining);

                dispatch_and_respond();
            });
    }

    // --- Parse the header block into request_ ---
    bool parse_headers(const std::string& header_data)
    {
        request_ = AsyncHttpRequest();
        request_.remote_address = remote_address_;

        std::istringstream ss(header_data);
        std::string request_line;
        std::getline(ss, request_line);
        if (!request_line.empty() && request_line.back() == '\r')
            request_line.pop_back();

        {
            std::istringstream rls(request_line);
            std::string url;
            rls >> request_.method >> url >> request_.version;

            std::size_t q = url.find('?');
            if (q != std::string::npos)
            {
                request_.path  = url.substr(0, q);
                request_.query = url.substr(q + 1);
            }
            else
            {
                request_.path = url;
            }
        }

        std::string line;
        while (std::getline(ss, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty())
                break;
            std::size_t colon = line.find(':');
            if (colon != std::string::npos)
            {
                std::string k = line.substr(0, colon);
                std::string v = line.substr(colon + 1);
                if (!v.empty() && v.front() == ' ')
                    v.erase(0, 1);
                for (char& c : k)
                    c = (char)std::tolower((unsigned char)c);
                request_.headers[k] = v;
            }
        }

        return !request_.method.empty();
    }

    // --- Call user handler and write response ---
    void dispatch_and_respond()
    {
        // Log the incoming request
        ServerLogger::instance().log_request(remote_address_, request_.method, 
                                              request_.path, request_.query);

        AsyncHttpResponse response;

        try
        {
            if (handler_)
                handler_(request_, response);
            else
                response.set_status(501).set_body("No handler registered");
        }
        catch (const std::exception& e)
        {
            ServerLogger::instance().error("Handler exception: " + std::string(e.what()));
            response.set_status(500).set_body(e.what());
        }
        catch (...)
        {
            ServerLogger::instance().error("Unknown handler exception");
            response.set_status(500).set_body("Internal Server Error");
        }

        // Calculate request duration and log response
        auto duration = std::chrono::steady_clock::now() - request_start_time_;
        long duration_ms = static_cast<long>(
            std::chrono::duration_cast<std::chrono::milliseconds>(duration).count());
        ServerLogger::instance().log_response(remote_address_, response.status_code, 
                                               response.body.size(), duration_ms);

        bool keep_alive = should_keep_alive();
        response_data_  = response.serialize(keep_alive);

        do_write(keep_alive);
    }

    // --- Async write response ---
    void do_write(bool keep_alive)
    {
        auto self = shared_from_this();
        boost::asio::async_write(
            socket_,
            boost::asio::buffer(response_data_),
            [this, self, keep_alive](boost::system::error_code ec, std::size_t /*bytes*/)
            {
                if (ec)
                {
                    ServerLogger::instance().log_connection(remote_address_, false);
                    return;
                }

                if (keep_alive)
                {
                    // Reset for next request
                    request_       = AsyncHttpRequest();
                    response_data_.clear();
                    do_read_headers();
                }
                else
                {
                    // Graceful close
                    boost::system::error_code shutdown_ec;
                    socket_.shutdown(tcp::socket::shutdown_both, shutdown_ec);
                }
            });
    }

    bool should_keep_alive() const
    {
        auto it = request_.headers.find("connection");
        if (it != request_.headers.end())
        {
            std::string v = it->second;
            for (char& c : v)
                c = (char)std::tolower((unsigned char)c);
            if (v.find("close") != std::string::npos)
                return false;
            if (v.find("keep-alive") != std::string::npos)
                return true;
        }
        return request_.version == "HTTP/1.1";
    }

    tcp::socket              socket_;
    Handler                  handler_;
    boost::asio::streambuf   read_buf_;
    AsyncHttpRequest         request_;
    std::string              response_data_;
    std::string              remote_address_;
    std::chrono::steady_clock::time_point request_start_time_;
};

// ---------------------------------------------------------------------------
// AsyncHttpsConnection ?TLS version
// ---------------------------------------------------------------------------
class AsyncHttpsConnection
    : public std::enable_shared_from_this<AsyncHttpsConnection>
{
public:
    using tcp       = boost::asio::ip::tcp;
    using SslStream = boost::asio::ssl::stream<tcp::socket>;
    using Handler   = std::function<void(const AsyncHttpRequest&, AsyncHttpResponse&)>;

    static std::shared_ptr<AsyncHttpsConnection>
    create(tcp::socket socket,
           boost::asio::ssl::context& ssl_ctx,
           Handler handler)
    {
        return std::shared_ptr<AsyncHttpsConnection>(
            new AsyncHttpsConnection(std::move(socket), ssl_ctx, std::move(handler)));
    }

    void start()
    {
        remote_address_ = stream_.next_layer().remote_endpoint().address().to_string();
        ServerLogger::instance().log_connection(remote_address_ + " (SSL)", true);
        do_handshake();
    }

private:
    AsyncHttpsConnection(tcp::socket socket,
                          boost::asio::ssl::context& ssl_ctx,
                          Handler handler)
        : stream_(std::move(socket), ssl_ctx)
        , handler_(std::move(handler))
    {}

    void do_handshake()
    {
        auto self = shared_from_this();
        stream_.async_handshake(
            boost::asio::ssl::stream_base::server,
            [this, self](boost::system::error_code ec)
            {
                if (!ec)
                {
                    do_read_headers();
                }
                else
                {
                    ServerLogger::instance().error("SSL handshake failed: " + ec.message());
                }
            });
    }

    void do_read_headers()
    {
        auto self = shared_from_this();
        boost::asio::async_read_until(
            stream_, read_buf_, "\r\n\r\n",
            [this, self](boost::system::error_code ec, std::size_t bytes_transferred)
            {
                if (ec)
                    return;

                // Start timing after headers are received
                request_start_time_ = std::chrono::steady_clock::now();

                std::string header_data(
                    boost::asio::buffers_begin(read_buf_.data()),
                    boost::asio::buffers_begin(read_buf_.data()) + bytes_transferred);
                read_buf_.consume(bytes_transferred);

                if (!parse_headers(header_data))
                    return;

                auto it = request_.headers.find("content-length");
                if (it != request_.headers.end())
                {
                    std::size_t content_length = 0;
                    try { content_length = std::stoul(it->second); }
                    catch (...) { return; }

                    if (content_length > 0)
                    {
                        do_read_body(content_length);
                        return;
                    }
                }

                dispatch_and_respond();
            });
    }

    void do_read_body(std::size_t content_length)
    {
        std::size_t already = read_buf_.size();
        if (already >= content_length)
        {
            request_.body.assign(
                boost::asio::buffers_begin(read_buf_.data()),
                boost::asio::buffers_begin(read_buf_.data()) + content_length);
            read_buf_.consume(content_length);
            dispatch_and_respond();
            return;
        }

        request_.body.assign(
            boost::asio::buffers_begin(read_buf_.data()),
            boost::asio::buffers_end(read_buf_.data()));
        read_buf_.consume(already);

        std::size_t remaining = content_length - already;

        auto self = shared_from_this();
        boost::asio::async_read(
            stream_, read_buf_,
            boost::asio::transfer_exactly(remaining),
            [this, self, remaining](boost::system::error_code ec, std::size_t)
            {
                if (ec)
                    return;

                request_.body.append(
                    boost::asio::buffers_begin(read_buf_.data()),
                    boost::asio::buffers_begin(read_buf_.data()) + remaining);
                read_buf_.consume(remaining);

                dispatch_and_respond();
            });
    }

    bool parse_headers(const std::string& header_data)
    {
        request_ = AsyncHttpRequest();
        request_.remote_address = remote_address_;

        std::istringstream ss(header_data);
        std::string request_line;
        std::getline(ss, request_line);
        if (!request_line.empty() && request_line.back() == '\r')
            request_line.pop_back();

        {
            std::istringstream rls(request_line);
            std::string url;
            rls >> request_.method >> url >> request_.version;

            std::size_t q = url.find('?');
            if (q != std::string::npos)
            {
                request_.path  = url.substr(0, q);
                request_.query = url.substr(q + 1);
            }
            else
            {
                request_.path = url;
            }
        }

        std::string line;
        while (std::getline(ss, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty())
                break;
            std::size_t colon = line.find(':');
            if (colon != std::string::npos)
            {
                std::string k = line.substr(0, colon);
                std::string v = line.substr(colon + 1);
                if (!v.empty() && v.front() == ' ')
                    v.erase(0, 1);
                for (char& c : k)
                    c = (char)std::tolower((unsigned char)c);
                request_.headers[k] = v;
            }
        }

        return !request_.method.empty();
    }

    void dispatch_and_respond()
    {
        // Log the incoming request
        ServerLogger::instance().log_request(remote_address_, request_.method, 
                                              request_.path, request_.query);

        AsyncHttpResponse response;

        try
        {
            if (handler_)
                handler_(request_, response);
            else
                response.set_status(501).set_body("No handler registered");
        }
        catch (const std::exception& e)
        {
            ServerLogger::instance().error("Handler exception: " + std::string(e.what()));
            response.set_status(500).set_body(e.what());
        }
        catch (...)
        {
            ServerLogger::instance().error("Unknown handler exception");
            response.set_status(500).set_body("Internal Server Error");
        }

        // Calculate request duration and log response
        auto duration = std::chrono::steady_clock::now() - request_start_time_;
        long duration_ms = static_cast<long>(
            std::chrono::duration_cast<std::chrono::milliseconds>(duration).count());
        ServerLogger::instance().log_response(remote_address_, response.status_code, 
                                               response.body.size(), duration_ms);

        bool keep_alive = should_keep_alive();
        response_data_  = response.serialize(keep_alive);

        do_write(keep_alive);
    }

    void do_write(bool keep_alive)
    {
        auto self = shared_from_this();
        boost::asio::async_write(
            stream_,
            boost::asio::buffer(response_data_),
            [this, self, keep_alive](boost::system::error_code ec, std::size_t)
            {
                if (ec)
                    return;

                if (keep_alive)
                {
                    request_ = AsyncHttpRequest();
                    response_data_.clear();
                    do_read_headers();
                }
                else
                {
                    boost::system::error_code shutdown_ec;
                    stream_.shutdown(shutdown_ec);
                }
            });
    }

    bool should_keep_alive() const
    {
        auto it = request_.headers.find("connection");
        if (it != request_.headers.end())
        {
            std::string v = it->second;
            for (char& c : v)
                c = (char)std::tolower((unsigned char)c);
            if (v.find("close") != std::string::npos)
                return false;
            if (v.find("keep-alive") != std::string::npos)
                return true;
        }
        return request_.version == "HTTP/1.1";
    }

    SslStream                stream_;
    Handler                  handler_;
    boost::asio::streambuf   read_buf_;
    AsyncHttpRequest         request_;
    std::string              response_data_;
    std::string              remote_address_;
    std::chrono::steady_clock::time_point request_start_time_;
};

// ---------------------------------------------------------------------------
// AsyncHttpServer
//
// High-performance async HTTP/HTTPS server using Boost.Asio async I/O.
// A small pool of threads runs io_context::run(), handling thousands of
// concurrent connections efficiently without blocking.
//
// Usage (plain HTTP):
//   AsyncHttpServer srv;
//   srv.on_request = [](const AsyncHttpRequest& req, AsyncHttpResponse& resp) {
//       resp.set_body("Hello, Async World!");
//   };
//   srv.start(8080);        // uses hardware_concurrency threads
//   srv.start(8080, 4);     // explicit 4 threads
//   srv.stop();
//
// Usage (HTTPS):
//   srv.start(8443, "server.crt", "server.key");
//   srv.start(8443, "server.crt", "server.key", 4);
// ---------------------------------------------------------------------------
class AsyncHttpServer
{
public:
    using Handler = std::function<void(const AsyncHttpRequest&, AsyncHttpResponse&)>;

    Handler on_request;

    AsyncHttpServer()  = default;
    ~AsyncHttpServer() { stop(); }

    AsyncHttpServer(const AsyncHttpServer&)            = delete;
    AsyncHttpServer& operator=(const AsyncHttpServer&) = delete;

    // -----------------------------------------------------------------------
    // start ?plain HTTP
    // -----------------------------------------------------------------------
    void start(uint16_t port,
               std::size_t num_threads = 0)
    {
        start_internal(port, "", "", num_threads);
    }

    // -----------------------------------------------------------------------
    // start ?HTTPS
    // -----------------------------------------------------------------------
    void start(uint16_t port,
               const std::string& cert_file,
               const std::string& key_file,
               std::size_t num_threads = 0)
    {
        start_internal(port, cert_file, key_file, num_threads);
    }

    // -----------------------------------------------------------------------
    // stop ?graceful shutdown
    // -----------------------------------------------------------------------
    void stop()
    {
        if (!running_.exchange(false))
            return;

        ServerLogger::instance().log_server_stop();

        io_ctx_->stop();

        for (auto& t : threads_)
        {
            if (t.joinable())
                t.join();
        }
        threads_.clear();

        acceptor_.reset();
        ssl_ctx_.reset();
        io_ctx_.reset();
    }

    // -----------------------------------------------------------------------
    // port ?returns the bound port
    // -----------------------------------------------------------------------
    uint16_t port() const
    {
        if (!acceptor_)
            return 0;
        return acceptor_->local_endpoint().port();
    }

private:
    using tcp = boost::asio::ip::tcp;

    std::unique_ptr<boost::asio::io_context>   io_ctx_;
    std::unique_ptr<boost::asio::ssl::context> ssl_ctx_;
    std::unique_ptr<tcp::acceptor>             acceptor_;
    std::vector<std::thread>                   threads_;
    std::atomic<bool>                          running_{ false };
    bool                                       is_ssl_{ false };

    void start_internal(uint16_t port,
                        const std::string& cert_file,
                        const std::string& key_file,
                        std::size_t num_threads)
    {
        if (running_)
            throw std::runtime_error("AsyncHttpServer: already running");

        if (num_threads == 0)
            num_threads = std::thread::hardware_concurrency();
        if (num_threads == 0)
            num_threads = 2;

        is_ssl_ = !cert_file.empty();


        io_ctx_ = std::unique_ptr<boost::asio::io_context>(
                      new boost::asio::io_context());

        if (is_ssl_)
        {
            ssl_ctx_ = std::unique_ptr<boost::asio::ssl::context>(
                           new boost::asio::ssl::context(
                               boost::asio::ssl::context::sslv23_server));

            ssl_ctx_->set_options(
                boost::asio::ssl::context::default_workarounds |
                boost::asio::ssl::context::no_sslv2            |
                boost::asio::ssl::context::single_dh_use);

            ssl_ctx_->use_certificate_chain_file(cert_file);
            ssl_ctx_->use_private_key_file(key_file,
                                           boost::asio::ssl::context::pem);
        }

        acceptor_ = std::unique_ptr<tcp::acceptor>(
                        new tcp::acceptor(*io_ctx_,
                            tcp::endpoint(tcp::v4(), port)));
        acceptor_->set_option(tcp::acceptor::reuse_address(true));

        running_ = true;

        // Log server start
        ServerLogger::instance().log_server_start(port, static_cast<int>(num_threads), is_ssl_);

        // Start async accept chain
        do_accept();

        // Launch worker threads running io_context::run()
        for (std::size_t i = 0; i < num_threads; ++i)
        {
            threads_.emplace_back([this]()
            {
                io_ctx_->run();
            });
        }
    }

    void do_accept()
    {
        acceptor_->async_accept(
            [this](boost::system::error_code ec, tcp::socket socket)
            {
                if (!ec && running_)
                {
                    if (is_ssl_)
                    {
                        auto conn = AsyncHttpsConnection::create(
                                        std::move(socket), *ssl_ctx_, on_request);
                        conn->start();
                    }
                    else
                    {
                        auto conn = AsyncHttpConnection::create(
                                        std::move(socket), on_request);
                        conn->start();
                    }
                }

                // Continue accepting if still running
                if (running_)
                    do_accept();
            });
    }
};
