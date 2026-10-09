-- SPDX-License-Identifier: GPL-2.0-or-later
-- The orders ring's settings: where it opens, its size, and which orders it holds (up to eight, in ring order).
-- With "a fixed spot", unlocking frames shows the spot to drag.
local _, LA = ...

local RingSettings = {}
LA.RingSettings = RingSettings

local PLACEMENTS = {
    { id = "beside", label = "Beside the frame", hint = "Next to the bot's frame" },
    { id = "cursor", label = "At the mouse", hint = "Centred where you click" },
    { id = "fixed", label = "A fixed spot", hint = "Drag it after /la unlock" },
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
    window = CreateFrame("Frame", "LivingAzerothRingSettings", UIParent)
    window:SetSize(470, 360)
    window:SetPoint("CENTER", 0, 60)
    window:SetFrameStrata("DIALOG")
    window:SetToplevel(true)
    window:EnableMouse(true)
    window:SetMovable(true)
    window:RegisterForDrag("LeftButton")
    window:SetScript("OnDragStart", window.StartMoving)
    window:SetScript("OnDragStop", window.StopMovingOrSizing)
    window:SetBackdrop({ bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
        edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border", tile = true, tileSize = 32, edgeSize = 32,
        insets = { left = 11, right = 12, top = 12, bottom = 11 } })
    table.insert(UISpecialFrames, "LivingAzerothRingSettings")
    window:Hide()

    local header = window:CreateTexture(nil, "ARTWORK")
    header:SetTexture("Interface\\DialogFrame\\UI-DialogBox-Header")
    header:SetSize(256, 64)
    header:SetPoint("TOP", 0, 12)
    local title = window:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    title:SetPoint("TOP", header, "TOP", 0, -14)
    title:SetText("Orders ring")
    local close = CreateFrame("Button", nil, window, "UIPanelCloseButton")
    close:SetPoint("TOPRIGHT", -6, -6)

    local function Heading(text, y)
        local heading = window:CreateFontString(nil, "OVERLAY", "GameFontNormal")
        heading:SetPoint("TOPLEFT", 22, y)
        heading:SetText(text)
        return heading
    end

    Heading("Where it opens", -30)
    window.placements = {}
    for i, placement in ipairs(PLACEMENTS) do
        local radio = CreateFrame("CheckButton", nil, window, "UIRadioButtonTemplate")
        radio:SetPoint("TOPLEFT", 24 + (i - 1) * 146, -50)
        local label = radio:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
        label:SetPoint("LEFT", radio, "RIGHT", 2, 0)
        label:SetText(placement.label)
        local hint = radio:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
        hint:SetPoint("TOPLEFT", label, "BOTTOMLEFT", 0, -2)
        hint:SetWidth(128)
        hint:SetJustifyH("LEFT")
        hint:SetText(placement.hint)
        radio:SetScript("OnClick", function()
            Settings().placement = placement.id
            Changed()
        end)
        radio.id = placement.id
        window.placements[i] = radio
    end

    Heading("Size", -92)
    window.sizes = {}
    for i, size in ipairs(SIZES) do
        local radio = CreateFrame("CheckButton", nil, window, "UIRadioButtonTemplate")
        radio:SetPoint("TOPLEFT", 24 + (i - 1) * 90, -112)
        local label = radio:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
        label:SetPoint("LEFT", radio, "RIGHT", 2, 0)
        label:SetText(size.label)
        radio:SetScript("OnClick", function()
            Settings().size = size.id
            Changed()
        end)
        radio.id = size.id
        window.sizes[i] = radio
    end

    Heading("Orders in the ring", -142)
    window.count = window:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
    window.count:SetPoint("TOPRIGHT", -24, -144)
    window.orders = {}
    for i, order in ipairs(LA.Orders.catalog) do
        local button = CreateFrame("CheckButton", "LivingAzerothRingChoice" .. i, window, "ActionButtonTemplate")
        local column, row = (i - 1) % 5, math.floor((i - 1) / 5)
        button:SetPoint("TOPLEFT", 36 + column * 84, -170 - row * 70)
        _G[button:GetName() .. "Icon"]:SetTexture(order.icon)
        button.slot = _G[button:GetName() .. "HotKey"]
        local label = button:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
        label:SetPoint("TOP", button, "BOTTOM", 0, -3)
        label:SetWidth(76)
        label:SetText(order.label)
        button:SetScript("OnClick", function()
            local orders = Settings().orders
            for slot, id in ipairs(orders) do
                if id == order.id then
                    if #orders > 2 then
                        table.remove(orders, slot)
                    end
                    Changed()
                    return
                end
            end
            if #orders < LA.Orders.MAX_RING then
                table.insert(orders, order.id)
            end
            Changed()
        end)
        button:SetScript("OnEnter", function(self)
            GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
            GameTooltip:AddLine(order.label, 1, 1, 1)
            GameTooltip:AddLine("Click to add to or take out of the ring.", 0.6, 0.6, 0.6)
            GameTooltip:Show()
        end)
        button:SetScript("OnLeave", function()
            GameTooltip:Hide()
        end)
        button.id = order.id
        window.orders[i] = button
    end

    local okay = CreateFrame("Button", nil, window, "UIPanelButtonTemplate")
    okay:SetSize(90, 22)
    okay:SetPoint("BOTTOMRIGHT", -20, 16)
    okay:SetText(OKAY)
    okay:SetScript("OnClick", function()
        window:Hide()
    end)
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
