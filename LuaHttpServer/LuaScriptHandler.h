#pragma once


// ---------------------------------------------------------------------------
// LuaScriptHandler ?Lua scripting support for AsyncHttpServer
//
// To enable Lua support:
//   1. Install Lua (https://www.lua.org/) or use vcpkg: vcpkg install lua
//   2. Add Lua include directory to your project
//   3. Add Lua library directory to your project
//   4. Define HAS_LUA=1 in preprocessor definitions
//
// To enable SQLite support (optional):
//   1. Install SQLite or use vcpkg: vcpkg install sqlite3
//   2. Define HAS_SQLITE=1 in preprocessor definitions
//
// If HAS_LUA is not defined, this header provides a stub implementation
// that throws an error when used.
// ---------------------------------------------------------------------------

#include <string>
#include <map>
#include <stdexcept>
#include <mutex>
#include <memory>
#include <functional>

#include "AsyncHttpServer.h"

#if defined(HAS_LUA) && HAS_LUA

// Lua headers
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

// Link against Lua library (adjust name for your version)
#if defined(_WIN32)
#pragma comment(lib, "lua54.lib")
#endif

// Optional SQLite support
#if defined(HAS_SQLITE) && HAS_SQLITE
#include <sqlite3.h>
#endif

// ---------------------------------------------------------------------------
// LuaScriptHandler
//
// Wraps a Lua state and provides a handler for AsyncHttpServer that executes
// Lua scripts to process HTTP requests.
//
// Usage:
//   LuaScriptHandler lua_handler;
//   lua_handler.load_script("handler.lua");
//   // or
//   lua_handler.load_string(R"(
//       function handle_request(req, resp)
//           if req.method == "GET" and req.path == "/hello" then
//               resp:set_body("Hello from Lua!")
//           else
//               resp:set_status(404, "Not Found")
//               resp:set_body("Not Found")
//           end
//       end
//   )");
//
//   AsyncHttpServer srv;
//   srv.on_request = lua_handler.get_handler();
//   srv.start(8080);
//
// Lua API:
//   req (table):
//     - req.method         : string ("GET", "POST", etc.)
//     - req.path           : string ("/api/resource")
//     - req.query          : string ("name=value&...")
//     - req.version        : string ("HTTP/1.1")
//     - req.body           : string (request body)
//     - req.remote_address : string (client IP)
//     - req.headers        : table  (header_name -> value)
//
//   resp (userdata with methods):
//     - resp:set_status(code)
//     - resp:set_status(code, message)
//     - resp:set_body(body)
//     - resp:set_body(body, content_type)
//     - resp:set_header(name, value)
//     - resp:get_status()           -> code
//     - resp:get_body()             -> body string
//
//   Global helper functions:
//     - read_file(path)             -> string (file contents) or nil, error
//     - file_exists(path)           -> boolean
//     - get_mime_type(path)         -> string (guessed MIME type)
//     - render_template(tpl, vars)  -> string (rendered HTML)
//
//   Template syntax:
//     {{ var }}                     - Output variable (HTML escaped)
//     {! var !}                     - Output variable (raw, no escaping)
//     {% lua code %}                - Execute Lua code
//     {% for i, item in ipairs(list) do %} ... {% end %}
//     {% if condition then %} ... {% else %} ... {% end %}
//
//   SQLite functions (if HAS_SQLITE=1):
//     - db_open(path)               -> db handle or nil, error
//     - db_close(db)                -> boolean
//     - db_execute(db, sql)         -> boolean, error (for INSERT/UPDATE/DELETE)
//     - db_query(db, sql)           -> array of row tables or nil, error
//     - db_escape(str)              -> escaped string for SQL
// ---------------------------------------------------------------------------
class LuaScriptHandler
{
public:
    LuaScriptHandler()
        : L_(luaL_newstate(), lua_close)
        , web_root_(".")
    {
        if (!L_)
            throw std::runtime_error("LuaScriptHandler: failed to create Lua state");

        luaL_openlibs(L_.get());
        register_response_metatable();
        register_helper_functions();
    }

    // Set the web root directory for file serving
    void set_web_root(const std::string& path)
    {
        web_root_ = path;
        // Update Lua global
        std::lock_guard<std::mutex> lock(mtx_);
        lua_pushstring(L_.get(), web_root_.c_str());
        lua_setglobal(L_.get(), "WEB_ROOT");
    }

    // Load and execute a Lua script file
    void load_script(const std::string& filename)
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (luaL_dofile(L_.get(), filename.c_str()) != LUA_OK)
        {
            std::string err = lua_tostring(L_.get(), -1);
            lua_pop(L_.get(), 1);
            throw std::runtime_error("LuaScriptHandler: " + err);
        }
    }

    // Load and execute a Lua script from a string
    void load_string(const std::string& script)
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (luaL_dostring(L_.get(), script.c_str()) != LUA_OK)
        {
            std::string err = lua_tostring(L_.get(), -1);
            lua_pop(L_.get(), 1);
            throw std::runtime_error("LuaScriptHandler: " + err);
        }
    }

    // Get a handler function for AsyncHttpServer::on_request
    AsyncHttpServer::Handler get_handler()
    {
        return [this](const AsyncHttpRequest& req, AsyncHttpResponse& resp)
        {
            this->handle_request(req, resp);
        };
    }

