-- SPDX-License-Identifier: GPL-2.0-or-later
-- Telling bots from players at a glance: a line in the unit tooltip and a small medal on party frames
-- (the game's own party frames, which DragonUI restyles in place).
local _, LA = ...

local Markers = {}
LA.Markers = Markers

local ICON = "Interface\\Icons\\INV_Misc_Gear_01"
local RING = "Interface\\Minimap\\MiniMap-TrackingBorder"

-- A round gold-ringed medal like the minimap's tracking button, size pixels across.
function Markers.Medal(parent, size)
    local medal = CreateFrame("Frame", nil, parent)
    medal:SetSize(size, size)
    medal.icon = medal:CreateTexture(nil, "ARTWORK")
    medal.icon:SetTexture(ICON)
    medal.icon:SetTexCoord(0.08, 0.92, 0.08, 0.92)
    medal.icon:SetPoint("CENTER")
    medal.icon:SetSize(size * 0.62, size * 0.62)
    medal.ring = medal:CreateTexture(nil, "OVERLAY")
    medal.ring:SetTexture(RING)
    -- The ring sits in the top left of its texture, which is 54/31 of the button it frames.
    medal.ring:SetSize(size * 54 / 31, size * 54 / 31)
    medal.ring:SetPoint("TOPLEFT", medal, "TOPLEFT", 0, 0)
    function medal:SetCommandable(commandable)
        self.icon:SetDesaturated(not commandable)
        self.ring:SetVertexColor(commandable and 1 or 0.6, commandable and 1 or 0.6, commandable and 1 or 0.6)
    end
    return medal
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
        tooltip:AddLine("|T" .. ICON .. ":14:14:0:0:64:64:5:59:5:59|t " .. text, r, g, b)
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
