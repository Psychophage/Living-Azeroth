-- SPDX-License-Identifier: GPL-2.0-or-later
-- Bots of the player's that die release their spirit and run back on their own: soon when no one in the group can
-- bring them back, and after a longer wait when someone could (a priest, paladin or shaman, or a druid with Rebirth;
-- the player counts too), counted from the end of the fight, since that is when resurrecting starts. In dungeons and
-- raids the player can have them wait for the fight to end either way. It is on for every bot unless the player
-- switches it off, for all bots in the settings or for some in the orders ring ("Run back"); a bot with it off waits
-- to be resurrected or told to release. Releasing is the ring's own Release order, given for the player.
local _, LA = ...

local Deaths = {}
LA.Deaths = Deaths

-- Seconds a dead bot waits: { lowest, highest, default }.
Deaths.ALONE = { 0, 60, 5 }
Deaths.HELPED = { 10, 180, 45 }

-- Classes that resurrect out of combat, from the level they learn it.
local RESURRECTERS = { PRIEST = 10, PALADIN = 12, SHAMAN = 12, DRUID = 20 }
local RETRY = 10 -- seconds before asking a bot again when a release didn't go through

local deadSince = {} -- guid -> when the bot was first seen dead
local asked = {} -- guid -> when it was told to release
local quietSince -- when the group's last fight ended; nil while it fights

local function Settings()
    return LivingAzerothDB.deaths
end

-- Whether this bot runs back on its own: its own switch if the player set one in the ring, else the setting for all.
function Deaths.IsOn(bot)
    local own = Settings().bots[bot.guid]
    if own ~= nil then
        return own
    end
    return Settings().on
end

function Deaths.Set(bot, on)
    Settings().bots[bot.guid] = on
    LA.Fire("orders:changed", bot.guid)
    LA.Settings.Refresh()
end

-- The setting for every bot; it replaces what was set for single bots.
function Deaths.SetAll(on)
    Settings().on = on
    wipe(Settings().bots)
    for guid in pairs(LA.Bots.Mine()) do
        LA.Fire("orders:changed", guid)
    end
end

local function Units()
    local units = { "player" }
    local raid = GetNumRaidMembers()
    if raid > 0 then
        for i = 1, raid do
            units[#units + 1] = "raid" .. i
        end
    else
        for i = 1, GetNumPartyMembers() do
            units[#units + 1] = "party" .. i
        end
    end
    return units
end

-- Whether anyone alive in the group could resurrect a bot.
function Deaths.SomeoneCanResurrect()
    for _, unit in ipairs(Units()) do
        local _, class = UnitClass(unit)
        local level = RESURRECTERS[class]
        if level and UnitLevel(unit) >= level and not UnitIsDeadOrGhost(unit) and UnitIsConnected(unit) then
            return true
        end
    end
    return false
end

local function Fighting()
    for _, unit in ipairs(Units()) do
        if UnitAffectingCombat(unit) and not UnitIsDeadOrGhost(unit) then
            return true
        end
    end
    return false
end

local function InDungeon()
    local inside, kind = IsInInstance()
    return inside and (kind == "party" or kind == "raid")
end

-- When the bot's wait started, and how long it is; no start while it has to wait for the fight to end.
local function Wait(guid, helped)
    local settings = Settings()
    if helped then
        return quietSince and math.max(quietSince, deadSince[guid]), settings.helped
    end
    if settings.dungeonsWait and InDungeon() then
        return quietSince and math.max(quietSince, deadSince[guid]), settings.alone
    end
    return deadSince[guid], settings.alone
end

local function Check()
    local now = GetTime()
    if Fighting() then
        quietSince = nil
    else
        quietSince = quietSince or now
    end
    local helped
    for guid, bot in pairs(LA.Bots.Mine()) do
        if bot.life == "dead" and bot.commandable then
            deadSince[guid] = deadSince[guid] or now
            if Deaths.IsOn(bot) and (not asked[guid] or now - asked[guid] >= RETRY) then
                if helped == nil then
                    helped = Deaths.SomeoneCanResurrect()
                end
                local start, wait = Wait(guid, helped)
                if start and now - start >= wait then
                    asked[guid] = now
                    LA.Debug(bot.name .. " releases on its own" .. (helped and " (no one resurrected it)" or ""))
                    LA.Orders.Give(bot, "release")
                end
            end
        else
            deadSince[guid], asked[guid] = nil, nil
        end
    end
end

local ticker = CreateFrame("Frame")
local elapsed = 0
ticker:SetScript("OnUpdate", function(_, delta)
    elapsed = elapsed + delta
    if elapsed >= 0.5 and LivingAzerothDB and LivingAzerothDB.deaths then
        elapsed = 0
        Check()
    end
end)

LA.On("loaded", function()
    local settings = LivingAzerothDB.deaths or {}
    if settings.on == nil then
        settings.on = true
    end
    settings.bots = settings.bots or {}
    settings.alone = tonumber(settings.alone) or Deaths.ALONE[3]
    settings.helped = tonumber(settings.helped) or Deaths.HELPED[3]
    if settings.dungeonsWait == nil then
        settings.dungeonsWait = true
    end
    LivingAzerothDB.deaths = settings
end)
