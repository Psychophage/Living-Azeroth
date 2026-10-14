-- SPDX-License-Identifier: GPL-2.0-or-later
-- Telling bots from players at a glance: a line in the unit tooltip and a small medal on party frames
-- (the game's own party frames, which DragonUI restyles in place).
local _, LA = ...

local Markers = {}
LA.Markers = Markers

-- The bot glyph, gold, for a line of tooltip text.
local ICON = "|T" .. LA.Style.ART .. "glyph-bot:14:14:0:0:64:64:0:64:0:64:255:211:107|t"

-- The bot mark: a gold-rimmed medal with the bot glyph, size across; grey for someone else's bot.
function Markers.Medal(parent, size)
    local medal = LA.UI.Icon(parent, size, "medal")
    medal:Set("bot", "gold")
    function medal:SetCommandable(commandable)
        self:Set("bot", commandable and "gold" or "stone")
        self.base:SetDesaturated(not commandable)
    end
    return medal
end

-- Small marks for switches that change how a bot behaves: passive, and not picking up loot.
function Markers.Flags(parent, size)
    local flags = CreateFrame("Frame", nil, parent)
    flags:SetSize(size * 2 + 2, size)
    flags.passive = LA.UI.Icon(flags, size, "plain")
    flags.passive:Set("passive", LA.Style.C("#c7c0ff"))
    flags.noLoot = LA.UI.Icon(flags, size, "plain")
    flags.noLoot:Set("loot", LA.Style.C("#8a8170"))
    flags.noLoot:SetAlpha(0.8)
    function flags:Update(bot)
        local switches = bot and bot.switches or {}
        local passive, noLoot = switches.passive == true, switches.loot == false
        self.passive:ClearAllPoints()
        self.noLoot:ClearAllPoints()
        self.passive:SetPoint("RIGHT")
        if passive then
            self.noLoot:SetPoint("RIGHT", self.passive, "LEFT", -2, 0)
        else
            self.noLoot:SetPoint("RIGHT")
        end
        LA.SetShown(self.passive, passive)
        LA.SetShown(self.noLoot, noLoot)
    end
    return flags
end

-- Tooltip

local function TooltipLine(bot)
    if bot.commandable then
        return "Bot · you can give orders", 1, 0.82, 0
    end
    return "Bot", 0.6, 0.6, 0.6
end

local function DescribeTooltip(tooltip)
    local _, unit = tooltip:GetUnit()
    local bot = LA.Bots.ForUnit(unit)
    tooltip.livingAzerothGuid = LA.Bots.Guid(unit)
    if bot then
        local text, r, g, b = TooltipLine(bot)
        tooltip:AddLine(ICON .. " " .. text, r, g, b)
        tooltip:Show()
    end
end

GameTooltip:HookScript("OnTooltipSetUnit", DescribeTooltip)

-- Party frames

local medals = {}

local function PartyMedal(index)
    local frame = _G["PartyMemberFrame" .. index]
    if not frame then
        return nil
    end
    if not medals[index] then
        local medal = Markers.Medal(frame, 18)
        local portrait = _G["PartyMemberFrame" .. index .. "Portrait"] or frame
        medal:SetPoint("CENTER", portrait, "BOTTOMLEFT", 4, 4)
        medal:SetFrameLevel(frame:GetFrameLevel() + 5)
        medals[index] = medal
    end
    return medals[index]
end

function Markers.UpdateParty()
    for index = 1, MAX_PARTY_MEMBERS do
        local medal = PartyMedal(index)
        if medal then
            local bot = LA.Bots.ForUnit("party" .. index)
            if bot then
                medal:SetCommandable(bot.commandable)
                medal:Show()
            else
                medal:Hide()
            end
        end
    end
end

LA.On("bots:changed", function(guid)
    Markers.UpdateParty()
    -- Refresh an open tooltip that was waiting for this answer.
    if GameTooltip:IsShown() and GameTooltip.livingAzerothGuid == guid then
        local _, unit = GameTooltip:GetUnit()
        if unit then
            GameTooltip:SetUnit(unit)
        end
    end
end)
LA.On("bridge:state", Markers.UpdateParty)

local events = CreateFrame("Frame")
events:RegisterEvent("PARTY_MEMBERS_CHANGED")
events:RegisterEvent("PLAYER_ENTERING_WORLD")
events:SetScript("OnEvent", Markers.UpdateParty)
