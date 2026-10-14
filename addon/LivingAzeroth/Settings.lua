-- SPDX-License-Identifier: GPL-2.0-or-later
-- The settings window, in two tabs. Orders ring: where the ring opens, its size, and which orders it holds (up to
-- eight, in ring order). Raid frames: how the groups are laid out, the cells' size, and what they show, with a
-- cell drawn as it will look. "Move frames" unlocks the party and raid frames and, with the ring at "a fixed
-- spot", shows the ring itself to drag.
local _, LA = ...

local Settings = {}
LA.Settings = Settings

local UI, Style = LA.UI, LA.Style

local PLACEMENTS = {
    { id = "beside", label = "Beside the frame", hint = "Next to the bot you clicked" },
    { id = "cursor", label = "At the mouse", hint = "Centred where you click" },
    { id = "fixed", label = "A fixed spot", hint = "Press Move frames to place it" },
}
local LAYOUTS = {
    { id = "columns", label = "Groups side by side", hint = "Each group a column" },
    { id = "rows", label = "Groups in rows", hint = "Each group a row" },
}
local WIDTH, HEIGHT = 500, 330
local LEFT = 214 -- the left panel's width
local TABS = 28 -- the tabs and the space under them

local window

local function Ring()
    return LivingAzerothDB.ring
end

local function Raid()
    return LivingAzerothDB.raid
end

local function RingChanged()
    LA.Ring.Relayout()
    Settings.Unlock(LA.unlocked)
    Settings.Refresh()
end

local function RaidChanged()
    LA.Raid.Apply()
    Settings.Refresh()
end

-- A labelled slider with its value on the right: "Width ...... 72".
local function SliderRow(parent, label, low, high, step, width, format, onChange)
    local row = CreateFrame("Frame", nil, parent)
    row:SetSize(width, 32)
    row.label = UI.Text(row, "label")
    row.label:SetPoint("TOPLEFT")
    row.label:SetText(label)
    row.value = UI.Text(row, "value")
    row.value:SetPoint("TOPRIGHT")
    row.value:SetJustifyH("RIGHT")
    row.slider = UI.Slider(row, low, high, step, width, function(value)
        row.value:SetText(string.format(format, value))
        onChange(value)
    end)
    row.slider:SetPoint("BOTTOMLEFT")
    function row:Set(value)
        self.slider:SetQuietly(value)
        self.value:SetText(string.format(format, value))
    end
    return row
end

-- Orders ring

local function BuildRing(page, width, height)
    local inner = LEFT - 20
    local left = UI.Inset(page)
    left:SetPoint("TOPLEFT")
    left:SetSize(LEFT, height)
    local stack = UI.Stack(left)
    local where = UI.Heading(left, "Where it opens")
    where:SetWidth(inner)
    stack:Add(where, 8, 10)
    window.placements = {}
    for i, placement in ipairs(PLACEMENTS) do
        local radio = UI.Radio(left, placement.label, placement.hint, inner)
        stack:Add(radio, i == 1 and 8 or 6, 10)
        radio:SetScript("OnClick", function()
            Ring().placement = placement.id
            RingChanged()
        end)
        radio.id = placement.id
        window.placements[i] = radio
    end
    local size = UI.Heading(left, "Size")
    size:SetWidth(inner)
    stack:Add(size, 12, 10)
    window.ringSize = UI.Slider(left, LA.Ring.MIN_SIZE * 100, LA.Ring.MAX_SIZE * 100, 5, inner, function(value)
        Ring().size = value / 100
        size:SetNote(value .. "%")
        RingChanged()
    end)
    window.ringSizeHeading = size
    stack:Add(window.ringSize, 10, 10)

    -- Which orders it holds.
    local right = UI.Inset(page)
    right:SetPoint("TOPRIGHT")
    local rightWidth = width - LEFT - 10
    right:SetSize(rightWidth, height)
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
            local list = Ring().orders
            for slot, id in ipairs(list) do
                if id == order.id then
                    if #list > 2 then
                        table.remove(list, slot)
                    end
                    RingChanged()
                    return
                end
            end
            if #list < LA.Orders.MAX_RING then
                table.insert(list, order.id)
            end
            RingChanged()
        end)
        UI.Tooltip(button, order.label, "Click to add it to the ring or take it out. The number is its key while " ..
            "the ring is open.")
        button.id = order.id
        window.orders[i] = button
    end
end

-- Raid frames

