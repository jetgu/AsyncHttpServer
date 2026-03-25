# AsyncHttpServer

A high-performance, asynchronous HTTP/HTTPS server built in **C++14** with **Lua scripting** for dynamic content. Lightweight, extensible, and self-contained — no external web frameworks required.

![C++14](https://img.shields.io/badge/C%2B%2B-14-blue?logo=cplusplus)
![Boost.Asio](https://img.shields.io/badge/Boost-Asio-green)
![Lua 5.4](https://img.shields.io/badge/Lua-5.4-blue?logo=lua)
![SQLite](https://img.shields.io/badge/SQLite-3-lightblue?logo=sqlite)
![OpenSSL](https://img.shields.io/badge/OpenSSL-TLS-red?logo=openssl)
![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux-lightgrey)

---

## Features

### Core Server
- **Fully Asynchronous I/O** — Built on Boost.Asio with a configurable thread pool for non-blocking request handling
- **HTTP/HTTPS Support** — Native TLS via OpenSSL; enable SSL by providing cert and key in config
- **Keep-Alive Connections** — Persistent connections with proper HTTP/1.1 support
- **JSON Configuration** — Single `server_config.json` for port, SSL, Lua scripts, web root, logging, and more

### Lua Scripting Engine
- **Dynamic Request Handling** — Write route handlers in Lua with full access to request/response objects
- **Template Engine** — `{{ var }}` for escaped output, `{! expr !}` for raw HTML, `{% lua_code %}` for logic
- **SQLite Integration** — Built-in SQLite bindings accessible from Lua for database operations
- **File I/O** — Read/write files, check existence, and get MIME types from Lua
- **Multipart Form Parsing** — Binary-safe parsing for file uploads with original filename preservation

### Logging
- **Colored Console Output** — Log levels with colors (Windows & ANSI terminal support)
- **File Logging** — Configurable log directory and filename
- **Log Rotation** — Automatic size-based rotation with configurable max file size and file count
- **Request Tracking** — Logs method, path, status code, response size, and processing duration

### Additional
- **CAPTCHA Image Generation** — Server-side BMP image generation in C++ with noise, random lines, and per-character color variation
- **WebSocket Client & Server** — Full WebSocket support for both plain `ws://` and secure `wss://` connections
- **HTTP Client** — Built-in HTTP/HTTPS client with sync and async APIs

---

## Architecture

```
Client Request
    ?
    ?
???????????????????????????????
?  AsyncHttpServer (C++)      ?
?  Boost.Asio Thread Pool     ?
???????????????????????????????
           ?
           ?
???????????????????????????????
?  LuaScriptHandler           ?
?  ?????????? ??????????????  ?
?  ? Routes ? ? Templates  ?  ?
?  ?????????? ??????????????  ?
?      ?            ?         ?
?  ?????????????????????????  ?
?  ?   SQLite Database     ?  ?
?  ?????????????????????????  ?
???????????????????????????????
           ?
           ?
     HTTP Response
```

---

## Project Structure

```
??? ConsoleApplication1.cpp            # Entry point — loads config and starts server
??? ConsoleApplication1/
?   ??? AsyncHttpServer.h              # Async HTTP/HTTPS server (Boost.Asio)
?   ??? HttpServer.h                   # Synchronous HTTP server
?   ??? HttpClient.h                   # HTTP/HTTPS client (sync & async)
?   ??? WebSocketServer.h              # WebSocket server
?   ??? WebSocketClient.h              # WebSocket client (ws/wss)
?   ??? LuaScriptHandler.h            # Lua scripting engine & template renderer
?   ??? ServerConfig.h                 # JSON configuration parser
?   ??? ServerLogger.h                 # Logger with rotation & colored output
?   ??? server_config.json             # Server configuration file
?   ??? bbs/                           # Lua application: BBS forum
?   ?   ??? bbs.lua                    # Main entry point & request dispatcher
?   ?   ??? bbs_config.lua             # Forum configuration
?   ?   ??? bbs_routes.lua             # Route handlers
?   ?   ??? bbs_database.lua           # Database operations
?   ?   ??? bbs_auth.lua               # Authentication & CAPTCHA
?   ?   ??? bbs_utils.lua              # Utility functions
?   ??? www/                           # Web root
?       ??? bbs/                       # Forum HTML templates
?           ??? home.html              # Landing page
?           ??? index.html             # Forum post list
?           ??? post.html              # Post detail & replies
?           ??? login.html             # Login with CAPTCHA
?           ??? register.html          # Registration with CAPTCHA
?           ??? new_post.html          # Create post (with emoji picker)
?           ??? edit_post.html         # Edit post
?           ??? profile.html           # User profile
?           ??? static/css/style.css   # Forum styles
?           ??? admin/                 # Admin panel templates
?               ??? dashboard.html
?               ??? users.html
?               ??? posts.html
?               ??? categories.html
??? lua-5.4.2_Win64_vc14_lib/          # Lua 5.4 library
??? sqlite3/                           # SQLite 3 source
```

---

## Configuration

All settings are in `server_config.json`:

```json
{
    "server": {
        "port": 8082,
        "worker_threads": 4,
        "max_body_size": 10485760,
        "request_timeout": 30
    },
    "ssl": {
        "enabled": false,
        "cert_file": "server.crt",
        "key_file": "server.key",
        "password": ""
    },
    "logging": {
        "enabled": true,
        "path": "./logs",
        "file": "server.log",
        "max_size": 10485760,
        "max_files": 5
    },
    "lua": {
        "script_path": "./bbs/bbs.lua",
        "web_root": "./www"
    },
    "static_files": {
        "enabled": true,
        "path": "/static"
    }
}
```

You can also pass a custom config path as a command-line argument:

```bash
./AsyncHttpServer my_config.json
```

---

## Building

### Prerequisites

- **C++14** compatible compiler (MSVC 2017+, GCC 5+, Clang 3.4+)
- **Boost** (Asio, System)
- **OpenSSL**
- **Lua 5.4** (included for Windows)
- **SQLite 3** (included as source)

### Windows (Visual Studio)

1. Open `ConsoleApplication1.sln` in Visual Studio 2017 or later
2. Ensure Boost and OpenSSL include/library paths are configured
3. Build in Release x64 mode
4. Copy `server_config.json`, `bbs/`, and `www/` folders to the output directory

### Linux

```bash
g++ -std=c++14 -O2 \
    ConsoleApplication1.cpp \
    sqlite3/sqlite3.c \
    -I/path/to/boost/include \
    -I/path/to/lua/include \
    -I/path/to/openssl/include \
    -lboost_system -lpthread -lssl -lcrypto -llua5.4 -ldl \
    -o AsyncHttpServer
```

---

## Running

```bash
# Start with default config (server_config.json)
./AsyncHttpServer

# Start with custom config
./AsyncHttpServer /path/to/config.json
```

The server will display:

```
=== Server Configuration ===
Port: 8082
Worker threads: 4
SSL: disabled
Lua script: ./bbs/bbs.lua
Web root: ./www
Logging: enabled
  Log path: ./logs
  Log file: server.log
  Max size: 10 MB
  Max files: 5
============================

=== Server Started ===
Listening on: http://localhost:8082
=======================

Press Enter to stop the server...
```

---

## Log Output

```
[2024-03-23 11:06:12.501] [INFO ] Server started on port 8082 with 4 worker threads
[2024-03-23 11:06:12.526] [INFO ] 127.0.0.1 - GET /bbs
[2024-03-23 11:06:12.527] [INFO ] 127.0.0.1 - Response: 200 (15.2 KB) [1ms]
[2024-03-23 11:06:12.530] [INFO ] 127.0.0.1 - GET /bbs/static/css/style.css
[2024-03-23 11:06:12.530] [INFO ] 127.0.0.1 - Response: 200 (3.1 KB) [0ms]
```

Logs rotate automatically when the file exceeds the configured `max_size`.

---

## Demo Application: BBS Forum

The included BBS (Bulletin Board System) demonstrates the full capabilities of the server:

| Feature | Description |
|---------|-------------|
| **User System** | Registration, login, profiles with password hashing |
| **Posts & Replies** | Create, edit, delete posts with threaded replies |
| **File Attachments** | Upload images, documents, archives with preview |
| **Emoji Picker** | 7-category emoji selector for posts and replies |
| **Quote & Reply** | Quote any post or reply with BBCode-style markup |
| **CAPTCHA** | Server-generated BMP image CAPTCHA on login/register |
| **Admin Panel** | User management, post moderation, category management |
| **Categories** | Organize posts into categories |

### Screenshots

Visit `http://localhost:8082/` after starting the server to see the landing page, then navigate to `http://localhost:8082/bbs` for the forum.

---

## Template Engine

Templates support three tag types:

| Syntax | Description | Example |
|--------|-------------|---------|
| `{{ expr }}` | HTML-escaped output | `{{ user.name }}` |
| `{! expr !}` | Raw HTML output | `{! formatted_html !}` |
| `{% code %}` | Lua code block | `{% if user then %}...{% end %}` |

### Example Template

```html
<h1>{{ title }}</h1>
{% if posts and #posts > 0 then %}
    {% for i, post in ipairs(posts) do %}
    <div class="post">
        <h2>{{ post.title }}</h2>
        <p>{! post.content !}</p>
    </div>
    {% end %}
{% else %}
    <p>No posts yet.</p>
{% end %}
```

---

## Lua API Reference

### Request Object

```lua
req.method          -- "GET", "POST", etc.
req.path            -- "/bbs/post"
req.query           -- "id=5&page=2"
req.body            -- Request body string
req.headers         -- Table of headers
req.remote_address  -- Client IP
```

### Response Object

```lua
resp:set_status(200)
resp:set_status(404, "Not Found")
resp:set_header("Content-Type", "application/json")
resp:set_body("Hello World")
resp:set_body(json_string, "application/json")
```

### Built-in Functions

```lua
-- File I/O
read_file(path)                     --> content or nil
write_file(path, data)              --> boolean
file_exists(path)                   --> boolean
get_mime_type(path)                 --> "text/html", etc.

-- Database
db = db_open("database.db")
db_execute(db, sql)
rows = db_query(db, sql)
db_close(db)

-- Templates
render_template(template_str, vars) --> rendered HTML

-- CAPTCHA
generate_captcha_image(text)        --> BMP image binary data
```

---

## Header-Only Libraries

All C++ components are header-only for easy integration:

| Header | Description |
|--------|-------------|
| `AsyncHttpServer.h` | Async HTTP/HTTPS server with thread pool |
| `HttpServer.h` | Simple synchronous HTTP server |
| `HttpClient.h` | HTTP/HTTPS client (sync & async, GET/POST/DELETE) |
| `WebSocketServer.h` | WebSocket server with connection management |
| `WebSocketClient.h` | WebSocket client (ws:// and wss://) |
| `LuaScriptHandler.h` | Lua scripting engine, template renderer, CAPTCHA generator |
| `ServerConfig.h` | JSON configuration file parser |
| `ServerLogger.h` | Logger with colored output and file rotation |

---

## License

This project is open source. Feel free to use, modify, and distribute.

---

## Contributing

Contributions are welcome! Please open an issue or submit a pull request.
