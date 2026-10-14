-- SPDX-License-Identifier: GPL-2.0-or-later
-- The orders ring's settings: where it opens, its size, and which orders it holds (up to eight, in ring order).
-- "Move frames" unlocks the party and raid frames and, with "a fixed spot", shows the ring itself to drag.
local _, LA = ...

local RingSettings = {}
LA.RingSettings = RingSettings

local UI, Style = LA.UI, LA.Style

local PLACEMENTS = {
    { id = "beside", label = "Beside the frame", hint = "Next to the bot you clicked" },
    { id = "cursor", label = "At the mouse", hint = "Centred where you click" },
    { id = "fixed", label = "A fixed spot", hint = "Press Move frames to place it" },
}
local SIZES = {
    { id = "small", label = "Small" }, { id = "normal", label = "Normal" }, { id = "large", label = "Large" },
}
local WIDTH, HEIGHT = 480, 300

local window

local function Settings()
    return LivingAzerothDB.ring
end

local function Changed()
    LA.Ring.Relayout()
    RingSettings.Unlock(LA.unlocked)
    RingSettings.Refresh()
end

local function Window()
    if window then
        return window
    end
    window = LA.Window.Panel("LivingAzerothRingSettings", WIDTH, HEIGHT, "Orders ring", "settings")
    window:SetFrameStrata("DIALOG")
    local width, height = LA.Window.BodySize(WIDTH, HEIGHT)
    local footer = UI.BUTTON_HEIGHT + 8
    local leftWidth = 210
    local inner = leftWidth - 20

    -- Where it opens, its size, and the raid icons.
    local left = UI.Inset(window.body)
    left:SetPoint("TOPLEFT")
    left:SetSize(leftWidth, height - footer)
    local stack = UI.Stack(left)
    local where = UI.Heading(left, "Where it opens")
    where:SetWidth(inner)
    stack:Add(where, 8, 10)
    window.placements = {}
    for i, placement in ipairs(PLACEMENTS) do
        local radio = UI.Radio(left, placement.label, placement.hint, inner)
        stack:Add(radio, i == 1 and 8 or 6, 10)
        radio:SetScript("OnClick", function()
            Settings().placement = placement.id
            Changed()
        end)
        radio.id = placement.id
        window.placements[i] = radio
    end
    local size = UI.Heading(left, "Size")
    size:SetWidth(inner)
    stack:Add(size, 12, 10)
    window.sizes = UI.Segment(left, SIZES, inner, function(id)
        Settings().size = id
        Changed()
    end)
    stack:Add(window.sizes, 8, 10)
    window.always = UI.Check(left, "Order icons on every bot in a raid",
        "Otherwise only on bots not simply following you", inner)
    stack:Add(window.always, 12, 10)
    window.always.onToggle = function(_, on)
        LivingAzerothDB.iconsAlways = on
        LA.Fire("bots:changed")
    end

    -- Which orders it holds.
    local right = UI.Inset(window.body)
    right:SetPoint("TOPRIGHT")
    local rightWidth = width - leftWidth - 10
    right:SetSize(rightWidth, height - footer)
    local ordersTitle = UI.Heading(right, "Orders in the ring")
    ordersTitle:SetPoint("TOPLEFT", 10, -8)
    ordersTitle:SetWidth(rightWidth - 20)
    window.count = ordersTitle.note
    local columns, gap = 4, 4
    local cell = math.floor((rightWidth - 20 - (columns - 1) * gap) / columns)
    window.orders = {}
    for i, order in ipairs(LA.Orders.catalog) do
        local button = CreateFrame("Button", nil, right)
        button:SetSize(cell, 48)
        local column, row = (i - 1) % columns, math.floor((i - 1) / columns)
        button:SetPoint("TOPLEFT", 10 + column * (cell + gap), -32 - row * (48 + gap))
        UI.Surface(button, "row")
        UI.HoverLight(button)
        button.tile = UI.Icon(button, 24, "tile")
        button.tile:SetPoint("TOP", 0, -4)
        button.tile:Set(order.glyph, order.tone)
        button.slot = UI.Text(button.tile, "small")
        button.slot:SetPoint("TOPRIGHT", -1, 0)
        button.slot:SetTextColor(0.95, 0.95, 0.95)
        button.label = UI.Text(button, "small", cell - 4)
        button.label:SetJustifyH("CENTER")
        button.label:SetWordWrap(false)
        button.label:SetPoint("BOTTOM", 0, 4)
        button.label:SetText(order.short)
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
            GameTooltip:AddLine(order.label, 1, 0.82, 0)
            GameTooltip:AddLine("Click to add it to the ring or take it out. The number is its key while the " ..
                "ring is open.", 1, 1, 1, true)
            GameTooltip:Show()
        end)
        button:SetScript("OnLeave", GameTooltip_Hide)
        button.id = order.id
        window.orders[i] = button
    end

    window.move = UI.Button(window.body, "Move frames", 100, "plain", "move")
    window.move:SetPoint("BOTTOMLEFT")
    window.move:SetScript("OnClick", function()
        LA.SetUnlocked(not LA.unlocked)
    end)
    local done = UI.Button(window.body, "Done", 70)
    done:SetPoint("BOTTOMRIGHT")
    done:SetScript("OnClick", function() window:Hide() end)
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
    window.sizes:Select(settings.size)
    for _, button in ipairs(window.orders) do
        local slot
        for i, id in ipairs(settings.orders) do
            if id == button.id then
                slot = i
            end
        end
        button.surface:Set(slot and "rowOn" or "row")
        button.tile:SetDim(slot == nil)
        button.slot:SetText(slot or "")
        local colour = slot and Style.gold or Style.T().muted
        button.label:SetTextColor(colour[1], colour[2], colour[3])
    end
    window.count:SetText(#settings.orders .. " of " .. LA.Orders.MAX_RING)
    window.always:SetChecked(LivingAzerothDB.iconsAlways ~= false)
    window.move:SetText(LA.unlocked and "Lock frames" or "Move frames")
    window.move.glyphIcon:Set(LA.unlocked and "lock" or "move", Style.gold)
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
