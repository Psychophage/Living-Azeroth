-- SPDX-License-Identifier: GPL-2.0-or-later
-- Telling bots from players at a glance: a line in the unit tooltip (the party and raid frames mark bots
-- themselves).
local _, LA = ...

local Markers = {}
LA.Markers = Markers

-- The bot glyph, gold, for a line of tooltip text.
local ICON = "|T" .. LA.Style.ART .. "glyph-bot:14:14:0:0:64:64:0:64:0:64:255:211:107|t"

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

LA.On("bots:changed", function(guid)
    -- Refresh an open tooltip that was waiting for this answer.
    if GameTooltip:IsShown() and GameTooltip.livingAzerothGuid == guid then
        local _, unit = GameTooltip:GetUnit()
        if unit then
            GameTooltip:SetUnit(unit)
        end
    end
end)
