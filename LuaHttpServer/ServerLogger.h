#pragma once
// ServerLogger.h
// Logging utility for AsyncHttpServer
// Supports console and file output with log levels and log rotation

#include <string>
#include <fstream>
#include <iostream>
#include <sstream>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <memory>
#include <cstdio>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define MKDIR(path) _mkdir(path)
#else
#include <unistd.h>
#define MKDIR(path) mkdir(path, 0755)
#endif

enum class LogLevel
{
    LOG_DEBUG = 0,
    LOG_INFO = 1,
    LOG_WARNING = 2,
    LOG_ERROR = 3,
    LOG_NONE = 4
};

class ServerLogger
{
public:
    static ServerLogger& instance()
    {
        static ServerLogger logger;
        return logger;
    }

    // Configure the logger with rotation support
    void configure(bool enabled, 
                   const std::string& log_path = "./logs",
                   const std::string& log_file = "server.log",
                   int max_size = 10 * 1024 * 1024,  // 10MB
                   int max_files = 5,
                   LogLevel level = LogLevel::LOG_INFO)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        enabled_ = enabled;
        min_level_ = level;
        log_path_ = log_path;
        log_filename_ = log_file;
        max_file_size_ = max_size;
        max_files_ = max_files;
        current_size_ = 0;
        
        if (file_.is_open())
        {
            file_.close();
        }
        
        if (enabled && !log_path.empty() && !log_file.empty())
        {
            // Create log directory if it doesn't exist
            create_directory(log_path);
            
            // Open log file
            std::string full_path = get_log_filepath();
            file_.open(full_path, std::ios::app);
            if (file_.is_open())
            {
                // Get current file size
                file_.seekp(0, std::ios::end);
                current_size_ = static_cast<size_t>(file_.tellp());
            }
            else
            {
                std::cerr << "[Logger] Warning: Could not open log file: " << full_path << "\n";
            }
        }
    }

    // Log methods
    void debug(const std::string& message)
    {
        log(LogLevel::LOG_DEBUG, message);
    }

    void info(const std::string& message)
    {
        log(LogLevel::LOG_INFO, message);
    }

    void warning(const std::string& message)
    {
        log(LogLevel::LOG_WARNING, message);
    }

    void error(const std::string& message)
    {
        log(LogLevel::LOG_ERROR, message);
    }

    // Log HTTP request
    void log_request(const std::string& remote_addr, 
                     const std::string& method, 
                     const std::string& path,
                     const std::string& query = "")
    {
        if (!enabled_ || min_level_ > LogLevel::LOG_INFO) return;

        std::ostringstream oss;
        oss << remote_addr << " - " << method << " " << path;
        if (!query.empty())
        {
            oss << "?" << query;
        }
        log(LogLevel::LOG_INFO, oss.str());
    }

    // Log HTTP response
    void log_response(const std::string& remote_addr, 
                      int status_code, 
                      size_t body_size,
                      long duration_ms = -1)
    {
        if (!enabled_ || min_level_ > LogLevel::LOG_INFO) return;

        std::ostringstream oss;
        oss << remote_addr << " - Response: " << status_code;
        oss << " (" << format_size(body_size) << ")";
        if (duration_ms >= 0)
        {
            oss << " [" << duration_ms << "ms]";
        }
        log(LogLevel::LOG_INFO, oss.str());
    }

    // Log server events
    void log_server_start(int port, int threads, bool ssl)
    {
        std::ostringstream oss;
        oss << "Server started on port " << port 
            << " with " << threads << " worker threads"
            << (ssl ? " (SSL enabled)" : "");
        log(LogLevel::LOG_INFO, oss.str());
    }

    void log_server_stop()
    {
        log(LogLevel::LOG_INFO, "Server stopped");
    }

    void log_connection(const std::string& remote_addr, bool connected)
    {
        if (!enabled_ || min_level_ > LogLevel::LOG_DEBUG) return;

        std::ostringstream oss;
        oss << remote_addr << " - " << (connected ? "Connected" : "Disconnected");
        log(LogLevel::LOG_DEBUG, oss.str());
    }

    // Check if logging is enabled
    bool is_enabled() const { return enabled_; }

    // Close log file
    void close()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (file_.is_open())
        {
            file_.close();
        }
    }