private:
    std::unique_ptr<lua_State, decltype(&lua_close)> L_;
    std::mutex mtx_;  // Lua state is not thread-safe
    std::string web_root_;

    // -----------------------------------------------------------------------
    // Register the "Response" metatable for resp userdata
    // -----------------------------------------------------------------------
    void register_response_metatable()
    {
        lua_State* L = L_.get();

        luaL_newmetatable(L, "AsyncHttpResponse");

        // __index = method table
        lua_pushvalue(L, -1);
        lua_setfield(L, -2, "__index");

        // Register methods
        static const luaL_Reg methods[] = {
            {"set_status", lua_resp_set_status},
            {"set_body",   lua_resp_set_body},
            {"set_header", lua_resp_set_header},
            {"get_status", lua_resp_get_status},
            {"get_body",   lua_resp_get_body},
            {nullptr, nullptr}
        };

        luaL_setfuncs(L, methods, 0);
        lua_pop(L, 1);
    }

    // -----------------------------------------------------------------------
    // Register global helper functions for file operations
    // -----------------------------------------------------------------------
    void register_helper_functions()
    {
        lua_State* L = L_.get();

        // read_file(path) -> string or nil, error
        lua_pushcfunction(L, lua_read_file);
        lua_setglobal(L, "read_file");

        // file_exists(path) -> boolean
        lua_pushcfunction(L, lua_file_exists);
        lua_setglobal(L, "file_exists");

        // get_mime_type(path) -> string
        lua_pushcfunction(L, lua_get_mime_type);
        lua_setglobal(L, "get_mime_type");

        // write_file(path, data) -> boolean, error
        lua_pushcfunction(L, lua_write_file);
        lua_setglobal(L, "write_file");

        // generate_captcha_image(text) -> image_data (PNG binary)
        lua_pushcfunction(L, lua_generate_captcha_image);
        lua_setglobal(L, "generate_captcha_image");

        // render_template(template_str, vars_table) -> rendered string
        lua_pushcfunction(L, lua_render_template);
        lua_setglobal(L, "render_template");

        // Set WEB_ROOT global
        lua_pushstring(L, web_root_.c_str());
        lua_setglobal(L, "WEB_ROOT");

#if defined(HAS_SQLITE) && HAS_SQLITE
        // Register SQLite functions
        register_sqlite_functions();
#endif
    }

    // -----------------------------------------------------------------------
    // Simple 5x7 bitmap font for CAPTCHA
    // -----------------------------------------------------------------------
    static const unsigned char CAPTCHA_FONT[128][7];

    // -----------------------------------------------------------------------
    // Lua C function: generate_captcha_image(text) -> BMP image data
    // Creates a simple BMP image with the given text
    // -----------------------------------------------------------------------
    static int lua_generate_captcha_image(lua_State* L)
    {
        const char* text = luaL_checkstring(L, 1);
        size_t text_len = strlen(text);

        // Image dimensions
        const int char_width = 12;
        const int char_height = 16;
        const int padding = 10;
        const int width = static_cast<int>(text_len) * char_width + padding * 2;
        const int height = char_height + padding * 2;
        const int row_stride = (width * 3 + 3) & ~3;  // BMP rows are 4-byte aligned

        // BMP file header (14 bytes)
        unsigned char bmp_header[14] = {
            'B', 'M',           // Signature
            0, 0, 0, 0,         // File size (will fill later)
            0, 0, 0, 0,         // Reserved
            54, 0, 0, 0         // Pixel data offset
        };

        // DIB header (40 bytes - BITMAPINFOHEADER)
        unsigned char dib_header[40] = {
            40, 0, 0, 0,        // DIB header size
            0, 0, 0, 0,         // Width (will fill later)
            0, 0, 0, 0,         // Height (will fill later)
            1, 0,               // Color planes
            24, 0,              // Bits per pixel (24-bit RGB)
            0, 0, 0, 0,         // No compression
            0, 0, 0, 0,         // Image size (can be 0 for uncompressed)
            0, 0, 0, 0,         // Horizontal resolution
            0, 0, 0, 0,         // Vertical resolution
            0, 0, 0, 0,         // Colors in palette
            0, 0, 0, 0          // Important colors
        };

        // Fill in dimensions
        int file_size = 54 + row_stride * height;
        bmp_header[2] = file_size & 0xFF;
        bmp_header[3] = (file_size >> 8) & 0xFF;
        bmp_header[4] = (file_size >> 16) & 0xFF;
        bmp_header[5] = (file_size >> 24) & 0xFF;

        dib_header[4] = width & 0xFF;
        dib_header[5] = (width >> 8) & 0xFF;
        dib_header[6] = (width >> 16) & 0xFF;
        dib_header[7] = (width >> 24) & 0xFF;

        dib_header[8] = height & 0xFF;
        dib_header[9] = (height >> 8) & 0xFF;
        dib_header[10] = (height >> 16) & 0xFF;
        dib_header[11] = (height >> 24) & 0xFF;

        // Create pixel data
        std::vector<unsigned char> pixels(row_stride * height, 0);

        // Random number generator for noise and colors
        srand(static_cast<unsigned int>(time(nullptr)));

        // Background with gradient and noise
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                int idx = y * row_stride + x * 3;
                // Gradient background
                unsigned char base_r = 200 + (y * 55 / height);
                unsigned char base_g = 200 + (x * 55 / width);
                unsigned char base_b = 220;
                // Add noise
                int noise = (rand() % 30) - 15;
                pixels[idx + 0] = static_cast<unsigned char>(std::min(255, std::max(0, base_b + noise))); // B
                pixels[idx + 1] = static_cast<unsigned char>(std::min(255, std::max(0, base_g + noise))); // G
                pixels[idx + 2] = static_cast<unsigned char>(std::min(255, std::max(0, base_r + noise))); // R
            }
        }

        // Draw some random lines for obfuscation
        for (int i = 0; i < 5; ++i)
        {
            int x1 = rand() % width;
            int y1 = rand() % height;
            int x2 = rand() % width;
            int y2 = rand() % height;
            unsigned char line_r = rand() % 100 + 100;
            unsigned char line_g = rand() % 100 + 100;
            unsigned char line_b = rand() % 100 + 100;

            // Simple line drawing (Bresenham's algorithm)
            int dx = abs(x2 - x1), sx = x1 < x2 ? 1 : -1;
            int dy = -abs(y2 - y1), sy = y1 < y2 ? 1 : -1;
            int err = dx + dy;

            while (true)
            {
                if (x1 >= 0 && x1 < width && y1 >= 0 && y1 < height)
                {
                    int idx = y1 * row_stride + x1 * 3;
                    pixels[idx + 0] = line_b;
                    pixels[idx + 1] = line_g;
                    pixels[idx + 2] = line_r;
                }
                if (x1 == x2 && y1 == y2) break;
                int e2 = 2 * err;
                if (e2 >= dy) { err += dy; x1 += sx; }
                if (e2 <= dx) { err += dx; y1 += sy; }
            }
        }

        // Draw text characters
        for (size_t i = 0; i < text_len; ++i)
        {
            unsigned char c = static_cast<unsigned char>(text[i]);
            if (c >= 128) c = '?';

            int char_x = padding + static_cast<int>(i) * char_width + (rand() % 3 - 1);
            int char_y = padding + (rand() % 3 - 1);

            // Random color for each character (dark colors)
            unsigned char text_r = rand() % 80;
            unsigned char text_g = rand() % 80;
            unsigned char text_b = rand() % 80 + 50;

            // Draw character using bitmap font (scaled 2x)
            for (int row = 0; row < 7; ++row)
            {
                unsigned char font_row = CAPTCHA_FONT[c][row];
                for (int col = 0; col < 5; ++col)
                {
                    if (font_row & (0x10 >> col))
                    {
                        // Draw 2x2 pixel block for scaling
                        for (int dy = 0; dy < 2; ++dy)
                        {
                            for (int dx = 0; dx < 2; ++dx)
                            {
                                int px = char_x + col * 2 + dx;
                                int py = char_y + row * 2 + dy;
                                // BMP is bottom-up, so flip y
                                int bmp_y = height - 1 - py;
                                if (px >= 0 && px < width && bmp_y >= 0 && bmp_y < height)
                                {
                                    int idx = bmp_y * row_stride + px * 3;
                                    pixels[idx + 0] = text_b;
                                    pixels[idx + 1] = text_g;
                                    pixels[idx + 2] = text_r;
                                }
                            }
                        }
                    }
                }
            }
        }

        // Combine headers and pixels
        std::string result;
        result.append(reinterpret_cast<char*>(bmp_header), 14);
        result.append(reinterpret_cast<char*>(dib_header), 40);
        result.append(reinterpret_cast<char*>(pixels.data()), pixels.size());

        lua_pushlstring(L, result.c_str(), result.size());
        return 1;
    }

    // -----------------------------------------------------------------------
    // Lua C function: write_file(path, data) -> boolean, error
    // -----------------------------------------------------------------------
    static int lua_write_file(lua_State* L)
    {
        const char* path = luaL_checkstring(L, 1);
        size_t data_len = 0;
        const char* data = luaL_checklstring(L, 2, &data_len);

        FILE* f = fopen(path, "wb");
        if (!f)
        {
            lua_pushboolean(L, 0);
            lua_pushstring(L, "Cannot open file for writing");
            return 2;
        }

        size_t written = fwrite(data, 1, data_len, f);
        fclose(f);

        if (written != data_len)
        {
            lua_pushboolean(L, 0);
            lua_pushstring(L, "Failed to write all data");
            return 2;
        }

        lua_pushboolean(L, 1);
        return 1;
    }

    // -----------------------------------------------------------------------
    // Lua C function: render_template(template_str, vars_table) -> string
    // Template syntax:
    //   {{ var }}          - Output variable (HTML escaped)
    //   {! var !}          - Output variable (raw, no escaping)
    //   {% lua code %}     - Execute Lua code
    //   {% for item in list %} ... {% end %}  - Loop
    //   {% if condition %} ... {% else %} ... {% end %}  - Conditional
    // -----------------------------------------------------------------------
    static int lua_render_template(lua_State* L)
    {
        const char* tpl = luaL_checkstring(L, 1);
        
        // Track variable names we set so we can clear them later
        std::vector<std::string> var_names;
        
        // Second argument is optional vars table
        if (lua_gettop(L) >= 2 && lua_istable(L, 2))
        {
            // Copy vars to global scope for template access
            lua_pushnil(L);
            while (lua_next(L, 2) != 0)
            {
                const char* key = lua_tostring(L, -2);
                if (key)
                {
                    var_names.push_back(key);
                    lua_setglobal(L, key);
                }
                else
                {
                    lua_pop(L, 1);  // pop value if key is not a string
                }
            }
        }

        std::string result;
        std::string code = "local __output = {}\n"
                           "local function __write(s) table.insert(__output, tostring(s or '')) end\n"
                           "local function __escape(s)\n"
                           "  s = tostring(s or '')\n"
                           "  s = s:gsub('&', '&amp;')\n"
                           "  s = s:gsub('<', '&lt;')\n"
                           "  s = s:gsub('>', '&gt;')\n"
                           "  s = s:gsub('\"', '&quot;')\n"
                           "  return s\n"
                           "end\n";

        std::string template_str(tpl);
        std::string::size_type pos = 0;
        std::string::size_type last_pos = 0;

        while (pos < template_str.size())
        {
            // Look for template tags
            std::string::size_type tag_start = template_str.find_first_of("{", pos);
            
            if (tag_start == std::string::npos)
            {
                // No more tags, output rest as literal
                std::string literal = template_str.substr(last_pos);
                code += "__write([=[" + literal + "]=])\n";
                break;
            }

            // Check what kind of tag - must be {{ }}, {! !}, or {% %}
            if (tag_start + 1 < template_str.size())
            {
                char next_char = template_str[tag_start + 1];
                
                if (next_char == '{')  // {{ var }} - escaped output
                {
                    std::string::size_type end = template_str.find("}}", tag_start + 2);
                    if (end != std::string::npos)
                    {
                        // Output literal text before this tag
                        if (tag_start > last_pos)
                        {
                            std::string literal = template_str.substr(last_pos, tag_start - last_pos);
                            code += "__write([=[" + literal + "]=])\n";
                        }

                        std::string expr = template_str.substr(tag_start + 2, end - tag_start - 2);
                        // Trim whitespace
                        size_t start = expr.find_first_not_of(" \t\n\r");
                        size_t finish = expr.find_last_not_of(" \t\n\r");
                        if (start != std::string::npos)
                            expr = expr.substr(start, finish - start + 1);
                        
                        code += "__write(__escape(" + expr + "))\n";
                        last_pos = end + 2;
                        pos = last_pos;
                        continue;
                    }
                }
                else if (next_char == '!')  // {! var !} - raw output
                {
                    std::string::size_type end = template_str.find("!}", tag_start + 2);
                    if (end != std::string::npos)
                    {
                        // Output literal text before this tag
                        if (tag_start > last_pos)
                        {
                            std::string literal = template_str.substr(last_pos, tag_start - last_pos);
                            code += "__write([=[" + literal + "]=])\n";
                        }

                        std::string expr = template_str.substr(tag_start + 2, end - tag_start - 2);
                        size_t start = expr.find_first_not_of(" \t\n\r");
                        size_t finish = expr.find_last_not_of(" \t\n\r");
                        if (start != std::string::npos)
                            expr = expr.substr(start, finish - start + 1);
                        
                        code += "__write(" + expr + ")\n";
                        last_pos = end + 2;
                        pos = last_pos;
                        continue;
                    }
                }
                else if (next_char == '%')  // {% code %} - Lua code
                {
                    std::string::size_type end = template_str.find("%}", tag_start + 2);
                    if (end != std::string::npos)
                    {
                        // Output literal text before this tag
                        if (tag_start > last_pos)
                        {
                            std::string literal = template_str.substr(last_pos, tag_start - last_pos);
                            code += "__write([=[" + literal + "]=])\n";
                        }

                        std::string lua_code = template_str.substr(tag_start + 2, end - tag_start - 2);
                        size_t start = lua_code.find_first_not_of(" \t\n\r");
                        size_t finish = lua_code.find_last_not_of(" \t\n\r");
                        if (start != std::string::npos)
                            lua_code = lua_code.substr(start, finish - start + 1);
                        
                        code += lua_code + "\n";
                        last_pos = end + 2;
                        pos = last_pos;
                        continue;
                    }
                }
            }
            
            // Not a template tag, skip this '{' character
            pos = tag_start + 1;
        }

        code += "return table.concat(__output)\n";

        // Execute the generated code
        if (luaL_dostring(L, code.c_str()) != LUA_OK)
        {
            // Clear template variables before returning
            for (const auto& name : var_names)
            {
                lua_pushnil(L);
                lua_setglobal(L, name.c_str());
            }
            // Return nil and error message
            lua_pushnil(L);
            lua_insert(L, -2);  // put nil before error message
            return 2;
        }

        // Clear template variables after successful render
        for (const auto& name : var_names)
        {
            lua_pushnil(L);
            lua_setglobal(L, name.c_str());
        }

        return 1;  // Return the rendered string
    }

