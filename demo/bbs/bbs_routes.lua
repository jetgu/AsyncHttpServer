-- bbs_routes.lua
-- Route handlers for BBS

local web_root = WEB_ROOT or "./www"
local routes = {}

--------------------------------------------------------------------------------
-- Landing Page
--------------------------------------------------------------------------------

routes["/"] = function(req, resp)
    render_html(resp, "home.html", {
        site_name = BBS_CONFIG.site_name
    })
end

--------------------------------------------------------------------------------
-- Public Pages
--------------------------------------------------------------------------------

-- Home page - list posts
routes["/bbs"] = function(req, resp)
    local user = get_current_user(req)
    local params = parse_query(req.query)
    local category_id = tonumber(params.category)
    local page = tonumber(params.page) or 1
    local offset = (page - 1) * BBS_CONFIG.posts_per_page
    local is_guest = not user
    
    -- Guests can only see guest-visible categories
    local guest_only = is_guest and BBS_CONFIG.guest_access
    
    -- If guest access is disabled entirely, redirect to login
    if is_guest and not BBS_CONFIG.guest_access then
        redirect(resp, "/bbs/login")
        return
    end
    
    -- If guest is trying to view a non-guest-visible category, deny
    if is_guest and category_id then
        local cat = db_get_category(category_id)
        if cat and cat.guest_visible == 0 then
            redirect(resp, "/bbs/login")
            return
        end
    end
    
    local posts = db_get_posts(category_id, BBS_CONFIG.posts_per_page, offset, guest_only)
    local categories = db_get_categories(guest_only)
    local total = db_get_post_count(category_id, guest_only)
    local total_pages = math.ceil(total / BBS_CONFIG.posts_per_page)
    
    render_html(resp, "index.html", {
        user = user,
        posts = posts,
        categories = categories,
        current_category = category_id,
        page = page,
        total_pages = total_pages,
        site_name = BBS_CONFIG.site_name
    })
end

routes["/bbs/"] = routes["/bbs"]

-- View single post
routes["/bbs/post"] = function(req, resp)
local params = parse_query(req.query)
local id = tonumber(params.id)
    
if not id then
    serve_404(resp, req.path)
    return
end
    
local post = db_get_post(id)
if not post then
    serve_404(resp, req.path)
    return
end
    
local user = get_current_user(req)
    
-- Check guest access for this post's category
if not user and post.category_id then
    local cat = db_get_category(post.category_id)
    if cat and cat.guest_visible == 0 then
        redirect(resp, "/bbs/login")
        return
    end
end
    
    -- Reply pagination
    local reply_total = db_get_reply_count(id)
    local reply_total_pages = math.max(1, math.ceil(reply_total / BBS_CONFIG.replies_per_page))
    
    local reply_page
    if params.rpage == "last" then
        reply_page = reply_total_pages
    else
        reply_page = tonumber(params.rpage) or 1
    end
    
    -- Clamp page
    if reply_page < 1 then reply_page = 1 end
    if reply_page > reply_total_pages then reply_page = reply_total_pages end
    local reply_offset = (reply_page - 1) * BBS_CONFIG.replies_per_page
    
    local replies = db_get_replies(id, BBS_CONFIG.replies_per_page, reply_offset)
    local attachments = db_get_attachments(id, nil)
    
    -- Get attachments for replies
    for _, reply in ipairs(replies) do
        reply.attachments = db_get_attachments(nil, reply.id)
    end
    
    render_html(resp, "post.html", {
        user = user,
        post = post,
        replies = replies,
        attachments = attachments,
        reply_page = reply_page,
        reply_total_pages = reply_total_pages,
        reply_total = reply_total,
        site_name = BBS_CONFIG.site_name
    })
end

--------------------------------------------------------------------------------
-- Authentication
--------------------------------------------------------------------------------

