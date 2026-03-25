#pragma once
#pragma once
// ServerConfig.h
// Configuration file parser for AsyncHttpServer
// Supports JSON configuration for server settings

#include <string>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <cctype>

struct ServerConfig
{
    // Server settings
    int port = 8080;
    int worker_threads = 4;
    
    // SSL settings
    bool ssl_enabled = false;
    std::string ssl_cert_file;
    std::string ssl_key_file;
    std::string ssl_password;
    
    // Lua handler settings
    std::string lua_script_path;
    std::string web_root = "./www";
    
    // Server behavior
    int max_body_size = 10 * 1024 * 1024;  // 10MB default
    int request_timeout = 30;               // seconds
    
    // Logging settings
    bool enable_logging = true;
    std::string log_path = "./logs";        // Log directory
    std::string log_file = "server.log";    // Log filename
    int log_max_size = 10 * 1024 * 1024;    // 10MB default, rotate when exceeded
    int log_max_files = 5;                  // Keep up to 5 rotated log files
    
    // Static file serving
    bool serve_static_files = true;
    std::string static_path = "/static";
    
    // Default constructor with sensible defaults
    ServerConfig() = default;
    
    // Load configuration from a JSON file
    static ServerConfig load_from_file(const std::string& config_path)
    {
        ServerConfig config;
        
        std::ifstream file(config_path);
        if (!file.is_open())
        {
            throw std::runtime_error("Cannot open config file: " + config_path);
        }
        
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string json = buffer.str();
        
        config.parse_json(json);
        return config;
    }
    
    // Try to load from file, return default config if file doesn't exist
    static ServerConfig load_or_default(const std::string& config_path)
    {
        std::ifstream file(config_path);
        if (!file.is_open())
        {
            return ServerConfig();  // Return default config
        }
        
        return load_from_file(config_path);
    }
    
    // Save current configuration to a JSON file
    void save_to_file(const std::string& config_path) const
    {
        std::ofstream file(config_path);
        if (!file.is_open())
        {
            throw std::runtime_error("Cannot create config file: " + config_path);
        }
        
        file << to_json();
    }
    
    // Generate default config file
    static void create_default_config(const std::string& config_path)
    {
        ServerConfig config;
        config.save_to_file(config_path);
    }
    
    // Convert to JSON string
    std::string to_json() const
    {
        std::ostringstream oss;
        oss << "{\n";
        oss << "    \"server\": {\n";
        oss << "        \"port\": " << port << ",\n";
        oss << "        \"worker_threads\": " << worker_threads << ",\n";
        oss << "        \"max_body_size\": " << max_body_size << ",\n";
        oss << "        \"request_timeout\": " << request_timeout << "\n";
        oss << "    },\n";
        oss << "    \"ssl\": {\n";
        oss << "        \"enabled\": " << (ssl_enabled ? "true" : "false") << ",\n";
        oss << "        \"cert_file\": \"" << escape_json(ssl_cert_file) << "\",\n";
        oss << "        \"key_file\": \"" << escape_json(ssl_key_file) << "\",\n";
        oss << "        \"password\": \"" << escape_json(ssl_password) << "\"\n";
        oss << "    },\n";
        oss << "    \"logging\": {\n";
        oss << "        \"enabled\": " << (enable_logging ? "true" : "false") << ",\n";
        oss << "        \"path\": \"" << escape_json(log_path) << "\",\n";
        oss << "        \"file\": \"" << escape_json(log_file) << "\",\n";
        oss << "        \"max_size\": " << log_max_size << ",\n";
        oss << "        \"max_files\": " << log_max_files << "\n";
        oss << "    },\n";
        oss << "    \"lua\": {\n";
        oss << "        \"script_path\": \"" << escape_json(lua_script_path) << "\",\n";
        oss << "        \"web_root\": \"" << escape_json(web_root) << "\"\n";
        oss << "    },\n";
        oss << "    \"static_files\": {\n";
        oss << "        \"enabled\": " << (serve_static_files ? "true" : "false") << ",\n";
        oss << "        \"path\": \"" << escape_json(static_path) << "\"\n";
        oss << "    }\n";
        oss << "}\n";
        return oss.str();
    }
    
    // Print configuration summary
    void print_summary() const
    {
        std::cout << "=== Server Configuration ===\n";
        std::cout << "Port: " << port << "\n";
        std::cout << "Worker threads: " << worker_threads << "\n";
        std::cout << "SSL: " << (ssl_enabled ? "enabled" : "disabled") << "\n";
        if (ssl_enabled)
        {
            std::cout << "  Cert file: " << ssl_cert_file << "\n";
            std::cout << "  Key file: " << ssl_key_file << "\n";
        }
        std::cout << "Lua script: " << (lua_script_path.empty() ? "(none)" : lua_script_path) << "\n";
        std::cout << "Web root: " << web_root << "\n";
        std::cout << "Max body size: " << (max_body_size / 1024 / 1024) << " MB\n";
        std::cout << "Request timeout: " << request_timeout << " seconds\n";
        std::cout << "Logging: " << (enable_logging ? "enabled" : "disabled") << "\n";
        if (enable_logging)
        {
            std::cout << "  Log path: " << log_path << "\n";
            std::cout << "  Log file: " << log_file << "\n";
            std::cout << "  Max size: " << (log_max_size / 1024 / 1024) << " MB\n";
            std::cout << "  Max files: " << log_max_files << "\n";
        }
        std::cout << "============================\n";
    }

private:
    // Simple JSON string escaping
    static std::string escape_json(const std::string& s)
    {
        std::string result;
        for (char c : s)
        {
            switch (c)
            {
                case '"':  result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\n': result += "\\n";  break;
                case '\r': result += "\\r";  break;
                case '\t': result += "\\t";  break;
                default:   result += c;      break;
            }
        }
        return result;
    }
    