#if defined(HAS_SQLITE) && HAS_SQLITE
    // -----------------------------------------------------------------------
    // Register SQLite helper functions
    // -----------------------------------------------------------------------
    void register_sqlite_functions()
    {
        lua_State* L = L_.get();

        // Create SQLite database metatable
        luaL_newmetatable(L, "SQLiteDB");
        lua_pushcfunction(L, lua_db_gc);
        lua_setfield(L, -2, "__gc");
        lua_pop(L, 1);

        // db_open(path) -> db handle or nil, error
        lua_pushcfunction(L, lua_db_open);
        lua_setglobal(L, "db_open");

        // db_close(db) -> boolean
        lua_pushcfunction(L, lua_db_close);
        lua_setglobal(L, "db_close");

        // db_execute(db, sql) -> boolean, error
        lua_pushcfunction(L, lua_db_execute);
        lua_setglobal(L, "db_execute");

        // db_query(db, sql) -> array of rows or nil, error
        lua_pushcfunction(L, lua_db_query);
        lua_setglobal(L, "db_query");

        // db_escape(str) -> escaped string
        lua_pushcfunction(L, lua_db_escape);
        lua_setglobal(L, "db_escape");
    }

    // SQLite database handle wrapper
    struct SQLiteHandle
    {
        sqlite3* db;
    };

    static SQLiteHandle* check_db(lua_State* L, int index = 1)
    {
        return (SQLiteHandle*)luaL_checkudata(L, index, "SQLiteDB");
    }

    // db_open(path) -> db handle or nil, error
    static int lua_db_open(lua_State* L)
    {
        const char* path = luaL_checkstring(L, 1);

        SQLiteHandle* handle = (SQLiteHandle*)lua_newuserdata(L, sizeof(SQLiteHandle));
        handle->db = nullptr;

        int rc = sqlite3_open(path, &handle->db);
        if (rc != SQLITE_OK)
        {
            const char* err = sqlite3_errmsg(handle->db);
            sqlite3_close(handle->db);
            lua_pop(L, 1);
            lua_pushnil(L);
            lua_pushstring(L, err);
            return 2;
        }

        luaL_getmetatable(L, "SQLiteDB");
        lua_setmetatable(L, -2);
        return 1;
    }

    // db_close(db) -> boolean
    static int lua_db_close(lua_State* L)
    {
        SQLiteHandle* handle = check_db(L);
        if (handle->db)
        {
            sqlite3_close(handle->db);
            handle->db = nullptr;
            lua_pushboolean(L, 1);
        }
        else
        {
            lua_pushboolean(L, 0);
        }
        return 1;
    }

    // __gc metamethod for automatic cleanup
    static int lua_db_gc(lua_State* L)
    {
        SQLiteHandle* handle = (SQLiteHandle*)luaL_checkudata(L, 1, "SQLiteDB");
        if (handle->db)
        {
            sqlite3_close(handle->db);
            handle->db = nullptr;
        }
        return 0;
    }

    // db_execute(db, sql) -> boolean, error
    static int lua_db_execute(lua_State* L)
    {
        SQLiteHandle* handle = check_db(L, 1);
        const char* sql = luaL_checkstring(L, 2);

        if (!handle->db)
        {
            lua_pushboolean(L, 0);
            lua_pushstring(L, "Database not open");
            return 2;
        }

        char* errmsg = nullptr;
        int rc = sqlite3_exec(handle->db, sql, nullptr, nullptr, &errmsg);

        if (rc != SQLITE_OK)
        {
            lua_pushboolean(L, 0);
            lua_pushstring(L, errmsg ? errmsg : "Unknown error");
            if (errmsg) sqlite3_free(errmsg);
            return 2;
        }

        lua_pushboolean(L, 1);
        return 1;
    }

    // db_query(db, sql) -> array of row tables or nil, error
    static int lua_db_query(lua_State* L)
    {
        SQLiteHandle* handle = check_db(L, 1);
        const char* sql = luaL_checkstring(L, 2);

        if (!handle->db)
        {
            lua_pushnil(L);
            lua_pushstring(L, "Database not open");
            return 2;
        }

        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(handle->db, sql, -1, &stmt, nullptr);

        if (rc != SQLITE_OK)
        {
            lua_pushnil(L);
            lua_pushstring(L, sqlite3_errmsg(handle->db));
            return 2;
        }

        lua_newtable(L);  // Result array
        int row_index = 1;

        while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
        {
            lua_newtable(L);  // Row table

            int col_count = sqlite3_column_count(stmt);
            for (int i = 0; i < col_count; ++i)
            {
                const char* col_name = sqlite3_column_name(stmt, i);
                int col_type = sqlite3_column_type(stmt, i);

                switch (col_type)
                {
                case SQLITE_INTEGER:
                    lua_pushinteger(L, sqlite3_column_int64(stmt, i));
                    break;
                case SQLITE_FLOAT:
                    lua_pushnumber(L, sqlite3_column_double(stmt, i));
                    break;
                case SQLITE_TEXT:
                    lua_pushstring(L, (const char*)sqlite3_column_text(stmt, i));
                    break;
                case SQLITE_BLOB:
                    lua_pushlstring(L, (const char*)sqlite3_column_blob(stmt, i),
                                    sqlite3_column_bytes(stmt, i));
                    break;
                case SQLITE_NULL:
                default:
                    lua_pushnil(L);
                    break;
                }
                lua_setfield(L, -2, col_name);
            }

            lua_rawseti(L, -2, row_index++);
        }

        sqlite3_finalize(stmt);

        if (rc != SQLITE_DONE)
        {
            lua_pop(L, 1);
            lua_pushnil(L);
            lua_pushstring(L, sqlite3_errmsg(handle->db));
            return 2;
        }

        return 1;
    }

    // db_escape(str) -> escaped string (doubles single quotes)
    static int lua_db_escape(lua_State* L)
    {
        const char* str = luaL_checkstring(L, 1);
        std::string result;
        result.reserve(strlen(str) * 2);

        for (const char* p = str; *p; ++p)
        {
            if (*p == '\'')
                result += "''";
            else
                result += *p;
        }

        lua_pushstring(L, result.c_str());
        return 1;
    }
