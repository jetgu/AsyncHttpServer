#include <iostream>
#include <mutex>
#include <condition_variable>
#include "LuaHttpServer/AsyncHttpServer.h"
#include "LuaHttpServer/LuaScriptHandler.h"
#include "LuaHttpServer/ServerConfig.h"
#include "LuaHttpServer/ServerLogger.h"

int main(int argc, char* argv[])
{
    try
    {
        // Determine config file path (can be passed as command line argument)
        std::string config_path = "server_config.json";
        if (argc > 1)
        {
            config_path = argv[1];
        }
        
        // Load configuration
        std::cout << "Loading configuration from: " << config_path << "\n";
        ServerConfig config;
        
        try
        {
            config = ServerConfig::load_from_file(config_path);
            std::cout << "Configuration loaded successfully.\n";
        }
        catch (const std::exception& e)
        {
            std::cout << "Could not load config file: " << e.what() << "\n";
            std::cout << "Creating default configuration...\n";
            config = ServerConfig();
            config.port = 8082;
            config.lua_script_path = "./bbs/bbs.lua";
            config.web_root = "./www";
            
            // Save default config for future use
            try
            {
                config.save_to_file(config_path);
                std::cout << "Default configuration saved to: " << config_path << "\n";
            }
            catch (...)
            {
                // Ignore save errors
            }
        }
        
        // Configure the logger
        ServerLogger::instance().configure(
            config.enable_logging,
            config.log_path,
            config.log_file,
            config.log_max_size,
            config.log_max_files,
            LogLevel::LOG_INFO
        );
        
        LOG_INFO("=== Server Initializing ===");
        
        // Print configuration summary
        config.print_summary();
 
        // In C++ code:
        LuaScriptHandler lua_handler;
        
        // Apply configuration
        if (!config.web_root.empty())
        {
            lua_handler.set_web_root(config.web_root);
        }
        
        if (!config.lua_script_path.empty())
        {
            std::cout << "Loading Lua script: " << config.lua_script_path << "\n";
            lua_handler.load_script(config.lua_script_path);
        }
        else
        {
            std::cerr << "Warning: No Lua script path configured!\n";
        }

        AsyncHttpServer lua_srv;
        lua_srv.on_request = lua_handler.get_handler();
        
        // Start server with configured port and threads
        // Use SSL if configured
        if (config.ssl_enabled && !config.ssl_cert_file.empty() && !config.ssl_key_file.empty())
        {
            std::cout << "Starting server with SSL enabled...\n";
            lua_srv.start(static_cast<unsigned short>(config.port), 
                          config.ssl_cert_file, 
                          config.ssl_key_file, 
                          config.worker_threads);
        }
        else
        {
            lua_srv.start(static_cast<unsigned short>(config.port), config.worker_threads);
        }
        
        std::cout << "\n=== Server Started ===\n";
        std::cout << "Listening on: http" << (config.ssl_enabled ? "s" : "") 
                  << "://localhost:" << config.port << "\n";
        std::cout << "Worker threads: " << config.worker_threads << "\n";
        std::cout << "Web root: " << config.web_root << "\n";
        std::cout << "Lua script: " << config.lua_script_path << "\n";
        if (config.ssl_enabled)
        {
            std::cout << "SSL Certificate: " << config.ssl_cert_file << "\n";
            std::cout << "SSL Key: " << config.ssl_key_file << "\n";
        }
        std::cout << "=======================\n";



        printf("\nPress Enter to stop the server...\n");
        getchar();
        
        LOG_INFO("Shutting down server...");
        lua_srv.stop();
        LOG_INFO("Server stopped successfully.");
        
        // Close logger
        ServerLogger::instance().close();

    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}

