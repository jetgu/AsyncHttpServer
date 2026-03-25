-- bbs.lua
-- Full-featured BBS (Bulletin Board System) with Lua scripting
-- Features: User registration, login, posts, file uploads, admin panel

-- Initialize random seed
math.randomseed(os.time())

-- Get the directory of this script
local script_dir = "bbs/"

dofile(script_dir .. "bbs_config.lua")
dofile(script_dir .. "bbs_utils.lua")
dofile(script_dir .. "bbs_auth.lua")
dofile(script_dir .. "bbs_database.lua")
dofile(script_dir .. "bbs_routes.lua")

-- Initialize database
db_init_bbs()

print("BBS System loaded!")
print("Web root: " .. (WEB_ROOT or "./www"))
