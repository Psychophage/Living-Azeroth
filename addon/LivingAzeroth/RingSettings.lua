-- SPDX-License-Identifier: GPL-2.0-or-later
-- The orders ring's settings: where it opens, its size, and which orders it holds (up to eight, in ring order).
-- "Move frames" unlocks the party and raid frames and, with "a fixed spot", shows the ring itself to drag.
local _, LA = ...

local RingSettings = {}
LA.RingSettings = RingSettings

local PLACEMENTS = {
    { id = "beside", label = "Beside the frame", hint = "Next to the bot you clicked" },
    { id = "cursor", label = "At the mouse", hint = "Centred where you click" },
    { id = "fixed", label = "A fixed spot", hint = "Press Move frames to place it" },
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
    RingSettings.Unlock(LA.unlocked)
    RingSettings.Refresh()
end

local function ShowPart(index)
    LA.SetShown(window.where, index == 1)
    LA.SetShown(window.orderPart, index == 2)
end

local function Window()
    if window then
        return window
    end
    local UI = LA.UI
    window = LA.Window.Finder("LivingAzerothRingSettings", "Orders ring", "Interface\\Icons\\Ability_Tracking")
    window:SetFrameStrata("DIALOG")

    local intro = UI.Text(window.header, 250)
    intro:SetPoint("TOPLEFT", 58, -10)
    intro:SetText("Click a bot's order icon to open its ring. Choose where it opens, how big it is, and what it " ..
        "holds.")

    window.parts = UI.SubTabs(window.bar, { "Where it opens", "Orders" }, ShowPart)

    -- Where it opens, and its size.
    window.where = CreateFrame("Frame", nil, window.content)
    window.where:SetAllPoints()
    local whereTitle = UI.Dark(window.where, "title", 290)
    whereTitle:SetPoint("TOPLEFT", 8, -8)
    whereTitle:SetText("Where it opens")
    window.placements = {}
    for i, placement in ipairs(PLACEMENTS) do
        local radio = UI.Radio(window.where, placement.label)
        radio:SetPoint("TOPLEFT", 12, -32 - (i - 1) * 34)
        local hint = UI.Dark(window.where, "small", 240)
        hint:SetPoint("TOPLEFT", radio.label, "BOTTOMLEFT", 0, -1)
        hint:SetText(placement.hint)
        radio:SetScript("OnClick", function()
            Settings().placement = placement.id
            Changed()
        end)
        radio.id = placement.id
        window.placements[i] = radio
    end
    local sizeTitle = UI.Dark(window.where, "title", 290)
    sizeTitle:SetPoint("TOPLEFT", 8, -140)
    sizeTitle:SetText("Size")
    window.sizes = {}
    for i, choice in ipairs(SIZES) do
        local radio = UI.Radio(window.where, choice.label)
        radio:SetPoint("TOPLEFT", 12 + (i - 1) * 96, -164)
        radio:SetScript("OnClick", function()
            Settings().size = choice.id
            Changed()
        end)
        radio.id = choice.id
        window.sizes[i] = radio
    end
    window.always = UI.Check(window.where, "Order icons on every bot in a raid",
        "Otherwise only on bots not simply following you", true)
    window.always:SetPoint("TOPLEFT", 8, -194)
    window.always:SetScript("OnClick", function(self)
        LivingAzerothDB.iconsAlways = self:GetChecked() and true or false
        LA.Fire("bots:changed")
    end)

    -- Which orders it holds.
    window.orderPart = CreateFrame("Frame", nil, window.content)
    window.orderPart:SetAllPoints()
    local ordersTitle = UI.Dark(window.orderPart, "title", 200)
    ordersTitle:SetPoint("TOPLEFT", 8, -8)
    ordersTitle:SetText("Orders in the ring")
    window.count = UI.Dark(window.orderPart, "small", 120)
    window.count:SetPoint("TOPRIGHT", -8, -12)
    window.count:SetJustifyH("RIGHT")
    window.orders = {}
    for i, order in ipairs(LA.Orders.catalog) do
        local button = CreateFrame("CheckButton", "LivingAzerothRingChoice" .. i, window.orderPart,
            "ActionButtonTemplate")
        local column, row = (i - 1) % 4, math.floor((i - 1) / 4)
        button:SetPoint("TOPLEFT", 22 + column * 76, -34 - row * 70)
        _G[button:GetName() .. "Icon"]:SetTexture(order.icon)
        button.slot = _G[button:GetName() .. "HotKey"]
        local label = UI.Dark(window.orderPart, "small", 74)
        label:SetPoint("TOP", button, "BOTTOM", 0, -2)
        label:SetJustifyH("CENTER")
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
            GameTooltip:AddLine("Click to add it to the ring or take it out.", nil, nil, nil, true)
            GameTooltip:Show()
        end)
        button:SetScript("OnLeave", GameTooltip_Hide)
        button.id = order.id
        window.orders[i] = button
    end

    window.move = UI.Button(window.footer, "Move frames", 110)
    window.move:SetPoint("LEFT", 0, 1)
    window.move:SetScript("OnClick", function()
        LA.SetUnlocked(not LA.unlocked)
    end)
    local okay = UI.Button(window.footer, OKAY, 80)
    okay:SetPoint("RIGHT", -8, 1)
    okay:SetScript("OnClick", function() window:Hide() end)
    window.parts:Select(1)
    ShowPart(1)
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
    window.count:SetText(#settings.orders .. " of " .. LA.Orders.MAX_RING .. "; numbers are keys")
    window.always:SetChecked(LivingAzerothDB.iconsAlways ~= false)
    window.move:SetText(LA.unlocked and "Lock frames" or "Move frames")
end

function RingSettings.Toggle()
    local frame = Window()
    LA.SetShown(frame, not frame:IsShown())
    RingSettings.Refresh()
end

-- While frames are unlocked and the ring opens at a fixed spot, the ring itself shows there to be dragged.
function RingSettings.Unlock(unlocked)
    LA.Ring.Preview(unlocked and Settings().placement == "fixed")
end
