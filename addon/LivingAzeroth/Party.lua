-- SPDX-License-Identifier: GPL-2.0-or-later
-- Party control: a round mark on the portrait of each bot in the party frames. On a bot the player commands it
-- shows the bot's standing order and opens the orders ring; on someone else's bot it is the grey bot mark. ("All my
-- bots", for every bot at once, is a button of its own: AllBots.lua.) Also moving the party frames: DragonUI places party frames in its own editor, so with DragonUI the addon leaves
-- placement to it.
local _, LA = ...

local Party = {}
LA.Party = Party

local UI, Style = LA.UI, LA.Style

local MARK = 20
local marks = {}

local function UnitOf(index)
    return "party" .. index
end

local function Mark(index)
    if marks[index] then
        return marks[index]
    end
    local frame = _G["PartyMemberFrame" .. index]
    if not frame then
        return nil
    end
    local mark = CreateFrame("Button", nil, frame)
    mark.livingAzerothPip = true
    mark.frame = frame
    mark:SetSize(MARK, MARK)
    local portrait = _G["PartyMemberFrame" .. index .. "Portrait"] or frame
    mark:SetPoint("CENTER", portrait, "BOTTOMLEFT", 5, 5)
    mark:SetFrameLevel(frame:GetFrameLevel() + 6)
    mark.medal = UI.Icon(mark, MARK, "medal")
    mark.medal:SetPoint("CENTER")
    mark.glow = UI.MedalGlow(mark, MARK, "OVERLAY")
    mark.glow:Hide()
    mark.hover = UI.MedalGlow(mark, MARK, "HIGHLIGHT")
    mark.hover:SetAlpha(0.45)
    -- Switches that change how the bot behaves (passive, not looting), beside the frame's bars.
    mark.flags = LA.Markers.Flags(mark, 12)
    mark.flags:SetPoint("LEFT", _G["PartyMemberFrame" .. index .. "HealthBar"] or frame, "RIGHT", 6, 0)
    mark.tick = UI.Icon(mark, 11, "plain")
    mark.tick:Set("check", Style.green)
    mark.tick:SetPoint("CENTER", mark, "TOPRIGHT", -2, -2)
    mark.tick:SetFrameLevel(mark:GetFrameLevel() + 3)
    mark.tick:Hide()
    mark:RegisterForClicks("AnyUp")
    mark:SetScript("OnClick", function(self)
        if self.guid then
            GameTooltip:Hide() -- it would cover the ring
            LA.Ring.Toggle({ key = "bot:" .. self.guid, guids = { self.guid } }, self.frame)
        end
    end)
    mark:SetScript("OnEnter", function(self)
        local unit = UnitOf(index)
        GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
        local bot = self.guid and LA.Bots.Mine()[self.guid]
        if bot then
            GameTooltip:AddLine(bot.name, 1, 0.82, 0)
            local waiting = LA.Orders.Pending(bot.guid)
            GameTooltip:AddLine(waiting and "Waiting for " .. bot.name .. "..." or LA.Orders.Standing(bot).state,
                1, 1, 1)
            GameTooltip:AddLine("Click for orders", 0.6, 0.6, 0.6)
        else
            GameTooltip:AddLine(UnitName(unit) or "", 1, 0.82, 0)
            GameTooltip:AddLine("Someone else's bot", 0.6, 0.6, 0.6)
        end
        GameTooltip:Show()
    end)
    mark:SetScript("OnLeave", GameTooltip_Hide)
    mark:SetScript("OnUpdate", function(self)
        local guid = self.guid
        if guid and LA.Orders.Pending(guid) then
            self.glow:SetAlpha(0.45 + 0.45 * math.sin(GetTime() * 9))
            self.glow:Show()
        elseif guid and LA.Ring.OpenFor() == "bot:" .. guid then
            self.glow:SetAlpha(1)
            self.glow:Show()
        else
            self.glow:Hide()
        end
        LA.SetShown(self.tick, guid and LA.Orders.RecentlyDone(guid))
    end)
    marks[index] = mark
    return mark
end

function Party.Update()
    for index = 1, MAX_PARTY_MEMBERS do
        local mark = Mark(index)
        if mark then
            local unit = UnitOf(index)
            local guid = LA.Bots.Guid(unit)
            local mine = guid and LA.Bots.Mine()[guid]
            local bot = LA.Bots.ForUnit(unit)
            if mine and mine.commandable then
                mark.guid = guid
                local order = LA.Orders.Standing(mine)
                mark.medal:Set(order.glyph, order.tone)
                mark.medal.base:SetDesaturated(false)
                mark.flags:Update(mine)
                mark.flags:Show()
                mark:Show()
            elseif bot then
                mark.guid = nil
                mark.medal:Set("bot", "stone")
                mark.medal.base:SetDesaturated(true)
                mark.flags:Hide()
                mark:Show()
            else
                mark.guid = nil
                mark:Hide()
            end
        end
    end
end

LA.On("bots:changed", Party.Update)
LA.On("orders:changed", Party.Update)
LA.On("bridge:state", Party.Update)

local events = CreateFrame("Frame")
events:RegisterEvent("PARTY_MEMBERS_CHANGED")
events:RegisterEvent("RAID_ROSTER_UPDATE")
events:RegisterEvent("PLAYER_ENTERING_WORLD")
events:SetScript("OnEvent", Party.Update)

-- Moving the party frames (without DragonUI). The first frame carries the others.

local mover

local function SavePosition()
    local frame = PartyMemberFrame1
    local x, y = frame:GetLeft(), frame:GetTop()
    LivingAzerothDB.party = { x = x, y = y }
end

local function ApplyPosition()
    local saved = LivingAzerothDB.party
    if not saved or LA.UsesDragonUI() or InCombatLockdown() then
        return
    end
    PartyMemberFrame1:ClearAllPoints()
    PartyMemberFrame1:SetPoint("TOPLEFT", UIParent, "BOTTOMLEFT", saved.x, saved.y)
end

local function Mover()
    if not mover then
        -- Four party frames and their gaps.
        mover = UI.Mover(PartyMemberFrame1, "Party frames", 190, 4 * 64, SavePosition)
    end
    return mover
end

function Party.Unlock(unlocked)
    if LA.UsesDragonUI() then
        if unlocked and GetNumRaidMembers() == 0 then
            LA.Print("DragonUI places the party frames: type /duiedit.")
        end
        return
    end
    if unlocked and InCombatLockdown() then
        LA.Print("frames can't be moved during combat.")
        return
    end
    -- In a raid the game hides the party frames; the raid frames have their own mover.
    LA.SetShown(Mover(), unlocked and GetNumRaidMembers() == 0)
end

function LA.UsesDragonUI()
    return IsAddOnLoaded("DragonUI")
end

local placement = CreateFrame("Frame")
placement:RegisterEvent("PLAYER_ENTERING_WORLD")
placement:RegisterEvent("PLAYER_REGEN_ENABLED")
placement:SetScript("OnEvent", ApplyPosition)
