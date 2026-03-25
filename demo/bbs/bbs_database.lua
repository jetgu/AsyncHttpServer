-- bbs_database.lua
-- Database functions for BBS

local db_path = BBS_CONFIG.db_path

-- Initialize database tables
function db_init_bbs()
    local db, err = db_open(db_path)
    if not db then
        print("Failed to open database: " .. (err or "unknown"))
        return false
    end
    
    -- Users table
    db_execute(db, [[
        CREATE TABLE IF NOT EXISTS users (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            username TEXT UNIQUE NOT NULL,
            email TEXT UNIQUE NOT NULL,
            password_hash TEXT NOT NULL,
            password_salt TEXT NOT NULL,
            is_admin INTEGER DEFAULT 0,
            is_banned INTEGER DEFAULT 0,
            avatar TEXT,
            bio TEXT,
            created_at INTEGER,
            last_login INTEGER
        )
    ]])
    
    -- Categories table
    db_execute(db, [[
        CREATE TABLE IF NOT EXISTS categories (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            description TEXT,
            sort_order INTEGER DEFAULT 0,
            guest_visible INTEGER DEFAULT 1,
            created_at INTEGER
        )
    ]])
    
    -- Migration: add guest_visible column if missing
    db_execute(db, [[
        ALTER TABLE categories ADD COLUMN guest_visible INTEGER DEFAULT 1
    ]])
    
    -- Posts table
    db_execute(db, [[
        CREATE TABLE IF NOT EXISTS posts (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            category_id INTEGER,
            user_id INTEGER NOT NULL,
            title TEXT NOT NULL,
            content TEXT NOT NULL,
            is_pinned INTEGER DEFAULT 0,
            is_locked INTEGER DEFAULT 0,
            view_count INTEGER DEFAULT 0,
            created_at INTEGER,
            updated_at INTEGER,
            FOREIGN KEY (category_id) REFERENCES categories(id),
            FOREIGN KEY (user_id) REFERENCES users(id)
        )
    ]])
    
    -- Replies table
    db_execute(db, [[
        CREATE TABLE IF NOT EXISTS replies (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            post_id INTEGER NOT NULL,
            user_id INTEGER NOT NULL,
            content TEXT NOT NULL,
            created_at INTEGER,
            updated_at INTEGER,
            FOREIGN KEY (post_id) REFERENCES posts(id),
            FOREIGN KEY (user_id) REFERENCES users(id)
        )
    ]])
    
    -- Attachments table
    db_execute(db, [[
        CREATE TABLE IF NOT EXISTS attachments (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            post_id INTEGER,
            reply_id INTEGER,
            user_id INTEGER NOT NULL,
            filename TEXT NOT NULL,
            original_name TEXT NOT NULL,
            file_size INTEGER,
            mime_type TEXT,
            created_at INTEGER,
            FOREIGN KEY (post_id) REFERENCES posts(id),
            FOREIGN KEY (reply_id) REFERENCES replies(id),
            FOREIGN KEY (user_id) REFERENCES users(id)
        )
    ]])
    
    -- Create default admin if not exists
    local admin = db_get_user_by_username("admin")
    if not admin then
        local hash, salt = hash_password("admin123")
        db_execute(db, string.format([[
            INSERT INTO users (username, email, password_hash, password_salt, is_admin, created_at)
            VALUES ('admin', 'admin@localhost', '%s', '%s', 1, %d)
        ]], hash, salt, get_timestamp()))
        print("Default admin created: admin / admin123")
    end
    
    -- Create default category if none exists
    local cats = db_get_categories()
    if not cats or #cats == 0 then
        db_execute(db, string.format([[
            INSERT INTO categories (name, description, sort_order, created_at)
            VALUES ('General Discussion', 'General topics and discussions', 1, %d)
        ]], get_timestamp()))
        print("Default category created")
    end
    
    db_close(db)
    print("Database initialized: " .. db_path)
    return true
end

-- User functions
function db_create_user(username, email, password)
    local db, err = db_open(db_path)
    if not db then return nil, err end
    
    local hash, salt = hash_password(password)
    local sql = string.format([[
        INSERT INTO users (username, email, password_hash, password_salt, created_at)
        VALUES ('%s', '%s', '%s', '%s', %d)
    ]], db_escape(username), db_escape(email), hash, salt, get_timestamp())
    
    local ok, err = db_execute(db, sql)
    db_close(db)
    
    if ok then
        return db_get_user_by_username(username)
    end
    return nil, err
end

function db_get_user_by_id(id)
    local db = db_open(db_path)
    if not db then return nil end
    
    local rows = db_query(db, string.format([[
        SELECT *, 
               datetime(created_at, 'unixepoch', 'localtime') as created_at_fmt
        FROM users WHERE id = %d
    ]], tonumber(id)))
    db_close(db)
    
    return rows and rows[1]
