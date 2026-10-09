-- SPDX-License-Identifier: GPL-2.0-or-later
-- What the addon knows about bots: the player's own and group bots (kept current by the server's events), and
-- whether any other character is a bot (asked for in batches and remembered for a while).
local _, LA = ...

local Bots = {}
LA.Bots = Bots

local REMEMBER = 120 -- seconds before asking again whether a character is a bot; an alt can be played or botted
local MAX_WHO = 40

local mine = {} -- guid -> bot description from the server
local others = {} -- guid -> { bot = description or false, at = time }
local wanted = {} -- guids waiting to be asked about
local asking = false

-- The low part of a unit's GUID, the number the server uses; nil for anything that isn't a player character.
function Bots.Guid(unit)
    local guid = unit and UnitGUID(unit)
    if not guid or not UnitIsPlayer(unit) then
        return nil
    end
    return tonumber(guid:sub(-8), 16)
end

-- A bot's description, false when the character is known not to be a bot, nil while unknown (then asked about;
-- "bots:changed" fires with the answer).
function Bots.Get(guid)
    if not guid then
        return nil
    end
    if mine[guid] then
        return mine[guid]
    end
    local known = others[guid]
    if known and GetTime() - known.at < REMEMBER then
        return known.bot
    end
    if LA.Bridge.state == "ready" and guid ~= Bots.Guid("player") then
        wanted[guid] = true
    end
    return known and known.bot or nil
end

function Bots.ForUnit(unit)
    return Bots.Get(Bots.Guid(unit))
end

function Bots.Mine()
    return mine
end

function Bots.Count()
    local n = 0
    for _ in pairs(mine) do
        n = n + 1
    end
    return n
end

local function Changed(guid)
    LA.Fire("bots:changed", guid)
end

local function Ask()
    if asking or LA.Bridge.state ~= "ready" or not next(wanted) then
        return
    end
    local guids = LA.Json.Array()
    for guid in pairs(wanted) do
        if #guids >= MAX_WHO then
            break
        end
        guids[#guids + 1] = guid
        wanted[guid] = nil
    end
    asking = true
    LA.Bridge.Request({ op = "who", guids = guids }, function(reply)
        asking = false
        if not reply.ok then
            LA.Debug("who failed: " .. tostring(reply.error))
            return
        end
        local now = GetTime()
        local found = {}
        for _, bot in ipairs(reply.bots or {}) do
            found[bot.guid] = bot
        end
        for _, guid in ipairs(guids) do
            local before = others[guid] and others[guid].bot
            others[guid] = { bot = found[guid] or false, at = now }
            if found[guid] or before then
                Changed(guid)
            end
        end
    end)
end

local function Refresh()
    LA.Bridge.Request({ op = "bots" }, function(reply)
        if not reply.ok then
            return
        end
        local before = mine
        mine = {}
        for _, bot in ipairs(reply.bots or {}) do
            mine[bot.guid] = bot
        end
        for guid in pairs(before) do
            if not mine[guid] then
                Changed(guid)
            end
        end
        for guid in pairs(mine) do
            Changed(guid)
        end
    end)
end

LA.On("bridge:state", function(state)
    if state == "ready" then
        Refresh()
    elseif state == "missing" then
        wipe(mine)
        wipe(wanted)
    end
end)

-- A full description from the server: one of the player's own or group bots.
function Bots.Update(bot)
    if type(bot) == "table" and bot.guid then
        mine[bot.guid] = bot
        Changed(bot.guid)
    end
end

LA.On("bridge:bot", function(event)
    Bots.Update(event.bot)
end)

LA.On("bridge:gone", function(event)
    if event.guid and mine[event.guid] then
        mine[event.guid] = nil
        Changed(event.guid)
    end
end)

local ticker = CreateFrame("Frame")
local elapsed = 0
ticker:SetScript("OnUpdate", function(_, delta)
    elapsed = elapsed + delta
    if elapsed >= 0.3 then
        elapsed = 0
        Ask()
    end
end)