-- CAPTCHA image endpoint
routes["/bbs/captcha"] = function(req, resp)
    local params = parse_query(req.query)
    local captcha_id = params.id
    
    if not captcha_id then
        resp:set_status(400)
        resp:set_body("Missing captcha ID")
        return
    end
    
    -- Get captcha data
    local captcha = CAPTCHAS[captcha_id]
    if not captcha or not captcha.question then
        resp:set_status(404)
        resp:set_body("CAPTCHA not found")
        return
    end
    
    -- Generate image
    local image_data = generate_captcha_image(captcha.question)
    
    -- Set headers for image
    resp:set_header("Content-Type", "image/bmp")
    resp:set_header("Cache-Control", "no-cache, no-store, must-revalidate")
    resp:set_header("Pragma", "no-cache")
    resp:set_header("Expires", "0")
    resp:set_body(image_data)
end

-- Login page
routes["/bbs/login"] = function(req, resp)
    if req.method == "GET" then
        local user = get_current_user(req)
        if user then
            redirect(resp, "/bbs")
            return
        end
        -- Generate CAPTCHA
        local captcha_id, captcha_question = generate_captcha()
        render_html(resp, "login.html", {
            site_name = BBS_CONFIG.site_name,
            captcha_id = captcha_id,
            captcha_question = captcha_question
        })
    else
        local data = parse_form(req.body)
        local username = data.username or ""
        local password = data.password or ""
        local captcha_id = data.captcha_id or ""
        local captcha_answer = data.captcha_answer or ""
        
        -- Verify CAPTCHA first
        local captcha_ok, captcha_err = verify_captcha(captcha_id, captcha_answer)
        if not captcha_ok then
            local new_captcha_id, new_captcha_question = generate_captcha()
            render_html(resp, "login.html", {
                error_message = captcha_err,
                username = username,
                site_name = BBS_CONFIG.site_name,
                captcha_id = new_captcha_id,
                captcha_question = new_captcha_question
            })
            return
        end
        
        local user, err = db_verify_user(username, password)
        if user then
            local session_id = create_session(user.id, user.username, user.is_admin == 1)
            set_cookie(resp, "session", session_id)
            redirect(resp, "/bbs")
        else
            local new_captcha_id, new_captcha_question = generate_captcha()
            render_html(resp, "login.html", {
                error_message = err or "Invalid credentials",
                username = username,
                site_name = BBS_CONFIG.site_name,
                captcha_id = new_captcha_id,
                captcha_question = new_captcha_question
            })
        end
    end
end

-- Register page
routes["/bbs/register"] = function(req, resp)
    if req.method == "GET" then
        -- Generate CAPTCHA
        local captcha_id, captcha_question = generate_captcha()
        render_html(resp, "register.html", {
            site_name = BBS_CONFIG.site_name,
            captcha_id = captcha_id,
            captcha_question = captcha_question
        })
    else
        local data = parse_form(req.body)
        local username = data.username or ""
        local email = data.email or ""
        local password = data.password or ""
        local confirm = data.confirm or ""
        local captcha_id = data.captcha_id or ""
        local captcha_answer = data.captcha_answer or ""
        
        -- Validation
        local errors = {}
        
        -- Verify CAPTCHA first
        local captcha_ok, captcha_err = verify_captcha(captcha_id, captcha_answer)
        if not captcha_ok then
            table.insert(errors, captcha_err)
        end
        
        if #username < 3 then
            table.insert(errors, "Username must be at least 3 characters")
        end
        if not email:match("^[^@]+@[^@]+%.[^@]+$") then
            table.insert(errors, "Invalid email address")
        end
        if #password < 6 then
            table.insert(errors, "Password must be at least 6 characters")
        end
        if password ~= confirm then
            table.insert(errors, "Passwords do not match")
        end
        if db_get_user_by_username(username) then
            table.insert(errors, "Username already taken")
        end
        if db_get_user_by_email(email) then
            table.insert(errors, "Email already registered")
        end
        
        if #errors > 0 then
            local new_captcha_id, new_captcha_question = generate_captcha()
            render_html(resp, "register.html", {
                errors = errors,
                username = username,
                email = email,
                site_name = BBS_CONFIG.site_name,
                captcha_id = new_captcha_id,
                captcha_question = new_captcha_question
            })
        else
            local user, err = db_create_user(username, email, password)
            if user then
                local session_id = create_session(user.id, user.username, false)
                set_cookie(resp, "session", session_id)
                redirect(resp, "/bbs")
            else
                local new_captcha_id, new_captcha_question = generate_captcha()
                render_html(resp, "register.html", {
                    errors = {err or "Registration failed"},
                    username = username,
                    email = email,
                    site_name = BBS_CONFIG.site_name,
                    captcha_id = new_captcha_id,
                    captcha_question = new_captcha_question
                })
            end
        end
    end