local function BuildRaid(page, width, height)
    local inner = LEFT - 20
    local left = UI.Inset(page)
    left:SetPoint("TOPLEFT")
    left:SetSize(LEFT, height)
    local stack = UI.Stack(left)
    local layout = UI.Heading(left, "Layout")
    layout:SetWidth(inner)
    stack:Add(layout, 8, 10)
    window.layouts = {}
    for i, choice in ipairs(LAYOUTS) do
        local radio = UI.Radio(left, choice.label, choice.hint, inner)
        stack:Add(radio, i == 1 and 8 or 6, 10)
        radio:SetScript("OnClick", function()
            Raid().layout = choice.id
            RaidChanged()
        end)
        radio.id = choice.id
        window.layouts[i] = radio
    end
    local show = UI.Heading(left, "Show")
    show:SetWidth(inner)
    stack:Add(show, 12, 10)
    window.power = UI.Check(left, "Power bars", "Mana, rage or energy under health", inner)
    stack:Add(window.power, 8, 10)
    window.power.onToggle = function(_, on)
        Raid().power = on
        RaidChanged()
    end
    window.always = UI.Check(left, "Every bot's order", "Otherwise bots simply following you show the bot mark",
        inner)
    stack:Add(window.always, 6, 10)
    window.always.onToggle = function(_, on)
        LivingAzerothDB.iconsAlways = on
        LA.Fire("bots:changed")
    end
    window.compact = UI.Check(left, "Hide other raid frames", "Those of the CompactRaidFrame addon", inner)
    stack:Add(window.compact, 6, 10)
    window.compact.onToggle = function(_, on)
        Raid().hideCompact = on
        Settings.Refresh()
        if not on then
            LA.Print("the other raid frames come back after a /reload.")
        end
    end

    local right = UI.Inset(page)
    right:SetPoint("TOPRIGHT")
    local rightWidth = width - LEFT - 10
    right:SetSize(rightWidth, height)
    local sizeHeading = UI.Heading(right, "Cell size")
    sizeHeading:SetPoint("TOPLEFT", 10, -8)
    sizeHeading:SetWidth(rightWidth - 20)
    window.cellWidth = SliderRow(right, "Width", LA.Raid.MIN_WIDTH, LA.Raid.MAX_WIDTH, 2, rightWidth - 20, "%d",
        function(value)
            Raid().width = value
            RaidChanged()
        end)
    window.cellWidth:SetPoint("TOPLEFT", sizeHeading, "BOTTOMLEFT", 0, -8)
    window.cellHeight = SliderRow(right, "Height", LA.Raid.MIN_HEIGHT, LA.Raid.MAX_HEIGHT, 2, rightWidth - 20, "%d",
        function(value)
            Raid().height = value
            RaidChanged()
        end)
    window.cellHeight:SetPoint("TOPLEFT", window.cellWidth, "BOTTOMLEFT", 0, -8)
    local looks = UI.Heading(right, "How a cell looks")
    looks:SetPoint("TOPLEFT", window.cellHeight, "BOTTOMLEFT", 0, -12)
    looks:SetWidth(rightWidth - 20)
    window.preview = LA.Raid.Preview(right)
    window.preview:SetPoint("TOP", looks, "BOTTOM", 0, -10)
end

local function Build()
    window = LA.Window.Panel("LivingAzerothSettings", WIDTH, HEIGHT, "Living Azeroth · Settings", "settings")
    window:SetFrameStrata("DIALOG")
    local width, height = LA.Window.BodySize(WIDTH, HEIGHT)
    local footer = UI.BUTTON_HEIGHT + 8
    local pageHeight = height - TABS - footer
    window.pages = {}
    window.tabs = UI.TopTabs(window.body, { "Orders ring", "Raid frames" }, function(index)
        Settings.Show(index)
    end)
    window.tabs:SetPoint("TOPLEFT")
    window.tabs:SetPoint("TOPRIGHT")
    for index, build in ipairs({ BuildRing, BuildRaid }) do
        local page = CreateFrame("Frame", nil, window.body)
        page:SetPoint("TOPLEFT", 0, -TABS)
        page:SetSize(width, pageHeight)
        build(page, width, pageHeight)
        window.pages[index] = page
    end

    window.move = UI.Button(window.body, "Move frames", 100, "plain", "move")
    window.move:SetPoint("BOTTOMLEFT")
    window.move:SetScript("OnClick", function()
        LA.SetUnlocked(not LA.unlocked)
    end)
    local done = UI.Button(window.body, "Done", 70)
    done:SetPoint("BOTTOMRIGHT")
    done:SetScript("OnClick", function() window:Hide() end)
end

function Settings.Refresh()
    if not window or not window:IsShown() then
        return
    end
    local ring = Ring()
    for _, radio in ipairs(window.placements) do
        radio:SetChecked(radio.id == ring.placement)
    end
    local percent = math.floor(ring.size * 100 + 0.5)
    window.ringSize:SetQuietly(percent)
    window.ringSizeHeading:SetNote(percent .. "%")
    for _, button in ipairs(window.orders) do
        local slot
        for i, id in ipairs(ring.orders) do
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
    window.count:SetText(#ring.orders .. " of " .. LA.Orders.MAX_RING)

    local raid = Raid()
    for _, radio in ipairs(window.layouts) do
        radio:SetChecked(radio.id == raid.layout)
    end
    window.power:SetChecked(raid.power)
    window.always:SetChecked(LivingAzerothDB.iconsAlways ~= false)
    window.compact:SetChecked(raid.hideCompact)
    window.cellWidth:Set(raid.width)
    window.cellHeight:Set(raid.height)
    window.preview:Refresh()

    window.move:SetText(LA.unlocked and "Lock frames" or "Move frames")
    window.move.glyphIcon:Set(LA.unlocked and "lock" or "move", Style.gold)
end

-- Shows the window at a tab (the last one shown, at first the ring's).
function Settings.Show(index)
    if not window then
        Build()
    end
    index = index or LivingAzerothDB.settingsTab or 1
    LivingAzerothDB.settingsTab = index
    window.tabs:Select(index)
    for i, page in ipairs(window.pages) do
        LA.SetShown(page, i == index)
    end
    window:Show()
    Settings.Refresh()
end

function Settings.Toggle()
    if window and window:IsShown() then
        window:Hide()
    else
        Settings.Show()
    end
end

-- While frames are unlocked and the ring opens at a fixed spot, the ring itself shows there to be dragged.
function Settings.Unlock(unlocked)
    LA.Ring.Preview(unlocked and Ring().placement == "fixed")
end
