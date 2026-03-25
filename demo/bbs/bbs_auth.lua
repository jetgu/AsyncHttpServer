-- bbs_auth.lua
-- Authentication functions for BBS

-- Create session
function create_session(user_id, username, is_admin)
    local session_id = random_string(32)
    SESSIONS[session_id] = {
        user_id = user_id,
        username = username,
        is_admin = is_admin,
        created_at = get_timestamp()
    }
    return session_id
end

-- Get session
function get_session(session_id)
    if not session_id then return nil end
    
    local session = SESSIONS[session_id]
    if not session then return nil end
    
    -- Check timeout
    if get_timestamp() - session.created_at > BBS_CONFIG.session_timeout then
        SESSIONS[session_id] = nil
        return nil
    end
    
    return session
end

-- Destroy session
function destroy_session(session_id)
    if session_id then
        SESSIONS[session_id] = nil
    end
end

-- Get current user from request
function get_current_user(req)
    local session_id = get_cookie(req, "session")
    return get_session(session_id)
end

-- Check if user is logged in
function is_logged_in(req)
    return get_current_user(req) ~= nil
end

-- Check if user is admin
function is_admin(req)
    local user = get_current_user(req)
    return user and user.is_admin
end

-- Require login middleware
function require_login(req, resp)
    if not is_logged_in(req) then
        redirect(resp, "/bbs/login")
        return false
    end
    return true
end

-- Require admin middleware
function require_admin(req, resp)
    if not is_admin(req) then
        resp:set_status(403)
        render_html(resp, "error.html", {
            error_title = "Access Denied",
            error_message = "You must be an administrator to access this page."
        })
        return false
    end
    return true
end

-- Hash password
function hash_password(password, salt)
    salt = salt or random_string(16)
    local hash = simple_hash(salt .. password .. salt)
    return hash, salt
end

-- Verify password
function verify_password(password, hash, salt)
    local computed_hash = simple_hash(salt .. password .. salt)
    return computed_hash == hash
end

--------------------------------------------------------------------------------
-- CAPTCHA Functions
--------------------------------------------------------------------------------

-- Generate a new CAPTCHA
function generate_captcha()
    -- Clean up expired CAPTCHAs
    local now = get_timestamp()
    for id, captcha in pairs(CAPTCHAS) do
        if now - captcha.created_at > BBS_CONFIG.captcha_timeout then
            CAPTCHAS[id] = nil
        end
    end
    
    -- Generate math problem
    local operators = {"+", "-", "x"}
    local op = operators[math.random(1, 3)]
    local num1, num2, answer
    
    if op == "+" then
        num1 = math.random(1, 50)
        num2 = math.random(1, 50)
        answer = num1 + num2
    elseif op == "-" then
        num1 = math.random(20, 100)
        num2 = math.random(1, num1)
        answer = num1 - num2
    else -- "x"
        num1 = math.random(1, 12)
        num2 = math.random(1, 12)
        answer = num1 * num2
    end
    
    local captcha_id = random_string(32)
    local question = tostring(num1) .. " " .. op .. " " .. tostring(num2) .. " = ?"
    
    CAPTCHAS[captcha_id] = {
        answer = answer,
        question = question,  -- Store question text for image generation
        created_at = now
    }
    
    return captcha_id, question
end

-- Verify CAPTCHA answer
function verify_captcha(captcha_id, user_answer)
    if not captcha_id or not user_answer then
        return false, "CAPTCHA is required"
    end
    
    local captcha = CAPTCHAS[captcha_id]
    if not captcha then
        return false, "CAPTCHA expired, please try again"
    end
    
    -- Check timeout
    if get_timestamp() - captcha.created_at > BBS_CONFIG.captcha_timeout then
        CAPTCHAS[captcha_id] = nil
        return false, "CAPTCHA expired, please try again"
    end
    
    -- Remove CAPTCHA after use (one-time use)
    CAPTCHAS[captcha_id] = nil
    
    -- Verify answer
    local answer_num = tonumber(user_answer)
    if answer_num == captcha.answer then
        return true
    else
        return false, "Incorrect CAPTCHA answer"
    end
end

print("bbs_auth.lua loaded")