end

-- Logout
routes["/bbs/logout"] = function(req, resp)
    local session_id = get_cookie(req, "session")
    destroy_session(session_id)
    clear_cookie(resp, "session")
    redirect(resp, "/bbs")
end

--------------------------------------------------------------------------------
-- Post Management
--------------------------------------------------------------------------------

-- New post page
routes["/bbs/new-post"] = function(req, resp)
    if not require_login(req, resp) then return end
    
    local user = get_current_user(req)
    local categories = db_get_categories()
    
    if req.method == "GET" then
        render_html(resp, "new_post.html", {
            user = user,
            categories = categories,
            site_name = BBS_CONFIG.site_name
        })
    else
        local content_type = req.headers["content-type"] or ""
        local data
        
        -- Parse form data based on content type
        if content_type:find("multipart/form%-data") then
            local boundary = get_boundary(content_type)
            if boundary then
                data = parse_multipart(req.body, boundary)
                print("Parsed multipart form, fields: " .. (data.title and "title " or "") .. (data.content and "content " or "") .. (data.attachment and "attachment" or ""))
            else
                data = {}
                print("Failed to get boundary from: " .. content_type)
            end
        else
            data = parse_form(req.body)
        end
        
        local title = data.title or ""
        local content = data.content or ""
        local category_id = tonumber(data.category)
        
        -- Debug output
        print("Creating post: title=" .. title .. ", content length=" .. #content)
        
        if not category_id then
            render_html(resp, "new_post.html", {
                user = user,
                categories = categories,
                error_message = "Please select a category",
                title = title,
                content = content,
                site_name = BBS_CONFIG.site_name
            })
            return
        end
        
        if #title < 3 then
            render_html(resp, "new_post.html", {
                user = user,
                categories = categories,
                error_message = "Title must be at least 3 characters",
                title = title,
                content = content,
                selected_category = category_id,
                site_name = BBS_CONFIG.site_name
            })
            return
        end
        
        if #content < 10 then
            render_html(resp, "new_post.html", {
                user = user,
                categories = categories,
                error_message = "Content must be at least 10 characters",
                title = title,
                content = content,
                selected_category = category_id,
                site_name = BBS_CONFIG.site_name
            })
            return
        end
        
        local post_id = db_create_post(user.user_id, category_id, title, content)
        print("Post created with ID: " .. tostring(post_id))
        
        if post_id then
            -- Handle file upload
            if data.attachment and type(data.attachment) == "table" and data.attachment.filename and data.attachment.filename ~= "" then
                local file = data.attachment
                print("Processing attachment: " .. file.filename .. ", size: " .. #file.data)
                
                local saved_filename, err = save_uploaded_file(file.data, file.filename)
                if saved_filename then
                    db_create_attachment(post_id, nil, user.user_id, saved_filename, file.filename, #file.data, file.content_type)
                    print("Attachment saved: " .. saved_filename)
                else
                    print("Failed to save attachment: " .. (err or "unknown error"))
                end
            end
            
            redirect(resp, "/bbs/post?id=" .. post_id)
        else
            render_html(resp, "new_post.html", {
                user = user,
                categories = categories,
                error_message = "Failed to create post. Please try again.",
                title = title,
                content = content,
                selected_category = category_id,
                site_name = BBS_CONFIG.site_name
            })
        end
    end
end

-- Edit post
routes["/bbs/edit-post"] = function(req, resp)
    if not require_login(req, resp) then return end
    
    local user = get_current_user(req)
    local params = parse_query(req.query)
    local id = tonumber(params.id)
    
    if not id then
        serve_404(resp, req.path)
        return
    end
    
    local post = db_get_post(id)
    if not post then
        serve_404(resp, req.path)
        return
    end
    
    -- Check permission
    if post.user_id ~= user.user_id and not user.is_admin then
        resp:set_status(403)
        render_html(resp, "error.html", {
            error_title = "Access Denied",
            error_message = "You can only edit your own posts."
        })
        return
    end
    
    local categories = db_get_categories()
    
    if req.method == "GET" then
        render_html(resp, "edit_post.html", {
            user = user,
            post = post,
            categories = categories,
            site_name = BBS_CONFIG.site_name
        })
    else
        local data = parse_form(req.body)
        local title = data.title or ""
        local content = data.content or ""
        local category_id = tonumber(data.category)
        
        if #title < 3 or #content < 10 then
            render_html(resp, "edit_post.html", {
                user = user,
                post = {id = id, title = title, content = content, category_id = category_id},
                categories = categories,
                error_message = "Title must be at least 3 characters and content at least 10 characters",
                site_name = BBS_CONFIG.site_name
            })
            return
        end
        
        if db_update_post(id, title, content, category_id) then
            redirect(resp, "/bbs/post?id=" .. id)
        else
            render_html(resp, "edit_post.html", {
                user = user,
                post = {id = id, title = title, content = content, category_id = category_id},
                categories = categories,
                error_message = "Failed to update post",
                site_name = BBS_CONFIG.site_name
            })
        end
    end
end

-- Delete post
routes["/bbs/delete-post"] = function(req, resp)
    if not require_login(req, resp) then return end
    
    local user = get_current_user(req)
    local params = parse_query(req.query)
    local id = tonumber(params.id)
    
    if not id then
        send_error(resp, "Invalid post ID")
        return
    end
    
    local post = db_get_post(id)
    if not post then
        send_error(resp, "Post not found")
        return
    end
    
    if post.user_id ~= user.user_id and not user.is_admin then
        send_error(resp, "Permission denied", 403)
        return
    end
    
    if db_delete_post(id) then
        if req.headers["accept"] and req.headers["accept"]:find("application/json") then
            send_success(resp, "Post deleted")
        else
            redirect(resp, "/bbs")
        end
    else
        send_error(resp, "Failed to delete post")
    end
end

--------------------------------------------------------------------------------
-- Reply Management
--------------------------------------------------------------------------------

-- Add reply
routes["/bbs/reply"] = function(req, resp)
    if not require_login(req, resp) then return end
    
    if req.method ~= "POST" then
        resp:set_status(405)
        return
    end
    
    local user = get_current_user(req)
    local content_type = req.headers["content-type"] or ""
    local data
    
    -- Parse form data based on content type
    if content_type:find("multipart/form%-data") then
        local boundary = get_boundary(content_type)
        if boundary then
            data = parse_multipart(req.body, boundary)
        else
            data = {}
        end
    else
        data = parse_form(req.body)
    end
    
    local post_id = tonumber(data.post_id)
    local content = data.content or ""
    
    if not post_id then
        send_error(resp, "Invalid post ID")
        return
    end
    
    local post = db_get_post(post_id)
    if not post then
        send_error(resp, "Post not found")
        return
    end
    
    if post.is_locked == 1 and not user.is_admin then
        send_error(resp, "This post is locked")
        return
    end
    
    if #content < 2 then
        send_error(resp, "Reply must be at least 2 characters")
        return
    end
    
    local reply_id = db_create_reply(post_id, user.user_id, content)
    if reply_id then
        -- Handle file upload
        if data.attachment and type(data.attachment) == "table" and data.attachment.filename and data.attachment.filename ~= "" then
            local file = data.attachment
            print("Processing reply attachment: " .. file.filename .. ", size: " .. #file.data)
            
            local saved_filename, err = save_uploaded_file(file.data, file.filename)
            if saved_filename then
                db_create_attachment(nil, reply_id, user.user_id, saved_filename, file.filename, #file.data, file.content_type)
                print("Reply attachment saved: " .. saved_filename)
            else
                print("Failed to save reply attachment: " .. (err or "unknown error"))
            end
        end
        
        redirect(resp, "/bbs/post?id=" .. post_id .. "&rpage=last#reply-" .. reply_id)
    else
        send_error(resp, "Failed to create reply")
    end
end

-- Delete reply
routes["/bbs/delete-reply"] = function(req, resp)
    if not require_login(req, resp) then return end
    
    local user = get_current_user(req)
    local params = parse_query(req.query)
    local id = tonumber(params.id)
    local post_id = tonumber(params.post_id)
    
    if not id then
        send_error(resp, "Invalid reply ID")
        return
    end
    
    -- For simplicity, only admin can delete replies
    if not user.is_admin then
        send_error(resp, "Permission denied", 403)
        return
    end
    
    if db_delete_reply(id) then
        if post_id then
            redirect(resp, "/bbs/post?id=" .. post_id)
        else
            send_success(resp, "Reply deleted")
        end
    else
        send_error(resp, "Failed to delete reply")
    end
end

--------------------------------------------------------------------------------
-- User Profile
--------------------------------------------------------------------------------

routes["/bbs/profile"] = function(req, resp)
    if not require_login(req, resp) then return end
    
    local user = get_current_user(req)
    local user_data = db_get_user_by_id(user.user_id)
    
    if req.method == "GET" then
        render_html(resp, "profile.html", {
            user = user,
            profile = user_data,
            site_name = BBS_CONFIG.site_name
        })
    else
        local data = parse_form(req.body)
        local updates = {}
        
        if data.bio then
            updates.bio = data.bio
        end
        
        if data.new_password and #data.new_password >= 6 then
            if data.new_password == data.confirm_password then
                local hash, salt = hash_password(data.new_password)
                updates.password_hash = hash
                updates.password_salt = salt
            end
        end
        
        if next(updates) then
            db_update_user(user.user_id, updates)
        end
        
        redirect(resp, "/bbs/profile")
    end
end

--------------------------------------------------------------------------------
-- Admin Panel
--------------------------------------------------------------------------------

routes["/bbs/admin"] = function(req, resp)
    if not require_login(req, resp) then return end
    if not require_admin(req, resp) then return end
    
    local user = get_current_user(req)
    local stats = db_get_stats()
    
    render_html(resp, "admin/dashboard.html", {
        user = user,
        stats = stats,
        site_name = BBS_CONFIG.site_name
    })
end

-- Admin: Manage Users
routes["/bbs/admin/users"] = function(req, resp)
    if not require_login(req, resp) then return end
    if not require_admin(req, resp) then return end
    
    local user = get_current_user(req)
    local users = db_get_all_users()
    
    render_html(resp, "admin/users.html", {
        user = user,
        users = users,
        site_name = BBS_CONFIG.site_name
    })
end

-- Admin: Ban/Unban User
routes["/bbs/admin/user/ban"] = function(req, resp)
    if not require_login(req, resp) then return end
    if not require_admin(req, resp) then return end
    
    local params = parse_query(req.query)
    local id = tonumber(params.id)
    local ban = params.ban == "1"
    
    if id then
        db_ban_user(id, ban)
    end
    
    redirect(resp, "/bbs/admin/users")
end

-- Admin: Delete User
routes["/bbs/admin/user/delete"] = function(req, resp)
    if not require_login(req, resp) then return end
    if not require_admin(req, resp) then return end
    
    local params = parse_query(req.query)
    local id = tonumber(params.id)
    
    if id then
        db_delete_user(id)
    end
    
    redirect(resp, "/bbs/admin/users")
end

-- Admin: Manage Categories
routes["/bbs/admin/categories"] = function(req, resp)
    if not require_login(req, resp) then return end
    if not require_admin(req, resp) then return end
    
    local user = get_current_user(req)
    
    if req.method == "POST" then
        local data = parse_form(req.body)
        local action = data.action
        
        if action == "create" then
            db_create_category(data.name, data.description)
        elseif action == "update" then
            local gv = nil
            if data.guest_visible then
                gv = (data.guest_visible == "1")
            end
            db_update_category(tonumber(data.id), data.name, data.description, gv)
        elseif action == "delete" then
            db_delete_category(tonumber(data.id))
        elseif action == "toggle_guest" then
            db_toggle_category_guest_visible(tonumber(data.id))
        elseif action == "show_all_guest" then
            db_set_all_categories_guest_visible(true)
        elseif action == "hide_all_guest" then
            db_set_all_categories_guest_visible(false)
        end
    end
    
    local categories = db_get_categories()
    
    render_html(resp, "admin/categories.html", {
        user = user,
        categories = categories,
        site_name = BBS_CONFIG.site_name
    })
end

-- Admin: Manage Posts
routes["/bbs/admin/posts"] = function(req, resp)
    if not require_login(req, resp) then return end
    if not require_admin(req, resp) then return end
    
    local user = get_current_user(req)
    local posts = db_get_posts(nil, 50, 0)
    
    render_html(resp, "admin/posts.html", {
        user = user,
        posts = posts,
        site_name = BBS_CONFIG.site_name
    })
end

-- Admin: Pin/Unpin Post
routes["/bbs/admin/post/pin"] = function(req, resp)
    if not require_login(req, resp) then return end
    if not require_admin(req, resp) then return end
    
    local params = parse_query(req.query)
    local id = tonumber(params.id)
    local pin = params.pin == "1"
    
    if id then
        db_pin_post(id, pin)
    end
    
    redirect(resp, "/bbs/admin/posts")
end

-- Admin: Lock/Unlock Post
routes["/bbs/admin/post/lock"] = function(req, resp)
    if not require_login(req, resp) then return end
    if not require_admin(req, resp) then return end
    
    local params = parse_query(req.query)
    local id = tonumber(params.id)
    local lock = params.lock == "1"
    
    if id then
        db_lock_post(id, lock)
    end
    
    redirect(resp, "/bbs/admin/posts")
end

--------------------------------------------------------------------------------
-- Main Request Handler
--------------------------------------------------------------------------------

function handle_request(req, resp)
    -- Check exact route
    local handler = routes[req.path]
    if handler then
        handler(req, resp)
        return
    end
    
    -- Static files in /bbs/static/
    if req.path:match("^/bbs/static/") then
        local static_path = req.path:gsub("^/bbs/static/", "")
        local filepath = web_root .. "/bbs/static/" .. static_path
        
        if static_path:find("%.%.") then
            resp:set_status(403)
            resp:set_body("Forbidden")
            return
        end
        
        if file_exists(filepath) then
            serve_file(resp, filepath)
            return
        end
    end
    
    -- Uploads - serve files with original filename
    if req.path:match("^/bbs/uploads/") then
        local file_path = req.path:gsub("^/bbs/uploads/", "")
        local filepath = BBS_CONFIG.upload_dir .. "/" .. file_path
        
        if file_path:find("%.%.") then
            resp:set_status(403)
            resp:set_body("Forbidden")
            return
        end
        
        if file_exists(filepath) then
            local content = read_file(filepath)
            if content then
                local mime = get_mime_type(filepath)
                resp:set_header("Content-Type", mime)
                
                -- Try to get original filename from database
                local attachment = db_get_attachment_by_filename(file_path)
                if attachment and attachment.original_name then
                    -- For images, use inline display; for other files, force download
                    local is_image = mime:match("^image/")
                    local disposition = is_image and "inline" or "attachment"
                    resp:set_header("Content-Disposition", disposition .. '; filename="' .. attachment.original_name .. '"')
                end
                
                resp:set_body(content)
                return
            end
        end
    end
    
    serve_404(resp, req.path)
end

print("bbs_routes.lua loaded")