end

function db_get_user_by_username(username)
    local db = db_open(db_path)
    if not db then return nil end
    
    local rows = db_query(db, string.format(
        "SELECT * FROM users WHERE username = '%s'", db_escape(username)
    ))
    db_close(db)
    
    return rows and rows[1]
end

function db_get_user_by_email(email)
    local db = db_open(db_path)
    if not db then return nil end
    
    local rows = db_query(db, string.format(
        "SELECT * FROM users WHERE email = '%s'", db_escape(email)
    ))
    db_close(db)
    
    return rows and rows[1]
end

function db_verify_user(username, password)
    local user = db_get_user_by_username(username)
    if not user then return nil, "User not found" end
    
    if user.is_banned == 1 then
        return nil, "Account is banned"
    end
    
    if verify_password(password, user.password_hash, user.password_salt) then
        -- Update last login
        local db = db_open(db_path)
        if db then
            db_execute(db, string.format(
                "UPDATE users SET last_login = %d WHERE id = %d",
                get_timestamp(), user.id
            ))
            db_close(db)
        end
        return user
    end
    
    return nil, "Invalid password"
end

function db_get_all_users(limit, offset)
    limit = limit or 50
    offset = offset or 0
    
    local db = db_open(db_path)
    if not db then return {} end
    
    local rows = db_query(db, string.format([[
        SELECT *, 
               datetime(created_at, 'unixepoch', 'localtime') as created_at_fmt,
               datetime(last_login, 'unixepoch', 'localtime') as last_login_fmt
        FROM users ORDER BY created_at DESC LIMIT %d OFFSET %d
    ]], limit, offset))
    db_close(db)
    
    return rows or {}
end

function db_update_user(id, fields)
    local db = db_open(db_path)
    if not db then return false end
    
    local updates = {}
    for k, v in pairs(fields) do
        if type(v) == "string" then
            table.insert(updates, k .. " = '" .. db_escape(v) .. "'")
        else
            table.insert(updates, k .. " = " .. tostring(v))
        end
    end
    
    local sql = "UPDATE users SET " .. table.concat(updates, ", ") .. " WHERE id = " .. id
    local ok = db_execute(db, sql)
    db_close(db)
    
    return ok
end

function db_ban_user(id, banned)
    return db_update_user(id, {is_banned = banned and 1 or 0})
end

function db_delete_user(id)
    local db = db_open(db_path)
    if not db then return false end
    
    local ok = db_execute(db, "DELETE FROM users WHERE id = " .. id)
    db_close(db)
    
    return ok
end

-- Category functions
function db_get_categories(guest_only)
    local db = db_open(db_path)
    if not db then return {} end
    
    local sql
    if guest_only then
        sql = "SELECT * FROM categories WHERE guest_visible = 1 ORDER BY sort_order ASC"
    else
        sql = "SELECT * FROM categories ORDER BY sort_order ASC"
    end
    
    local rows = db_query(db, sql)
    db_close(db)
    
    return rows or {}
end

function db_get_category(id)
    local db = db_open(db_path)
    if not db then return nil end
    
    local rows = db_query(db, "SELECT * FROM categories WHERE id = " .. tonumber(id))
    db_close(db)
    
    return rows and rows[1]
end

function db_toggle_category_guest_visible(id)
    local db = db_open(db_path)
    if not db then return false end
    
    local ok = db_execute(db, string.format(
        "UPDATE categories SET guest_visible = CASE WHEN guest_visible = 1 THEN 0 ELSE 1 END WHERE id = %d", id))
    db_close(db)
    
    return ok
end

function db_set_all_categories_guest_visible(visible)
    local db = db_open(db_path)
    if not db then return false end
    
    local ok = db_execute(db, string.format(
        "UPDATE categories SET guest_visible = %d", visible and 1 or 0))
    db_close(db)
    
    return ok
end

function db_create_category(name, description)
    local db = db_open(db_path)
    if not db then return nil end
    
    local sql = string.format([[
        INSERT INTO categories (name, description, created_at)
        VALUES ('%s', '%s', %d)
    ]], db_escape(name), db_escape(description or ""), get_timestamp())
    
    local ok = db_execute(db, sql)
    db_close(db)
    
    return ok
end

function db_update_category(id, name, description, guest_visible)
    local db = db_open(db_path)
    if not db then return false end
    
    local gv = ""
    if guest_visible ~= nil then
        gv = string.format(", guest_visible = %d", guest_visible and 1 or 0)
    end
    
    local sql = string.format([[
        UPDATE categories SET name = '%s', description = '%s'%s WHERE id = %d
    ]], db_escape(name), db_escape(description or ""), gv, id)
    
    local ok = db_execute(db, sql)
    db_close(db)
    
    return ok