#endif // HAS_SQLITE

    // -----------------------------------------------------------------------
    // Lua C function: read_file(path) -> string or nil, error
    // -----------------------------------------------------------------------
    static int lua_read_file(lua_State* L)
    {
        const char* path = luaL_checkstring(L, 1);

        FILE* f = fopen(path, "rb");
        if (!f)
        {
            lua_pushnil(L);
            lua_pushstring(L, "Cannot open file");
            return 2;
        }

        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);

        if (size < 0 || size > 100 * 1024 * 1024) // 100MB limit
        {
            fclose(f);
            lua_pushnil(L);
            lua_pushstring(L, "File too large or invalid");
            return 2;
        }

        std::string content(static_cast<size_t>(size), '\0');
        size_t read = fread(&content[0], 1, static_cast<size_t>(size), f);
        fclose(f);

        if (read != static_cast<size_t>(size))
        {
            lua_pushnil(L);
            lua_pushstring(L, "Failed to read file");
            return 2;
        }

        lua_pushlstring(L, content.c_str(), content.size());
        return 1;
    }

    // -----------------------------------------------------------------------
    // Lua C function: file_exists(path) -> boolean
    // -----------------------------------------------------------------------
    static int lua_file_exists(lua_State* L)
    {
        const char* path = luaL_checkstring(L, 1);
        FILE* f = fopen(path, "rb");
        if (f)
        {
            fclose(f);
            lua_pushboolean(L, 1);
        }
        else
        {
            lua_pushboolean(L, 0);
        }
        return 1;
    }

    // -----------------------------------------------------------------------
    // Lua C function: get_mime_type(path) -> string
    // -----------------------------------------------------------------------
    static int lua_get_mime_type(lua_State* L)
    {
        const char* path = luaL_checkstring(L, 1);
        std::string p(path);

        // Find extension
        std::string ext;
        size_t dot = p.rfind('.');
        if (dot != std::string::npos)
        {
            ext = p.substr(dot);
            // Convert to lowercase
            for (char& c : ext)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }

        // Map extension to MIME type
        const char* mime = "application/octet-stream";
        if (ext == ".html" || ext == ".htm") mime = "text/html";
        else if (ext == ".css")              mime = "text/css";
        else if (ext == ".js")               mime = "application/javascript";
        else if (ext == ".json")             mime = "application/json";
        else if (ext == ".xml")              mime = "application/xml";
        else if (ext == ".txt")              mime = "text/plain";
        else if (ext == ".png")              mime = "image/png";
        else if (ext == ".jpg" || ext == ".jpeg") mime = "image/jpeg";
        else if (ext == ".gif")              mime = "image/gif";
        else if (ext == ".svg")              mime = "image/svg+xml";
        else if (ext == ".ico")              mime = "image/x-icon";
        else if (ext == ".woff")             mime = "font/woff";
        else if (ext == ".woff2")            mime = "font/woff2";
        else if (ext == ".ttf")              mime = "font/ttf";
        else if (ext == ".pdf")              mime = "application/pdf";
        else if (ext == ".zip")              mime = "application/zip";

        lua_pushstring(L, mime);
        return 1;
    }

    // -----------------------------------------------------------------------
    // Push the request as a Lua table
    // -----------------------------------------------------------------------
    void push_request(lua_State* L, const AsyncHttpRequest& req)
    {
        lua_newtable(L);

        lua_pushstring(L, req.method.c_str());
        lua_setfield(L, -2, "method");

        lua_pushstring(L, req.path.c_str());
        lua_setfield(L, -2, "path");

        lua_pushstring(L, req.query.c_str());
        lua_setfield(L, -2, "query");


        lua_pushstring(L, req.version.c_str());
        lua_setfield(L, -2, "version");

        // Use pushlstring for body to handle binary data (e.g., file uploads)
        lua_pushlstring(L, req.body.c_str(), req.body.size());
        lua_setfield(L, -2, "body");

        lua_pushstring(L, req.remote_address.c_str());
        lua_setfield(L, -2, "remote_address");

        // Headers as nested table
        lua_newtable(L);
        for (const auto& kv : req.headers)
        {
            lua_pushstring(L, kv.second.c_str());
            lua_setfield(L, -2, kv.first.c_str());
        }
        lua_setfield(L, -2, "headers");
    }

    // -----------------------------------------------------------------------
    // Push the response as userdata with metatable
    // -----------------------------------------------------------------------
    void push_response(lua_State* L, AsyncHttpResponse* resp)
    {
        AsyncHttpResponse** udata = (AsyncHttpResponse**)lua_newuserdata(L, sizeof(AsyncHttpResponse*));
        *udata = resp;

        luaL_getmetatable(L, "AsyncHttpResponse");
        lua_setmetatable(L, -2);
    }

    // -----------------------------------------------------------------------
    // Call the Lua handle_request function
    // -----------------------------------------------------------------------
    void handle_request(const AsyncHttpRequest& req, AsyncHttpResponse& resp)
    {
        std::lock_guard<std::mutex> lock(mtx_);
        lua_State* L = L_.get();

        // Get handle_request function
        lua_getglobal(L, "handle_request");
        if (!lua_isfunction(L, -1))
        {
            lua_pop(L, 1);
            resp.set_status(500).set_body("Lua error: handle_request function not defined");
            return;
        }

        // Push arguments
        push_request(L, req);
        push_response(L, &resp);

        // Call: handle_request(req, resp)
        if (lua_pcall(L, 2, 0, 0) != LUA_OK)
        {
            std::string err = lua_tostring(L, -1);
            lua_pop(L, 1);
            resp.set_status(500).set_body("Lua error: " + err);
        }
    }

    // -----------------------------------------------------------------------
    // Lua C functions for Response methods
    // -----------------------------------------------------------------------

    static AsyncHttpResponse* check_response(lua_State* L, int index = 1)
    {
        AsyncHttpResponse** udata = (AsyncHttpResponse**)luaL_checkudata(L, index, "AsyncHttpResponse");
        return *udata;
    }

    // resp:set_status(code) or resp:set_status(code, message)
    static int lua_resp_set_status(lua_State* L)
    {
        AsyncHttpResponse* resp = check_response(L);
        int code = (int)luaL_checkinteger(L, 2);

        if (lua_gettop(L) >= 3 && lua_isstring(L, 3))
        {
            const char* msg = lua_tostring(L, 3);
            resp->set_status(code, msg);
        }
        else
        {
            resp->set_status(code);
        }

        return 0;
    }

    // resp:set_body(body) or resp:set_body(body, content_type)
    static int lua_resp_set_body(lua_State* L)
    {
        AsyncHttpResponse* resp = check_response(L);
        size_t body_len = 0;
        const char* body = luaL_checklstring(L, 2, &body_len);

        if (lua_gettop(L) >= 3 && lua_isstring(L, 3))
        {
            const char* content_type = lua_tostring(L, 3);
            resp->body.assign(body, body_len);
            resp->set_header("Content-Type", content_type);
        }
        else
        {
            resp->body.assign(body, body_len);
        }

        return 0;
    }

    // resp:set_header(name, value)
    static int lua_resp_set_header(lua_State* L)
    {
        AsyncHttpResponse* resp = check_response(L);
        const char* name  = luaL_checkstring(L, 2);
        const char* value = luaL_checkstring(L, 3);
        resp->set_header(name, value);
        return 0;
    }

    // resp:get_status() -> code
    static int lua_resp_get_status(lua_State* L)
    {
        AsyncHttpResponse* resp = check_response(L);
        lua_pushinteger(L, resp->status_code);
        return 1;
    }

    // resp:get_body() -> body string
    static int lua_resp_get_body(lua_State* L)
    {
        AsyncHttpResponse* resp = check_response(L);
        lua_pushstring(L, resp->body.c_str());
        return 1;
    }
};

