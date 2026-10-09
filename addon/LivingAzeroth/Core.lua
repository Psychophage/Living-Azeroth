-- SPDX-License-Identifier: GPL-2.0-or-later
-- Living Azeroth: bot control for the Living Azeroth server.
local _, LA = ...
LivingAzeroth = LA

LA.name = "Living Azeroth"
LA.gold = { 1, 0.82, 0 }

-- Listeners for the addon's own events ("bridge:state", "bots:changed", ...).
local listeners = {}

function LA.On(event, callback)
    listeners[event] = listeners[event] or {}
    table.insert(listeners[event], callback)
end

function LA.Fire(event, ...)
    for _, callback in ipairs(listeners[event] or {}) do
        local ok, problem = pcall(callback, ...)
        if not ok then
            LA.Debug("error in " .. event .. ": " .. tostring(problem))
        end
    end
end

-- Show or hide (the 3.3.5 API has no SetShown).
function LA.SetShown(region, shown)
    if shown then
        region:Show()
    else
        region:Hide()
    end
end

function LA.Print(text)
    DEFAULT_CHAT_FRAME:AddMessage("|cffffd100Living Azeroth:|r " .. text)
end

-- A short rolling log kept in the saved variables (written on reload or logout), for finding problems.
function LA.Debug(text)
    if not LivingAzerothDB then
        return
    end
    local log = LivingAzerothDB.log
    log[#log + 1] = date("%H:%M:%S ") .. text
    while #log > 200 do
        table.remove(log, 1)
    end
    if LivingAzerothDB.debug then
        LA.Print("|cff888888" .. text .. "|r")
    end
end

local events = CreateFrame("Frame")
events:RegisterEvent("ADDON_LOADED")
events:RegisterEvent("PLAYER_ENTERING_WORLD")
events:SetScript("OnEvent", function(_, event, name)
    if event == "ADDON_LOADED" and name == "LivingAzeroth" then
        LivingAzerothDB = LivingAzerothDB or {}
        LivingAzerothDB.log = LivingAzerothDB.log or {}
        LA.Fire("loaded")
    elseif event == "PLAYER_ENTERING_WORLD" and not LA.connected then
        -- Once per login or reload; zoning keeps the connection.
        LA.connected = true
        LA.Bridge.Connect()
    end
end)

LA.On("bridge:state", function(state)
    LA.Debug("bridge " .. state)
    if state == "missing" then
        LA.Print("this server has no bot control, so the addon stays quiet here.")
    end
end)

SLASH_LIVINGAZEROTH1 = "/la"
SLASH_LIVINGAZEROTH2 = "/livingazeroth"
SlashCmdList.LIVINGAZEROTH = function(text)
    local command = strtrim(text or ""):lower()
    if command == "debug" then
        LivingAzerothDB.debug = not LivingAzerothDB.debug
        LA.Print("debug messages " .. (LivingAzerothDB.debug and "on" or "off") .. ".")
    elseif command == "reconnect" then
        LA.Bridge.Connect()
    elseif command == "unlock" or command == "lock" then
        LA.unlocked = command == "unlock"
        LA.Party.Unlock(command == "unlock")
        LA.Raid.Unlock(command == "unlock")
        LA.RingSettings.Unlock(command == "unlock")
    elseif command == "ring" then
        LA.RingSettings.Toggle()
    elseif command == "" then
        LA.Manager.Toggle()
    else
        local state = ({ ready = "connected", connecting = "connecting...", missing = "no bot control on this server" })
        LA.Print(state[LA.Bridge.state] .. "; " .. LA.Bots.Count() .. " bots of yours known.")
    end
end
