-- SPDX-License-Identifier: GPL-2.0-or-later
-- The orders ring's settings: where it opens, its size, and which orders it holds (up to eight, in ring order).
-- With "a fixed spot", unlocking frames shows the spot to drag.
local _, LA = ...

local RingSettings = {}
LA.RingSettings = RingSettings

local PLACEMENTS = {
    { id = "beside", label = "Beside the frame", hint = "Next to the bot's frame" },
    { id = "cursor", label = "At the mouse", hint = "Centred where you click" },
    { id = "fixed", label = "A fixed spot", hint = "Drag it after Move frames" },
}
local SIZES = {
    { id = "small", label = "Small" },
    { id = "normal", label = "Normal" },
    { id = "large", label = "Large" },
}

local window

local function Settings()
    return LivingAzerothDB.ring
end

local function Changed()
    LA.Ring.Relayout()
    RingSettings.Refresh()
    LA.Fire("ring:settings")
end

local function Window()
    if window then
        return window
    end
    local UI = LA.UI
    window = LA.Window.Create("LivingAzerothRingSettings", 470, 550, "Orders ring",
        "Interface\\Icons\\Ability_Tracking")
    window:SetFrameStrata("DIALOG")

    local intro = UI.Text(window.header, 360)
    intro:SetPoint("TOPLEFT", 64, -10)
    intro:SetText("Click a bot's order icon to open its ring. Choose where it opens, its size, and what it holds.")

    local where = window.bar:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    where:SetPoint("LEFT", 12, 0)
    where:SetText("Where it opens")

    local content = window.content
    window.placements = {}
    for i, placement in ipairs(PLACEMENTS) do
        local radio = CreateFrame("CheckButton", nil, content, "UIRadioButtonTemplate")
        radio:SetPoint("TOPLEFT", 8 + (i - 1) * 140, -8)
        local label = radio:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
        label:SetPoint("LEFT", radio, "RIGHT", 2, 0)
        label:SetText(placement.label)
        local hint = radio:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
        hint:SetPoint("TOPLEFT", label, "BOTTOMLEFT", 0, -2)
        hint:SetWidth(122)
        hint:SetJustifyH("LEFT")
        hint:SetTextColor(0.75, 0.7, 0.6)
        hint:SetText(placement.hint)
        radio:SetHitRectInsets(0, -label:GetStringWidth() - 4, 0, 0)
        radio:SetScript("OnClick", function()
            Settings().placement = placement.id
            Changed()
        end)
        radio.id = placement.id
        window.placements[i] = radio
    end

    local size = UI.Heading(content, "Size")
    size:SetPoint("TOPLEFT", 8, -70)
    window.sizes = {}
    for i, choice in ipairs(SIZES) do
        local radio = CreateFrame("CheckButton", nil, content, "UIRadioButtonTemplate")
        radio:SetPoint("TOPLEFT", 90 + (i - 1) * 90, -72)
        local label = radio:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
        label:SetPoint("LEFT", radio, "RIGHT", 2, 0)
        label:SetText(choice.label)
        radio:SetHitRectInsets(0, -label:GetStringWidth() - 4, 0, 0)
        radio:SetScript("OnClick", function()
            Settings().size = choice.id
            Changed()
        end)
        radio.id = choice.id
        window.sizes[i] = radio
    end

    local orders = UI.Heading(content, "Orders in the ring")
    orders:SetPoint("TOPLEFT", 8, -104)
    window.count = content:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    window.count:SetPoint("TOPRIGHT", -8, -108)
    window.orders = {}
    for i, order in ipairs(LA.Orders.catalog) do
        local button = CreateFrame("CheckButton", "LivingAzerothRingChoice" .. i, content, "ActionButtonTemplate")
        local column, row = (i - 1) % 5, math.floor((i - 1) / 5)
        button:SetPoint("TOPLEFT", 22 + column * 82, -132 - row * 70)
        _G[button:GetName() .. "Icon"]:SetTexture(order.icon)
        button.slot = _G[button:GetName() .. "HotKey"]
        local label = button:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
        label:SetPoint("TOP", button, "BOTTOM", 0, -3)
        label:SetWidth(80)
        label:SetText(order.label)
        button:SetScript("OnClick", function()
            local list = Settings().orders
            for slot, id in ipairs(list) do
                if id == order.id then
                    if #list > 2 then
                        table.remove(list, slot)
                    end
                    Changed()
                    return
                end
            end
            if #list < LA.Orders.MAX_RING then
                table.insert(list, order.id)
            end
            Changed()
        end)
        button:SetScript("OnEnter", function(self)
            GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
            GameTooltip:AddLine(order.label, 1, 1, 1)
            GameTooltip:AddLine("Click to add to or take out of the ring.", 0.6, 0.6, 0.6)
            GameTooltip:Show()
        end)
        button:SetScript("OnLeave", GameTooltip_Hide)
        button.id = order.id
        window.orders[i] = button
    end

    window.always = UI.Check(content, "Order icons on every bot in a raid",
        "Otherwise only on bots that are not simply following you")
    window.always:SetPoint("TOPLEFT", 4, -284)
    window.always:SetScript("OnClick", function(self)
        LivingAzerothDB.iconsAlways = self:GetChecked() and true or false
        LA.Fire("bots:changed")
    end)

    window.move = UI.Button(window.footer, "Move frames", 120)
    window.move:SetPoint("LEFT", 0, 0)
    window.move:SetScript("OnClick", function()
        LA.unlocked = not LA.unlocked
        LA.Party.Unlock(LA.unlocked)
        LA.Raid.Unlock(LA.unlocked)
        RingSettings.Unlock(LA.unlocked)
        window.move:SetText(LA.unlocked and "Lock frames" or "Move frames")
    end)
    local okay = UI.Button(window.footer, OKAY, 100)
    okay:SetPoint("RIGHT", 0, 0)
    okay:SetScript("OnClick", function() window:Hide() end)
    return window