#else // !HAS_LUA

// ---------------------------------------------------------------------------
// Stub implementation when Lua is not available
// ---------------------------------------------------------------------------
class LuaScriptHandler
{
public:
    LuaScriptHandler()
    {
        // Don't throw in constructor; throw when actually used
    }

    void load_script(const std::string& /*filename*/)
    {
        throw std::runtime_error(
            "LuaScriptHandler: Lua support not enabled. "
            "Define HAS_LUA=1 and link against Lua library.");
    }

    void load_string(const std::string& /*script*/)
    {
        throw std::runtime_error(
            "LuaScriptHandler: Lua support not enabled. "
            "Define HAS_LUA=1 and link against Lua library.");
    }

    AsyncHttpServer::Handler get_handler()
    {
        return [](const AsyncHttpRequest& /*req*/, AsyncHttpResponse& resp)
        {
            resp.set_status(501).set_body(
                "Lua scripting not enabled. Define HAS_LUA=1 and link Lua library.");
        };
    }
};

#endif // HAS_LUA

// ---------------------------------------------------------------------------
// 5x7 Bitmap font for CAPTCHA image generation
// Each character is 5 pixels wide, 7 pixels tall
// Bit 0x10 = leftmost pixel, 0x01 = rightmost pixel
// ---------------------------------------------------------------------------
#if HAS_LUA
const unsigned char LuaScriptHandler::CAPTCHA_FONT[128][7] = {
    // ASCII 0-31: Control characters (blank)
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 0
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 1
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 2
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 3
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 4
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 5
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 6
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 7
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 8
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 9
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 10
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 11
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 12
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 13
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 14
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 15
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 16
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 17
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 18
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 19
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 20
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 21
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 22
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 23
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 24
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 25
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 26
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 27
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 28
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 29
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 30
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 31
    // ASCII 32-47: Punctuation
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 32 (space)
    {0x04,0x04,0x04,0x04,0x00,0x04,0x00}, // 33 !
    {0x0A,0x0A,0x00,0x00,0x00,0x00,0x00}, // 34 "
    {0x0A,0x1F,0x0A,0x1F,0x0A,0x00,0x00}, // 35 #
    {0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04}, // 36 $
    {0x19,0x1A,0x04,0x0B,0x13,0x00,0x00}, // 37 %
    {0x08,0x14,0x08,0x15,0x12,0x0D,0x00}, // 38 &
    {0x04,0x04,0x00,0x00,0x00,0x00,0x00}, // 39 '
    {0x02,0x04,0x04,0x04,0x04,0x02,0x00}, // 40 (
    {0x08,0x04,0x04,0x04,0x04,0x08,0x00}, // 41 )
    {0x04,0x15,0x0E,0x15,0x04,0x00,0x00}, // 42 *
    {0x00,0x04,0x04,0x1F,0x04,0x04,0x00}, // 43 +
    {0x00,0x00,0x00,0x00,0x04,0x04,0x08}, // 44 ,
    {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, // 45 -
    {0x00,0x00,0x00,0x00,0x00,0x04,0x00}, // 46 .
    {0x01,0x02,0x04,0x08,0x10,0x00,0x00}, // 47 /
    // ASCII 48-57: Digits
    {0x0E,0x11,0x13,0x15,0x19,0x0E,0x00}, // 48 0
    {0x04,0x0C,0x04,0x04,0x04,0x0E,0x00}, // 49 1
    {0x0E,0x11,0x01,0x0E,0x10,0x1F,0x00}, // 50 2
    {0x0E,0x11,0x06,0x01,0x11,0x0E,0x00}, // 51 3
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x00}, // 52 4
    {0x1F,0x10,0x1E,0x01,0x11,0x0E,0x00}, // 53 5
    {0x06,0x08,0x1E,0x11,0x11,0x0E,0x00}, // 54 6
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x00}, // 55 7
    {0x0E,0x11,0x0E,0x11,0x11,0x0E,0x00}, // 56 8
    {0x0E,0x11,0x0F,0x01,0x02,0x0C,0x00}, // 57 9
    // ASCII 58-64: More punctuation
    {0x00,0x04,0x00,0x00,0x04,0x00,0x00}, // 58 :
    {0x00,0x04,0x00,0x00,0x04,0x08,0x00}, // 59 ;
    {0x02,0x04,0x08,0x04,0x02,0x00,0x00}, // 60 <
    {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00}, // 61 =
    {0x08,0x04,0x02,0x04,0x08,0x00,0x00}, // 62 >
    {0x0E,0x11,0x02,0x04,0x00,0x04,0x00}, // 63 ?
    {0x0E,0x11,0x17,0x15,0x17,0x10,0x0E}, // 64 @
    // ASCII 65-90: Uppercase letters
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x00}, // 65 A
    {0x1E,0x11,0x1E,0x11,0x11,0x1E,0x00}, // 66 B
    {0x0E,0x11,0x10,0x10,0x11,0x0E,0x00}, // 67 C
    {0x1E,0x11,0x11,0x11,0x11,0x1E,0x00}, // 68 D
    {0x1F,0x10,0x1E,0x10,0x10,0x1F,0x00}, // 69 E
    {0x1F,0x10,0x1E,0x10,0x10,0x10,0x00}, // 70 F
    {0x0E,0x11,0x10,0x17,0x11,0x0F,0x00}, // 71 G
    {0x11,0x11,0x1F,0x11,0x11,0x11,0x00}, // 72 H
    {0x0E,0x04,0x04,0x04,0x04,0x0E,0x00}, // 73 I
    {0x01,0x01,0x01,0x01,0x11,0x0E,0x00}, // 74 J
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, // 75 K
    {0x10,0x10,0x10,0x10,0x10,0x1F,0x00}, // 76 L
    {0x11,0x1B,0x15,0x11,0x11,0x11,0x00}, // 77 M
    {0x11,0x19,0x15,0x13,0x11,0x11,0x00}, // 78 N
    {0x0E,0x11,0x11,0x11,0x11,0x0E,0x00}, // 79 O
    {0x1E,0x11,0x1E,0x10,0x10,0x10,0x00}, // 80 P
    {0x0E,0x11,0x11,0x15,0x12,0x0D,0x00}, // 81 Q
    {0x1E,0x11,0x1E,0x14,0x12,0x11,0x00}, // 82 R
    {0x0E,0x10,0x0E,0x01,0x11,0x0E,0x00}, // 83 S
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x00}, // 84 T
    {0x11,0x11,0x11,0x11,0x11,0x0E,0x00}, // 85 U
    {0x11,0x11,0x11,0x0A,0x0A,0x04,0x00}, // 86 V
    {0x11,0x11,0x15,0x15,0x0A,0x0A,0x00}, // 87 W
    {0x11,0x0A,0x04,0x04,0x0A,0x11,0x00}, // 88 X
    {0x11,0x0A,0x04,0x04,0x04,0x04,0x00}, // 89 Y
    {0x1F,0x02,0x04,0x08,0x10,0x1F,0x00}, // 90 Z
    // ASCII 91-96: More punctuation
    {0x0E,0x08,0x08,0x08,0x08,0x0E,0x00}, // 91 [
    {0x10,0x08,0x04,0x02,0x01,0x00,0x00}, // 92 backslash
    {0x0E,0x02,0x02,0x02,0x02,0x0E,0x00}, // 93 ]
    {0x04,0x0A,0x11,0x00,0x00,0x00,0x00}, // 94 ^
    {0x00,0x00,0x00,0x00,0x00,0x1F,0x00}, // 95 _
    {0x08,0x04,0x00,0x00,0x00,0x00,0x00}, // 96 `
    // ASCII 97-122: Lowercase letters
    {0x00,0x0E,0x01,0x0F,0x11,0x0F,0x00}, // 97 a
    {0x10,0x10,0x1E,0x11,0x11,0x1E,0x00}, // 98 b
    {0x00,0x0E,0x10,0x10,0x11,0x0E,0x00}, // 99 c
    {0x01,0x01,0x0F,0x11,0x11,0x0F,0x00}, // 100 d
    {0x00,0x0E,0x11,0x1F,0x10,0x0E,0x00}, // 101 e
    {0x06,0x08,0x1C,0x08,0x08,0x08,0x00}, // 102 f
    {0x00,0x0F,0x11,0x0F,0x01,0x0E,0x00}, // 103 g
    {0x10,0x10,0x1E,0x11,0x11,0x11,0x00}, // 104 h
    {0x04,0x00,0x0C,0x04,0x04,0x0E,0x00}, // 105 i
    {0x02,0x00,0x06,0x02,0x12,0x0C,0x00}, // 106 j
    {0x10,0x10,0x12,0x1C,0x12,0x11,0x00}, // 107 k
    {0x0C,0x04,0x04,0x04,0x04,0x0E,0x00}, // 108 l
    {0x00,0x1A,0x15,0x15,0x11,0x11,0x00}, // 109 m
    {0x00,0x1E,0x11,0x11,0x11,0x11,0x00}, // 110 n
    {0x00,0x0E,0x11,0x11,0x11,0x0E,0x00}, // 111 o
    {0x00,0x1E,0x11,0x1E,0x10,0x10,0x00}, // 112 p
    {0x00,0x0F,0x11,0x0F,0x01,0x01,0x00}, // 113 q
    {0x00,0x16,0x19,0x10,0x10,0x10,0x00}, // 114 r
    {0x00,0x0E,0x10,0x0E,0x01,0x1E,0x00}, // 115 s
    {0x08,0x1C,0x08,0x08,0x09,0x06,0x00}, // 116 t
    {0x00,0x11,0x11,0x11,0x13,0x0D,0x00}, // 117 u
    {0x00,0x11,0x11,0x0A,0x0A,0x04,0x00}, // 118 v
    {0x00,0x11,0x11,0x15,0x15,0x0A,0x00}, // 119 w
    {0x00,0x11,0x0A,0x04,0x0A,0x11,0x00}, // 120 x
    {0x00,0x11,0x0A,0x04,0x08,0x10,0x00}, // 121 y
    {0x00,0x1F,0x02,0x04,0x08,0x1F,0x00}, // 122 z
    // ASCII 123-127: More punctuation
    {0x06,0x08,0x18,0x08,0x08,0x06,0x00}, // 123 {
    {0x04,0x04,0x04,0x04,0x04,0x04,0x00}, // 124 |
    {0x0C,0x02,0x03,0x02,0x02,0x0C,0x00}, // 125 }
    {0x00,0x08,0x15,0x02,0x00,0x00,0x00}, // 126 ~
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 127 DEL
};
#endif // HAS_LUA