end

function db_delete_category(id)
    local db = db_open(db_path)
    if not db then return false end
    
    local ok = db_execute(db, "DELETE FROM categories WHERE id = " .. id)
    db_close(db)
    
    return ok
end

-- Post functions
function db_create_post(user_id, category_id, title, content)
    local db = db_open(db_path)
    if not db then return nil end
    
    local now = get_timestamp()
    local sql = string.format([[
        INSERT INTO posts (user_id, category_id, title, content, created_at, updated_at)
        VALUES (%d, %s, '%s', '%s', %d, %d)
    ]], user_id, category_id or "NULL", db_escape(title), db_escape(content), now, now)
    
    local ok = db_execute(db, sql)
    if ok then
        local rows = db_query(db, "SELECT last_insert_rowid() as id")
        db_close(db)
        return rows and rows[1] and rows[1].id
    end
    
    db_close(db)
    return nil
end

function db_get_posts(category_id, limit, offset, guest_only)
limit = limit or BBS_CONFIG.posts_per_page
offset = offset or 0
    
local db = db_open(db_path)
if not db then return {} end

local conditions = {}
if category_id then
    conditions[#conditions + 1] = "p.category_id = " .. tonumber(category_id)
end
if guest_only then
    conditions[#conditions + 1] = "(p.category_id IS NULL OR p.category_id IN (SELECT id FROM categories WHERE guest_visible = 1))"
end

local where = ""
if #conditions > 0 then
    where = " WHERE " .. table.concat(conditions, " AND ")
end
    
local sql = string.format([[
    SELECT p.*, u.username, c.name as category_name,
           datetime(p.created_at, 'unixepoch', 'localtime') as created_at_fmt,
           (SELECT COUNT(*) FROM replies WHERE post_id = p.id) as reply_count
    FROM posts p
    LEFT JOIN users u ON p.user_id = u.id
    LEFT JOIN categories c ON p.category_id = c.id
    %s
    ORDER BY p.is_pinned DESC, p.created_at DESC
    LIMIT %d OFFSET %d
]], where, limit, offset)
    
local rows = db_query(db, sql)
    db_close(db)
    
    return rows or {}
end

function db_get_post(id)
    local db = db_open(db_path)
    if not db then return nil end
    
    local sql = string.format([[
        SELECT p.*, u.username, u.avatar, c.name as category_name,
               datetime(p.created_at, 'unixepoch', 'localtime') as created_at_fmt
        FROM posts p
        LEFT JOIN users u ON p.user_id = u.id
        LEFT JOIN categories c ON p.category_id = c.id
        WHERE p.id = %d
    ]], id)
    
    local rows = db_query(db, sql)
    
    -- Increment view count
    db_execute(db, "UPDATE posts SET view_count = view_count + 1 WHERE id = " .. id)
    
    db_close(db)
    
    return rows and rows[1]
end

function db_update_post(id, title, content, category_id)
    local db = db_open(db_path)
    if not db then return false end
    
    local sql = string.format([[
        UPDATE posts SET title = '%s', content = '%s', category_id = %s, updated_at = %d
        WHERE id = %d
    ]], db_escape(title), db_escape(content), category_id or "NULL", get_timestamp(), id)
    
    local ok = db_execute(db, sql)
    db_close(db)
    
    return ok
end

function db_delete_post(id)
    local db = db_open(db_path)
    if not db then return false end
    
    -- Delete replies first
    db_execute(db, "DELETE FROM replies WHERE post_id = " .. id)
    -- Delete attachments
    db_execute(db, "DELETE FROM attachments WHERE post_id = " .. id)
    -- Delete post
    local ok = db_execute(db, "DELETE FROM posts WHERE id = " .. id)
    db_close(db)
    
    return ok
end

function db_pin_post(id, pinned)
    local db = db_open(db_path)
    if not db then return false end
    
    local ok = db_execute(db, string.format(
        "UPDATE posts SET is_pinned = %d WHERE id = %d",
        pinned and 1 or 0, id
    ))
    db_close(db)
    
    return ok
end

function db_lock_post(id, locked)
    local db = db_open(db_path)
    if not db then return false end
    
    local ok = db_execute(db, string.format(
        "UPDATE posts SET is_locked = %d WHERE id = %d",
        locked and 1 or 0, id
    ))
    db_close(db)
    
    return ok
end

function db_get_post_count(category_id, guest_only)
    local db = db_open(db_path)
    if not db then return 0 end
    
    local conditions = {}
    if category_id then
        conditions[#conditions + 1] = "category_id = " .. tonumber(category_id)
    end
    if guest_only then
        conditions[#conditions + 1] = "(category_id IS NULL OR category_id IN (SELECT id FROM categories WHERE guest_visible = 1))"
    end
    
    local where = ""
    if #conditions > 0 then
        where = " WHERE " .. table.concat(conditions, " AND ")
    end
    
    local rows = db_query(db, "SELECT COUNT(*) as count FROM posts" .. where)
    db_close(db)
    
    return rows and rows[1] and rows[1].count or 0
end

-- Reply functions
function db_create_reply(post_id, user_id, content)
    local db = db_open(db_path)
    if not db then return nil end
    
    local now = get_timestamp()
    local sql = string.format([[
        INSERT INTO replies (post_id, user_id, content, created_at, updated_at)
        VALUES (%d, %d, '%s', %d, %d)
    ]], post_id, user_id, db_escape(content), now, now)
    
    local ok = db_execute(db, sql)
    if ok then
        local rows = db_query(db, "SELECT last_insert_rowid() as id")
        db_close(db)
        return rows and rows[1] and rows[1].id
    end
    
    db_close(db)
    return nil
end

function db_get_replies(post_id, limit, offset)
    limit = limit or 50
    offset = offset or 0
    
    local db = db_open(db_path)
    if not db then return {} end
    
    local sql = string.format([[
        SELECT r.*, u.username, u.avatar,
               datetime(r.created_at, 'unixepoch', 'localtime') as created_at_fmt
        FROM replies r
        LEFT JOIN users u ON r.user_id = u.id
        WHERE r.post_id = %d
        ORDER BY r.created_at ASC
        LIMIT %d OFFSET %d
    ]], post_id, limit, offset)
    
    local rows = db_query(db, sql)
    db_close(db)
    
    return rows or {}
end

function db_get_reply_count(post_id)
    local db = db_open(db_path)
    if not db then return 0 end
    
    local rows = db_query(db, string.format(
        "SELECT COUNT(*) as count FROM replies WHERE post_id = %d", post_id))
    db_close(db)
    
    return rows and rows[1] and rows[1].count or 0
end

function db_delete_reply(id)
    local db = db_open(db_path)
    if not db then return false end
    
    db_execute(db, "DELETE FROM attachments WHERE reply_id = " .. id)
    local ok = db_execute(db, "DELETE FROM replies WHERE id = " .. id)
    db_close(db)
    
    return ok
end

-- Attachment functions
function db_create_attachment(post_id, reply_id, user_id, filename, original_name, file_size, mime_type)
    local db = db_open(db_path)
    if not db then return nil end
    
    local sql = string.format([[
        INSERT INTO attachments (post_id, reply_id, user_id, filename, original_name, file_size, mime_type, created_at)
        VALUES (%s, %s, %d, '%s', '%s', %d, '%s', %d)
    ]], 
        post_id or "NULL", 
        reply_id or "NULL", 
        user_id, 
        db_escape(filename), 
        db_escape(original_name), 
        file_size or 0, 
        db_escape(mime_type or ""),
        get_timestamp()
    )
    
    local ok = db_execute(db, sql)
    db_close(db)
    
    return ok
end

function db_get_attachments(post_id, reply_id)
    local db = db_open(db_path)
    if not db then return {} end
    
    local where
    if post_id then
        where = "post_id = " .. post_id
    elseif reply_id then
        where = "reply_id = " .. reply_id
    else
        return {}
    end
    
    local rows = db_query(db, "SELECT * FROM attachments WHERE " .. where)
    db_close(db)
    
    return rows or {}
end

-- Get attachment by stored filename
function db_get_attachment_by_filename(filename)
    local db = db_open(db_path)
    if not db then return nil end
    
    local rows = db_query(db, string.format(
        "SELECT * FROM attachments WHERE filename = '%s' LIMIT 1",
        db_escape(filename)
    ))
    db_close(db)
    
    return rows and rows[1]
end

-- Stats functions
function db_get_stats()
    local db = db_open(db_path)
    if not db then return {} end
    
    local stats = {}
    
    local rows = db_query(db, "SELECT COUNT(*) as count FROM users")
    stats.user_count = rows and rows[1] and rows[1].count or 0
    
    rows = db_query(db, "SELECT COUNT(*) as count FROM posts")
    stats.post_count = rows and rows[1] and rows[1].count or 0
    
    rows = db_query(db, "SELECT COUNT(*) as count FROM replies")
    stats.reply_count = rows and rows[1] and rows[1].count or 0
    
    rows = db_query(db, "SELECT COUNT(*) as count FROM attachments")
    stats.attachment_count = rows and rows[1] and rows[1].count or 0
    
    db_close(db)
    
    return stats
end

print("bbs_database.lua loaded")