end

function RingSettings.Refresh()
    if not window or not window:IsShown() then
        return
    end
    local settings = Settings()
    for _, radio in ipairs(window.placements) do
        radio:SetChecked(radio.id == settings.placement)
    end
    for _, radio in ipairs(window.sizes) do
        radio:SetChecked(radio.id == settings.size)
    end
    for _, button in ipairs(window.orders) do
        local slot
        for i, id in ipairs(settings.orders) do
            if id == button.id then
                slot = i
            end
        end
        button:SetChecked(slot ~= nil)
        _G[button:GetName() .. "Icon"]:SetDesaturated(slot == nil)
        button.slot:SetText(slot or "")
    end
    window.count:SetText(#settings.orders .. " of " .. LA.Orders.MAX_RING .. " - their numbers are the keys")
    window.always:SetChecked(LivingAzerothDB.iconsAlways ~= false)
end

function RingSettings.Toggle()
    local frame = Window()
    LA.SetShown(frame, not frame:IsShown())
    RingSettings.Refresh()
end

-- The fixed spot, shown to drag while frames are unlocked.

local spot

local function Spot()
    if spot then
        return spot
    end
    spot = CreateFrame("Frame", nil, UIParent)
    spot:SetSize(180, 180)
    spot:SetFrameStrata("DIALOG")
    spot:SetClampedToScreen(true)
    local disc = spot:CreateTexture(nil, "BACKGROUND")
    disc:SetTexture("Interface\\CharacterFrame\\TempPortraitAlphaMask")
    disc:SetVertexColor(0.16, 0.12, 0.04, 0.55)
    disc:SetAllPoints()
    local text = spot:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    text:SetPoint("CENTER", 0, 6)
    text:SetText("Orders ring")
    local hint = spot:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    hint:SetPoint("TOP", text, "BOTTOM", 0, -2)
    hint:SetText("drag to place")
    spot:EnableMouse(true)
    spot:SetMovable(true)
    spot:RegisterForDrag("LeftButton")
    spot:SetScript("OnDragStart", spot.StartMoving)
    spot:SetScript("OnDragStop", function(self)
        self:StopMovingOrSizing()
        local x, y = self:GetCenter()
        Settings().x, Settings().y = x, y
    end)
    return spot
end

function RingSettings.Unlock(unlocked)
    local show = unlocked and Settings().placement == "fixed"
    if show then
        local frame = Spot()
        frame:ClearAllPoints()
        frame:SetPoint("CENTER", UIParent, "BOTTOMLEFT", Settings().x, Settings().y)
    end
    if spot or show then
        LA.SetShown(Spot(), show)
    end
end
