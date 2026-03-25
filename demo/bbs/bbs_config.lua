-- bbs_config.lua
-- bbs_config.lua
-- Configuration for BBS system

BBS_CONFIG = {
    db_path = "bbs.db",
    upload_dir = "uploads",
    max_file_size = 10 * 1024 * 1024,  -- 10MB
    allowed_extensions = {"jpg", "jpeg", "png", "gif", "pdf", "zip", "txt", "js", "css", "html", "json", "xml", "md", "doc", "docx", "xls", "xlsx", "ppt", "pptx", "mp3", "mp4", "wav", "avi", "mov"},
    posts_per_page = 10,
    replies_per_page = 10,
    guest_access = true,  -- Allow guests to view the forum (per-category control via admin)
    session_timeout = 3600 * 24,  -- 24 hours
    captcha_timeout = 300,  -- 5 minutes
    admin_username = "admin",
    site_name = "Lua BBS"
}

-- Session storage (in-memory, resets on server restart)
SESSIONS = {}

-- CAPTCHA storage (in-memory)
CAPTCHAS = {}

-- Create upload directory if it doesn't exist
os.execute("mkdir " .. BBS_CONFIG.upload_dir .. " 2>nul")

print("bbs_config.lua loaded")
