-- bbs_utils.lua
-- Utility functions for BBS

local web_root = WEB_ROOT or "./www"

-- JSON encoding
function escape_json(s)
    if not s then return "" end
    s = tostring(s)
    s = s:gsub("\\", "\\\\")
    s = s:gsub('"', '\\"')
    s = s:gsub("\n", "\\n")
    s = s:gsub("\r", "\\r")
    s = s:gsub("\t", "\\t")
    return s
end

function json_encode(tbl)
    if type(tbl) ~= "table" then
        if type(tbl) == "string" then
            return '"' .. escape_json(tbl) .. '"'
        elseif type(tbl) == "number" then
            return tostring(tbl)
        elseif type(tbl) == "boolean" then
            return tbl and "true" or "false"
        else
            return "null"
        end
    end
    
    -- Check if array or object
    local is_array = #tbl > 0 or next(tbl) == nil
    for k, _ in pairs(tbl) do
        if type(k) ~= "number" then
            is_array = false
            break
        end
    end
    
    local parts = {}
    if is_array then
        for i, v in ipairs(tbl) do
            table.insert(parts, json_encode(v))
        end
        return "[" .. table.concat(parts, ",") .. "]"
    else
        for k, v in pairs(tbl) do
            table.insert(parts, '"' .. escape_json(tostring(k)) .. '":' .. json_encode(v))
        end
        return "{" .. table.concat(parts, ",") .. "}"
    end
end

-- Parse query string
function parse_query(query)
    local params = {}
    if query and query ~= "" then
        for pair in query:gmatch("[^&]+") do
            local key, value = pair:match("([^=]+)=?(.*)")
            if key then
                key = url_decode(key)
                value = url_decode(value or "")
                params[key] = value
            end
        end
    end
    return params
end

-- URL decode
function url_decode(str)
    str = str:gsub("+", " ")
    str = str:gsub("%%(%x%x)", function(h)
        return string.char(tonumber(h, 16))
    end)
    return str
end

-- URL encode
function url_encode(str)
    str = str:gsub("([^%w%-_.~])", function(c)
        return string.format("%%%02X", string.byte(c))
    end)
    return str
end

-- Parse JSON (simple)
function parse_json(str)
    local result = {}
    for key, value in str:gmatch('"([^"]+)"%s*:%s*"([^"]*)"') do
        result[key] = value
    end
    -- Also parse numbers and booleans
    for key, value in str:gmatch('"([^"]+)"%s*:%s*(%d+)') do
        result[key] = tonumber(value)
    end
    for key, value in str:gmatch('"([^"]+)"%s*:%s*(true)') do
        result[key] = true
    end
    for key, value in str:gmatch('"([^"]+)"%s*:%s*(false)') do
        result[key] = false
    end
    return result
end

-- Parse form data (application/x-www-form-urlencoded)
function parse_form(body)
    return parse_query(body)
end

