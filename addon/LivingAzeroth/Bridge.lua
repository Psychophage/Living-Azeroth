-- SPDX-License-Identifier: GPL-2.0-or-later
-- The connection to the server's bot control bridge (docs/features/bot-control.md): JSON requests whispered to
-- the player's own character as addon messages, split into frames "1<id>:<part>/<total>:<chunk>".
local _, LA = ...

local Bridge = {}
LA.Bridge = Bridge

local PREFIX = "LABC"
local MAX_FRAME = 250
local MAX_PARTS = 24 -- the server's limit on a request
local MAX_REPLY_PARTS = 400 -- replies have no protocol limit; a raid's bots take about 70 frames
local HELLO_TIMEOUT = 6
local REQUEST_TIMEOUT = 20 -- orders wait up to the server's own 10 seconds for the bot to act

-- "connecting", "ready" (the server answered hello) or "missing" (no bridge on this server).
Bridge.state = "connecting"

local nextId = 0
local pending = {} -- id -> { callback, deadline }
local incoming = nil -- the reply being joined: { id, total, next, parts }

local function NewId()
    nextId = nextId % 2000000000 + 1
    local digits, n = "", nextId
    repeat
        local d = n % 36
        digits = string.sub("0123456789abcdefghijklmnopqrstuvwxyz", d + 1, d + 1) .. digits
        n = math.floor(n / 36)
    until n == 0
    return digits
end

local function Frames(id, message)
    -- The header grows with the number of parts, so settle the count first.
    local total, room = 1, 0
    while true do
        local header = 1 + #id + 1 + #tostring(total) * 2 + 2
        room = MAX_FRAME - header
        local needed = math.max(1, math.ceil(#message / room))
        if needed <= total then
            break
        end
        total = needed
    end
    if total > MAX_PARTS then
        return nil
    end
    local frames = {}
    for part = 1, total do
        local offset = (part - 1) * room
        frames[part] = "1" .. id .. ":" .. part .. "/" .. total .. ":" .. message:sub(offset + 1, offset + room)
    end
    return frames
end

-- Sends a request ({ op = ..., ... }); callback(reply) gets the server's reply, or { ok = false, error = "..." }
-- when it can't be sent or nothing came back within timeout seconds.
function Bridge.Request(request, callback, timeout)
    local id = NewId()
    request.id = id
    local frames = Frames(id, LA.Json.Encode(request))
    if not frames then
        if callback then
            callback({ ok = false, error = "too_large" })
        end
        return
    end
    pending[id] = { op = request.op, callback = callback, deadline = GetTime() + (timeout or REQUEST_TIMEOUT) }
    local me = UnitName("player")
    for _, frame in ipairs(frames) do
        SendAddonMessage(PREFIX, frame, "WHISPER", me)
    end
    return id
end

-- Joins one frame; the whole message once its last frame is in.
local function Join(frame)
    local id, part, total, chunk = frame:match("^1(%w+):(%d+)/(%d+):(.*)$")
    part, total = tonumber(part), tonumber(total)
    if not id or not part or part < 1 or part > total or total > MAX_REPLY_PARTS then
        return nil
    end
    if part == 1 then
        incoming = { id = id, total = total, next = 1, parts = {} }
    end
    if not incoming or incoming.id ~= id or incoming.total ~= total or incoming.next ~= part then
        incoming = nil
        return nil
    end
    incoming.parts[part] = chunk
    incoming.next = part + 1
    if part < total then
        return nil
    end
    local message = table.concat(incoming.parts)
    incoming = nil
    return message
end

local function Receive(message)
    local data, problem = LA.Json.Decode(message)
    if type(data) ~= "table" then
        LA.Debug("bridge: unreadable message (" .. tostring(problem) .. ")")
        return
    end
    if data.re then
        local request = pending[data.re]
        pending[data.re] = nil
        if request and request.callback then
            local ok, problem = pcall(request.callback, data)
            if not ok then
                LA.Debug("error handling the " .. tostring(request.op) .. " reply: " .. tostring(problem))
            end
        end
    elseif data.ev then
        LA.Fire("bridge:" .. data.ev, data)
    end
end

local function SetState(state)
    if Bridge.state ~= state then
        Bridge.state = state
        LA.Fire("bridge:state", state)
    end
end

-- Says hello (again after a reload or a new login); the server then sends changes to the player's bots.
function Bridge.Connect()
    SetState("connecting")
    Bridge.Request({ op = "hello" }, function(reply)
        if reply.ok then
            Bridge.protocol = reply.protocol
            Bridge.orders = reply.orders
            SetState("ready")
        else
            SetState("missing")
        end
    end, HELLO_TIMEOUT)
end

local frame = CreateFrame("Frame")
frame:RegisterEvent("CHAT_MSG_ADDON")
frame:SetScript("OnEvent", function(_, _, prefix, text, channel, sender)
    -- Only the server answers as the player's own whisper; anything else with this prefix is ignored.
    if prefix ~= PREFIX or channel ~= "WHISPER" or sender ~= UnitName("player") then
        return
    end
    if text:sub(1, 1) ~= "1" then
        return
    end
    local message = Join(text)
    if message then
        Receive(message)
    end
end)

local elapsed = 0
frame:SetScript("OnUpdate", function(_, delta)
    elapsed = elapsed + delta
    if elapsed < 0.5 then
        return
    end
    elapsed = 0
    local now = GetTime()
    for id, request in pairs(pending) do
        if now > request.deadline then
            pending[id] = nil
            if request.callback then
                request.callback({ ok = false, error = "no_answer" })
            end
        end
    end
end)
