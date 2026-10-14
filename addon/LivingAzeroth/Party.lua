-- SPDX-License-Identifier: GPL-2.0-or-later
-- Party control: a small order icon on the party frame of each bot the player commands (it shows what the bot is
-- doing and opens the orders ring), and moving the party frames. DragonUI places party frames in its own editor,
-- so with DragonUI the addon leaves placement to it.
local _, LA = ...

local Party = {}
LA.Party = Party

local pips = {}

local function UnitOf(index)
    return "party" .. index
end

local function Pip(index)
    if pips[index] then
        return pips[index]
    end
    local frame = _G["PartyMemberFrame" .. index]
    if not frame then
        return nil
    end
    local pip = CreateFrame("Button", nil, frame)
    pip.livingAzerothPip = true
    pip:SetSize(20, 20)
    pip:SetPoint("LEFT", _G["PartyMemberFrame" .. index .. "HealthBar"] or frame, "RIGHT", 6, 4)
    pip:SetFrameLevel(frame:GetFrameLevel() + 6)
    pip.tile = LA.UI.Icon(pip, 20, "tile")
    pip.tile:SetPoint("CENTER")
    pip.glow = pip:CreateTexture(nil, "OVERLAY")
    pip.glow:SetTexture(LA.Style.ART .. "tile-glow")
    pip.glow:SetBlendMode("ADD")
    pip.glow:SetSize(40, 40)
    pip.glow:SetPoint("CENTER")
    pip.glow:Hide()
    local hover = pip:CreateTexture(nil, "HIGHLIGHT")
    hover:SetTexture(LA.Style.ART .. "tile-glow")
    hover:SetBlendMode("ADD")
    hover:SetSize(40, 40)
    hover:SetPoint("CENTER")
    hover:SetAlpha(0.35)
    pip.flags = LA.Markers.Flags(pip, 12)
    pip.flags:SetPoint("LEFT", pip, "RIGHT", 4, 0)
    pip.tick = LA.UI.Icon(pip, 13, "plain")
    pip.tick:Set("check", LA.Style.green)
    pip.tick:SetPoint("CENTER", pip, "TOPRIGHT", -1, -1)
    pip.tick:SetFrameLevel(pip:GetFrameLevel() + 3)
    pip.tick:Hide()
    pip:RegisterForClicks("AnyUp")
    pip:SetScript("OnClick", function(self)
        if self.guid then
            GameTooltip:Hide() -- it would cover the ring
            LA.Ring.Toggle({ key = "bot:" .. self.guid, guids = { self.guid } }, self)
        end
    end)
    pip:SetScript("OnEnter", function(self)
        local bot = self.guid and LA.Bots.Mine()[self.guid]
        if not bot then
            return
        end
        GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
        GameTooltip:AddLine(bot.name, 1, 0.82, 0)
        local waiting = LA.Orders.Pending(bot.guid)
        GameTooltip:AddLine(waiting and "Waiting for " .. bot.name .. "..." or LA.Orders.Standing(bot).state, 1, 1, 1)
        GameTooltip:AddLine("Click for orders", 0.6, 0.6, 0.6)
        GameTooltip:Show()
    end)
    pip:SetScript("OnLeave", function()
        GameTooltip:Hide()
    end)
    pip:SetScript("OnUpdate", function(self)
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
    pips[index] = pip
    return pip
end

function Party.Update()
    for index = 1, MAX_PARTY_MEMBERS do
        local pip = Pip(index)
        if pip then
            local guid = LA.Bots.Guid(UnitOf(index))
            local bot = guid and LA.Bots.Mine()[guid]
            if bot and bot.commandable then
                pip.guid = guid
                local order = LA.Orders.Standing(bot)
                pip.tile:Set(order.glyph, order.tone)
                pip.flags:Update(bot)
                pip:Show()
            else
                pip.guid = nil
                pip:Hide()
            end
        end
    end
end

LA.On("bots:changed", Party.Update)
LA.On("orders:changed", Party.Update)
LA.On("bridge:state", Party.Update)

local events = CreateFrame("Frame")
events:RegisterEvent("PARTY_MEMBERS_CHANGED")
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
        mover = LA.UI.Mover(PartyMemberFrame1, "Party frames", 190, 4 * 64, function()
            PartyMemberFrame1:SetUserPlaced(false) -- the addon keeps the position, not the game's layout cache
            SavePosition()
        end)
    end
    return mover
end

function Party.Unlock(unlocked)
    if LA.UsesDragonUI() then
        LA.Print("DragonUI places the party frames: type /duiedit.")
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