    // Simple JSON parsing (handles the specific format we generate)
    void parse_json(const std::string& json)
    {
        // Parse server section
        port = parse_int(json, "port", port);
        worker_threads = parse_int(json, "worker_threads", worker_threads);
        max_body_size = parse_int(json, "max_body_size", max_body_size);
        request_timeout = parse_int(json, "request_timeout", request_timeout);
        
        // Parse SSL section
        ssl_enabled = parse_bool(json, "enabled", ssl_enabled);
        ssl_cert_file = parse_string(json, "cert_file", ssl_cert_file);
        ssl_key_file = parse_string(json, "key_file", ssl_key_file);
        ssl_password = parse_string(json, "password", ssl_password);
        
        // Parse logging section
        size_t logging_pos = json.find("\"logging\"");
        if (logging_pos != std::string::npos)
        {
            std::string logging_section = json.substr(logging_pos);
            size_t end_brace = logging_section.find('}');
            if (end_brace != std::string::npos)
            {
                logging_section = logging_section.substr(0, end_brace + 1);
                enable_logging = parse_bool(logging_section, "enabled", enable_logging);
                log_path = parse_string(logging_section, "path", log_path);
                log_file = parse_string(logging_section, "file", log_file);
                log_max_size = parse_int(logging_section, "max_size", log_max_size);
                log_max_files = parse_int(logging_section, "max_files", log_max_files);
            }
        }
        
        // Parse Lua section
        lua_script_path = parse_string(json, "script_path", lua_script_path);
        web_root = parse_string(json, "web_root", web_root);
        
        // Parse static files section
        serve_static_files = parse_bool(json, "\"static_files\"", serve_static_files);
        size_t static_pos = json.find("\"static_files\"");
        if (static_pos != std::string::npos)
        {
            std::string static_section = json.substr(static_pos);
            size_t end_brace = static_section.find('}');
            if (end_brace != std::string::npos)
            {
                static_section = static_section.substr(0, end_brace);
                serve_static_files = parse_bool(static_section, "enabled", serve_static_files);
                static_path = parse_string(static_section, "path", static_path);
            }
        }
    }
    
    // Parse integer value from JSON
    static int parse_int(const std::string& json, const std::string& key, int default_value)
    {
        std::string search = "\"" + key + "\"";
        size_t pos = json.find(search);
        if (pos == std::string::npos) return default_value;
        
        pos = json.find(':', pos);
        if (pos == std::string::npos) return default_value;
        
        pos++;
        while (pos < json.size() && std::isspace(json[pos])) pos++;
        
        std::string value;
        while (pos < json.size() && (std::isdigit(json[pos]) || json[pos] == '-'))
        {
            value += json[pos++];
        }
        
        if (value.empty()) return default_value;
        return std::stoi(value);
    }
    
    // Parse boolean value from JSON
    static bool parse_bool(const std::string& json, const std::string& key, bool default_value)
    {
        std::string search = "\"" + key + "\"";
        size_t pos = json.find(search);
        if (pos == std::string::npos) return default_value;
        
        pos = json.find(':', pos);
        if (pos == std::string::npos) return default_value;
        
        pos++;
        while (pos < json.size() && std::isspace(json[pos])) pos++;
        
        if (json.substr(pos, 4) == "true") return true;
        if (json.substr(pos, 5) == "false") return false;
        return default_value;
    }
    
    // Parse string value from JSON
    static std::string parse_string(const std::string& json, const std::string& key, const std::string& default_value)
    {
        std::string search = "\"" + key + "\"";
        size_t pos = json.find(search);
        if (pos == std::string::npos) return default_value;
        
        pos = json.find(':', pos);
        if (pos == std::string::npos) return default_value;
        
        pos = json.find('"', pos + 1);
        if (pos == std::string::npos) return default_value;
        
        pos++;  // Skip opening quote
        std::string value;
        while (pos < json.size() && json[pos] != '"')
        {
            if (json[pos] == '\\' && pos + 1 < json.size())
            {
                pos++;
                switch (json[pos])
                {
                    case 'n': value += '\n'; break;
                    case 'r': value += '\r'; break;
                    case 't': value += '\t'; break;
                    case '"': value += '"';  break;
                    case '\\': value += '\\'; break;
                    default: value += json[pos]; break;
                }
            }
            else
            {
                value += json[pos];
            }
            pos++;
        }
        
        return value;
    }
};