private:
    ServerLogger() = default;
    ~ServerLogger() { close(); }

    ServerLogger(const ServerLogger&) = delete;
    ServerLogger& operator=(const ServerLogger&) = delete;

    void log(LogLevel level, const std::string& message)
    {
        if (!enabled_ || level < min_level_) return;

        std::lock_guard<std::mutex> lock(mutex_);

        std::string timestamp = get_timestamp();
        std::string level_str = level_to_string(level);
        
        std::ostringstream oss;
        oss << "[" << timestamp << "] [" << level_str << "] " << message;
        std::string formatted = oss.str();

        // Output to console with colors
        print_colored(level, formatted);

        // Output to file
        if (file_.is_open())
        {
            file_ << formatted << "\n";
            file_.flush();
            
            // Update current size and check for rotation
            current_size_ += formatted.size() + 1;
            if (current_size_ >= static_cast<size_t>(max_file_size_))
            {
                rotate_logs();
            }
        }
    }

    // Rotate log files
    void rotate_logs()
    {
        if (!file_.is_open()) return;
        
        file_.close();
        
        std::string base_path = get_log_filepath();
        
        // Delete the oldest file if we're at max
        std::string oldest = base_path + "." + std::to_string(max_files_);
        std::remove(oldest.c_str());
        
        // Rename existing rotated files
        for (int i = max_files_ - 1; i >= 1; --i)
        {
            std::string old_name = base_path + "." + std::to_string(i);
            std::string new_name = base_path + "." + std::to_string(i + 1);
            std::rename(old_name.c_str(), new_name.c_str());
        }
        
        // Rename current log file to .1
        std::string rotated = base_path + ".1";
        std::rename(base_path.c_str(), rotated.c_str());
        
        // Open new log file
        file_.open(base_path, std::ios::out | std::ios::trunc);
        current_size_ = 0;
        
        if (file_.is_open())
        {
            std::string msg = "[" + get_timestamp() + "] [INFO ] Log rotated, previous log: " + rotated;
            file_ << msg << "\n";
            file_.flush();
            current_size_ = msg.size() + 1;
        }
    }

    std::string get_log_filepath() const
    {
        if (log_path_.empty())
        {
            return log_filename_;
        }
        
        std::string path = log_path_;
        // Ensure path ends with separator
        if (!path.empty() && path.back() != '/' && path.back() != '\\')
        {
            path += '/';
        }
        return path + log_filename_;
    }

    static void create_directory(const std::string& path)
    {
        if (path.empty()) return;
        
        // Try to create directory (ignore errors if it exists)
        MKDIR(path.c_str());
    }

    std::string get_timestamp()
    {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;

        std::tm tm_buf;
#ifdef _WIN32
        localtime_s(&tm_buf, &time);
#else
        localtime_r(&time, &tm_buf);
#endif

        std::ostringstream oss;
        oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S")
            << '.' << std::setfill('0') << std::setw(3) << ms.count();
        return oss.str();
    }

    static std::string level_to_string(LogLevel level)
    {
        switch (level)
        {
            case LogLevel::LOG_DEBUG:   return "DEBUG";
            case LogLevel::LOG_INFO:    return "INFO ";
            case LogLevel::LOG_WARNING: return "WARN ";
            case LogLevel::LOG_ERROR:   return "ERROR";
            default:                    return "?????";
        }
    }

    void print_colored(LogLevel level, const std::string& message)
    {
#ifdef _WIN32
        // Windows console colors
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        WORD color;
        switch (level)
        {
            case LogLevel::LOG_DEBUG:   color = 8;  break;  // Gray
            case LogLevel::LOG_INFO:    color = 7;  break;  // White
            case LogLevel::LOG_WARNING: color = 14; break;  // Yellow
            case LogLevel::LOG_ERROR:   color = 12; break;  // Red
            default:                    color = 7;  break;
        }
        SetConsoleTextAttribute(hConsole, color);
        std::cout << message << "\n";
        SetConsoleTextAttribute(hConsole, 7);  // Reset to white
#else
        // ANSI colors for Unix
        const char* color;
        switch (level)
        {
            case LogLevel::LOG_DEBUG:   color = "\033[90m"; break;  // Gray
            case LogLevel::LOG_INFO:    color = "\033[0m";  break;  // Default
            case LogLevel::LOG_WARNING: color = "\033[33m"; break;  // Yellow
            case LogLevel::LOG_ERROR:   color = "\033[31m"; break;  // Red
            default:                    color = "\033[0m";  break;
        }
        std::cout << color << message << "\033[0m\n";
#endif
    }

    static std::string format_size(size_t bytes)
    {
        std::ostringstream oss;
        if (bytes < 1024)
        {
            oss << bytes << " B";
        }
        else if (bytes < 1024 * 1024)
        {
            oss << std::fixed << std::setprecision(1) << (bytes / 1024.0) << " KB";
        }
        else
        {
            oss << std::fixed << std::setprecision(1) << (bytes / 1024.0 / 1024.0) << " MB";
        }
        return oss.str();
    }

    bool enabled_ = true;
    LogLevel min_level_ = LogLevel::LOG_INFO;
    std::string log_path_;
    std::string log_filename_;
    int max_file_size_ = 10 * 1024 * 1024;  // 10MB
    int max_files_ = 5;
    size_t current_size_ = 0;
    std::ofstream file_;
    std::mutex mutex_;
};

// Convenience macros for logging
#define LOG_DEBUG(msg)   ServerLogger::instance().debug(msg)
#define LOG_INFO(msg)    ServerLogger::instance().info(msg)
#define LOG_WARNING(msg) ServerLogger::instance().warning(msg)
#define LOG_ERROR(msg)   ServerLogger::instance().error(msg)