-- Parse multipart form data (for file uploads)
function parse_multipart(body, boundary)
    local parts = {}
    
    if not boundary or not body then
        return parts
    end
    
    -- The boundary in the body is prefixed with --
    local delimiter = "--" .. boundary
    local end_delimiter = delimiter .. "--"
    
    -- Split body by delimiter
    local start_pos = 1
    while true do
        local delim_start, delim_end = body:find(delimiter, start_pos, true)
        if not delim_start then break end
        
        -- Check if this is the end delimiter
        if body:sub(delim_start, delim_start + #end_delimiter - 1) == end_delimiter then
            break
        end
        
        -- Find the next delimiter
        local next_delim_start = body:find(delimiter, delim_end + 1, true)
        if not next_delim_start then break end
        
        -- Extract the part between delimiters
        local part = body:sub(delim_end + 1, next_delim_start - 1)
        
        -- Remove leading \r\n
        if part:sub(1, 2) == "\r\n" then
            part = part:sub(3)
        end
        
        -- Remove trailing \r\n
        if part:sub(-2) == "\r\n" then
            part = part:sub(1, -3)
        end
        
        -- Find the header/body separator
        local header_end = part:find("\r\n\r\n", 1, true)
        if header_end then
            local headers = part:sub(1, header_end - 1)
            local content = part:sub(header_end + 4)
            
            -- Parse headers
            local name = headers:match('name="([^"]+)"')
            local filename = headers:match('filename="([^"]+)"')
            local content_type = headers:match("[Cc]ontent%-[Tt]ype:%s*([^\r\n]+)")
            
            if name then
                if filename and filename ~= "" then
                    parts[name] = {
                        filename = filename,
                        content_type = content_type or "application/octet-stream",
                        data = content
                    }
                else
                    parts[name] = content
                end
            end
        end
        
        start_pos = next_delim_start
    end
    
    return parts
end

-- Get boundary from content-type header
function get_boundary(content_type)
    local boundary = content_type:match("boundary=([^;%s]+)")
    if boundary then
        -- Remove quotes if present
        boundary = boundary:gsub('^"', ''):gsub('"$', '')
    end
    return boundary
end

-- Save uploaded file
function save_uploaded_file(file_data, original_filename)
    if not file_data or not original_filename or original_filename == "" then
        return nil, "No file data"
    end
    
    -- Check file extension
    if not is_allowed_extension(original_filename) then
        return nil, "File type not allowed"
    end
    
    -- Check file size
    if #file_data > BBS_CONFIG.max_file_size then
        return nil, "File too large"
    end
    
    -- Generate unique filename
    local ext = get_extension(original_filename) or "bin"
    local new_filename = random_string(16) .. "." .. ext
    local filepath = BBS_CONFIG.upload_dir .. "/" .. new_filename
    
    -- Create upload directory if needed (would need mkdir function)
    -- For now, assume directory exists
    
    -- Save file
    local ok, err = write_file(filepath, file_data)
    if ok then
        return new_filename
    else
        return nil, err or "Failed to write file"
    end
end

-- HTML escape
function html_escape(s)
    if not s then return "" end
    s = tostring(s)
    s = s:gsub("&", "&amp;")
    s = s:gsub("<", "&lt;")
    s = s:gsub(">", "&gt;")
    s = s:gsub('"', "&quot;")
    s = s:gsub("'", "&#39;")
    return s
end

-- Parse BBCode-style quotes and convert to HTML
-- [quote=username]content[/quote] -> HTML quote box
function parse_quotes(content)
    if not content then return "" end
    
    -- Convert newlines to <br> first (but preserve them in processing)
    local result = content
    
    -- Parse [quote=author]...[/quote] tags
    result = result:gsub("%[quote=([^%]]+)%](.-)%[/quote%]", function(author, quote_content)
        -- Escape the content
        local escaped_author = html_escape(author)
        local escaped_content = html_escape(quote_content)
        -- Convert newlines in quote to <br>
        escaped_content = escaped_content:gsub("\n", "<br>")
        
        return '<div class="quote-box">' ..
               '<div class="quote-author"><i class="bi bi-quote"></i> ' .. escaped_author .. ' wrote:</div>' ..
               '<div class="quote-content">' .. escaped_content .. '</div>' ..
               '</div>'
    end)
    
    -- Convert remaining newlines to <br>
    result = result:gsub("\n", "<br>")
    
    return result
end

-- Format content for display (parse quotes, convert newlines)
function format_content(content)
    if not content then return "" end
    return parse_quotes(content)
end

-- Generate random string
function random_string(length)
    local chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
    local result = ""
    for i = 1, length do
        local idx = math.random(1, #chars)
        result = result .. chars:sub(idx, idx)
    end
    return result
end

-- Simple hash function (for demo purposes - use proper crypto in production)
function simple_hash(str)
    local hash = 5381
    for i = 1, #str do
        hash = ((hash * 33) + string.byte(str, i)) % 0xFFFFFFFF
    end
    return string.format("%08x", hash)
end

-- Get current timestamp
function get_timestamp()
    return os.time()
end

-- Format timestamp
function format_time(timestamp)
    return os.date("%Y-%m-%d %H:%M:%S", timestamp)
end

-- Get file extension
function get_extension(filename)
    return filename:match("%.([^%.]+)$")
end

-- Check if extension is allowed
function is_allowed_extension(filename)
    local ext = (get_extension(filename) or ""):lower()
    for _, allowed in ipairs(BBS_CONFIG.allowed_extensions) do
        if ext == allowed:lower() then
            return true
        end
    end
    return false
end

-- Send JSON response
function send_json(resp, data, status)
    if status then resp:set_status(status) end
    resp:set_body(json_encode(data), "application/json")
end

-- Send error response
function send_error(resp, message, status)
    resp:set_status(status or 400)
    send_json(resp, {success = false, error = message})
end

-- Send success response
function send_success(resp, message, data)
    local result = {success = true, message = message}
    if data then
        for k, v in pairs(data) do result[k] = v end
    end
    send_json(resp, result)
end

-- Serve static file
function serve_file(resp, filepath)
    local content = read_file(filepath)
    if content then
        resp:set_body(content, get_mime_type(filepath))
        return true
    end
    return false
end

-- Render template
function render_html(resp, template_file, vars)
    local filepath = web_root .. "/bbs/" .. template_file
    local template = read_file(filepath)
    
    if not template then
        resp:set_status(500)
        resp:set_body("Template not found: " .. template_file)
        return false
    end
    
    local html, err = render_template(template, vars or {})
    
    if html then
        resp:set_body(html, "text/html")
        return true
    else
        resp:set_status(500)
        resp:set_body("Template error: " .. (err or "unknown"))
        return false
    end
end

-- Serve 404
function serve_404(resp, path)
    resp:set_status(404, "Not Found")
    render_html(resp, "404.html", {path = path})
end

-- Redirect
function redirect(resp, url)
    resp:set_status(302, "Found")
    resp:set_header("Location", url)
    resp:set_body("")
end

-- Get cookie value
function get_cookie(req, name)
    local cookies = req.headers["cookie"] or ""
    local value = cookies:match(name .. "=([^;]+)")
    return value
end

-- Set cookie
function set_cookie(resp, name, value, max_age)
    max_age = max_age or BBS_CONFIG.session_timeout
    local cookie = name .. "=" .. value .. "; Path=/; Max-Age=" .. max_age .. "; HttpOnly"
    resp:set_header("Set-Cookie", cookie)
end

-- Clear cookie
function clear_cookie(resp, name)
    set_cookie(resp, name, "", 0)
end

-- Build pagination page list with ellipsis
-- Returns a table of {page=N} or {ellipsis=true}
-- Example for page=5, total=20, wing=2:
--   1 ... 3 4 [5] 6 7 ... 20
function build_page_range(current, total, wing)
    wing = wing or 2
    if total <= 1 then return {} end
    
    local pages = {}
    local added = {}
    
    local function add_page(p)
        if p >= 1 and p <= total and not added[p] then
            added[p] = true
            pages[#pages + 1] = {page = p}
        end
    end
    
    -- Always include first page
    add_page(1)
    
    -- Pages around current
    for p = current - wing, current + wing do
        add_page(p)
    end
    
    -- Always include last page
    add_page(total)
    
    -- Sort by page number
    table.sort(pages, function(a, b) return a.page < b.page end)
    
    -- Insert ellipsis markers where there are gaps
    local result = {}
    for i, item in ipairs(pages) do
        if i > 1 and item.page > pages[i - 1].page + 1 then
            result[#result + 1] = {ellipsis = true}
        end
        result[#result + 1] = item
    end
    
    return result
end

print("bbs_utils.lua loaded")
