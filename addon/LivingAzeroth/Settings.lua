-- SPDX-License-Identifier: GPL-2.0-or-later
-- The settings window, in four tabs. Orders ring: where it opens, its size, its layout and how the movement orders
-- show, and which orders it holds (up to eight), with the ring itself drawn as it will look. Raid frames: three parts (Layout, Look, Sizes) beside a small group drawn as it will look. All my bots:
-- that button's look and size. Trading: the bot bags window, and for game masters, trading freely with any bot.
-- "Move frames" unlocks the party and raid frames and the "All my bots" button and, with the ring at "a fixed spot",
-- shows the ring itself to drag.
local _, LA = ...

local Settings = {}
LA.Settings = Settings

local UI, Style = LA.UI, LA.Style

local PLACEMENTS = {
    { id = "beside", label = "Beside the frame", hint = "Next to the bot you clicked" },
    { id = "cursor", label = "At the mouse", hint = "Centred where you click" },
    { id = "fixed", label = "A fixed spot", hint = "Press Move frames to place it" },
}
-- The ring's layout and its movement orders, picked from chips with a line saying what the choice does.
local RING_CHOICES = {
    { key = "layout", label = "Layout", options = {
        { id = "classic", label = "Classic", hint = "Every order a part of the ring, in your order." },
        { id = "kinds", label = "By kind", hint = "Where they are, then things to do, then switches, each " ..
            "switch with a light that shows whether it is on." },
        { id = "two", label = "Two rings", hint = "What the bots are doing on an inner ring, things to do on an " ..
            "outer ring." } } },
    { key = "movement", label = "Moving", options = {
        { id = "toggle", label = "Toggle", hint = "Follow and Stay share one part: a click switches between them." },
        { id = "cycle", label = "Step", hint = "Follow, Stay and Guard share one part: a click moves on to the " ..
            "next, a right-click back." },
        { id = "separate", label = "Separate", hint = "Follow, Stay and Guard are parts of their own." } } },
}
local LAYOUTS = {
    { id = "columns", label = "Groups side by side", hint = "Each group a column" },
    { id = "rows", label = "Groups in rows", hint = "Each group a row" },
}
-- The raid cells' look, picked from rows of chips.
local LOOKS = {
    { key = "bars", label = "Bars", options = {
        { id = "gradient", label = "Gradient" }, { id = "smooth", label = "Smooth" }, { id = "glossy", label = "Glossy" },
        { id = "flat", label = "Flat" } } },
    { key = "health", label = "Health", options = {
        { id = "class", label = "Class" }, { id = "dark", label = "Dark" }, { id = "green", label = "Green" } } },
    { key = "names", label = "Names", options = { { id = "white", label = "White" }, { id = "class", label = "Class" } } },
    { key = "roles", label = "Roles", options = {
        { id = "some", label = "Tank, healer" }, { id = "all", label = "All" }, { id = "none", label = "None" } } },
}
-- The raid sizes, in two columns: { key, label, step }.
local CELL_SIZES = {
    { "width", "Width", 2 }, { "height", "Height", 2 }, { "gap", "Between cells", 1 },
    { "groupGap", "Between groups", 1 }, { "powerHeight", "Power bar (pixels)", 1 },
}
local PART_SIZES = {
    { "font", "Names", 1 }, { "status", "Health text", 1 }, { "label", "Group names", 1 }, { "role", "Role icons", 1 },
    { "mark", "Order marks", 1 },
}
local RAID_PARTS = { { id = 1, label = "Layout" }, { id = 2, label = "Look" }, { id = 3, label = "Sizes" } }
local ALL_BOTS_LOOKS = { { id = "both", label = "Medal and name" }, { id = "medal", label = "Medal" },
    { id = "name", label = "Name" } }
local WIDTH, HEIGHT = 620, 540
local LEFT = 230 -- the left panel's width on the ring and All my bots tabs
local PREVIEW = 200 -- the raid preview's panel width
local TABS = 28 -- the tabs and the space under them
local RING_TAB = 1
local RING_ROOM = 196 -- the space for the ring on its tab

local window

local function Ring()
    return LivingAzerothDB.ring
end

local function Raid()
    return LivingAzerothDB.raid
end

-- The ring shows in its space on its tab while that is open; while frames are unlocked with the ring at a fixed spot,
-- it shows there to be dragged instead.
local function RingPreview()
    if LA.unlocked and Ring().placement == "fixed" then
        LA.Ring.Preview(true)
    elseif window and window:IsShown() and window.tabs.selected == RING_TAB then
        LA.Ring.Preview(true, window.ringRoom)
    else
        LA.Ring.Preview(false)
    end
end

local function RingChanged()
    LA.Ring.Relayout()
    RingPreview()
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

-- A row of chips with a label on its left: "Bars [Gradient] [Smooth] ...".
local function ChipRow(parent, label, options, width, labelWidth, onPick)
    local row = CreateFrame("Frame", nil, parent)
    row:SetSize(width, UI.BUTTON_HEIGHT)
    local text = UI.Text(row, "label")
    text:SetPoint("LEFT")
    text:SetText(label)
    row.segment = UI.Segment(row, options, width - labelWidth, onPick)
    row.segment:SetPoint("LEFT", labelWidth, 0)
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
    local look = UI.Heading(left, "How it looks")
    look:SetWidth(inner)
    stack:Add(look, 14, 10)
    window.ringChoices = {}
    for i, choice in ipairs(RING_CHOICES) do
        local row = ChipRow(left, choice.label, choice.options, inner, 52, function(id)
            Ring()[choice.key] = id
            RingChanged()
        end)
        stack:Add(row, i == 1 and 8 or 6, 10)
        row.hint = UI.Text(left, "small", inner)
        stack:Add(row.hint, 4, 10, 26)
        row.choice = choice
        window.ringChoices[i] = row
    end

    -- The ring as it will look, and which orders it holds.
    local right = UI.Inset(page)
    right:SetPoint("TOPRIGHT")
    local rightWidth = width - LEFT - 10
    right:SetSize(rightWidth, height)
    local lookTitle = UI.Heading(right, "As it will look")
    lookTitle:SetPoint("TOPLEFT", 10, -8)
    lookTitle:SetWidth(rightWidth - 20)
    window.ringRoom = CreateFrame("Frame", nil, right)
    window.ringRoom:SetPoint("TOP", lookTitle, "BOTTOM", 0, -4)
    window.ringRoom:SetSize(rightWidth - 20, RING_ROOM)
    local ordersTitle = UI.Heading(right, "Orders in the ring")
    ordersTitle:SetPoint("TOPLEFT", lookTitle, "BOTTOMLEFT", 0, -RING_ROOM - 12)
    ordersTitle:SetWidth(rightWidth - 20)
    window.count = ordersTitle.note
    local columns, gap = 4, 4
    local cell = math.floor((rightWidth - 20 - (columns - 1) * gap) / columns)
    window.orders = {}
    local choices = {}
    for _, order in ipairs(LA.Orders.catalog) do
        if not order.automatic then
            choices[#choices + 1] = order
        end
    end
    for i, order in ipairs(choices) do
        local button = CreateFrame("Button", nil, right)
        button:SetSize(cell, 48)
        local column, row = (i - 1) % columns, math.floor((i - 1) / columns)
        button:SetPoint("TOPLEFT", ordersTitle, "BOTTOMLEFT", column * (cell + gap), -8 - row * (48 + gap))
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
        UI.Tooltip(button, order.label, "Click to add it to the ring or take it out. The number is its place in " ..
            "your order.")
        button.id = order.id
        window.orders[i] = button
    end
    local note = UI.Text(right, "small", rightWidth - 20)
    note:SetPoint("BOTTOMLEFT", 10, 10)
    note:SetText("While the ring is open, the number keys give its orders, in the order shown on the ring.")
end

-- Raid frames: Layout, Look and Sizes, one at a time on the left, with the preview on the right.

local function RaidLayout(panel, width)
    local stack = UI.Stack(panel)
    local layout = UI.Heading(panel, "Groups")
    layout:SetWidth(width)
    stack:Add(layout, 8, 10)
    window.layouts = {}
    for i, choice in ipairs(LAYOUTS) do
        local radio = UI.Radio(panel, choice.label, choice.hint, width)
        stack:Add(radio, i == 1 and 8 or 6, 10)
        radio:SetScript("OnClick", function()
            Raid().layout = choice.id
            RaidChanged()
        end)
        radio.id = choice.id
        window.layouts[i] = radio
    end
    local show = UI.Heading(panel, "Show")
    show:SetWidth(width)
    stack:Add(show, 12, 10)
    window.power = UI.Check(panel, "Power bars", "Mana, rage or energy under health", width)
    stack:Add(window.power, 8, 10)
    window.power.onToggle = function(_, on)
        Raid().power = on
        RaidChanged()
    end
    window.always = UI.Check(panel, "Every bot's order", "Otherwise bots simply following you show the bot mark",
        width)
    stack:Add(window.always, 6, 10)
    window.always.onToggle = function(_, on)
        LivingAzerothDB.iconsAlways = on
        LA.Fire("bots:changed")
        Settings.Refresh()
    end
    window.labels = UI.Check(panel, "Group names", "Click one for that group's orders", width)
    stack:Add(window.labels, 6, 10)
    window.labels.onToggle = function(_, on)
        Raid().labels = on
        RaidChanged()
    end
    window.compact = UI.Check(panel, "Hide other raid frames", "Those of the CompactRaidFrame addon", width)
    stack:Add(window.compact, 6, 10)
    window.compact.onToggle = function(_, on)
        Raid().hideCompact = on
        Settings.Refresh()
        if not on then
            LA.Print("the other raid frames come back after a /reload.")
        end
    end
end

local function RaidLook(panel, width)
    local heading = UI.Heading(panel, "Look")
    heading:SetPoint("TOPLEFT", 10, -8)
    heading:SetWidth(width)
    window.looks = {}
    local above = heading
    for i, look in ipairs(LOOKS) do
        local row = ChipRow(panel, look.label, look.options, width, 50, function(id)
            Raid()[look.key] = id
            RaidChanged()
        end)
        row:SetPoint("TOPLEFT", above, "BOTTOMLEFT", 0, i == 1 and -10 or -8)
        row.key = look.key
        window.looks[i] = row
        above = row
    end
    UI.Tooltip(window.looks[1].segment.chips[1], "Gradient", "Dark at the left, full colour where a full bar ends, " ..
        "with dark rims: the look of DragonUI's party bars.")
end

local function SizeColumn(panel, title, sizes, x, width)
    local heading = UI.Heading(panel, title)
    heading:SetPoint("TOPLEFT", x, -8)
    heading:SetWidth(width)
    local above = heading
    for i, size in ipairs(sizes) do
        local key = size[1]
        local range = LA.Raid.SIZES[key]
        local row = SliderRow(panel, size[2], range[1], range[2], size[3], width, "%d", function(value)
            Raid()[key] = value
            RaidChanged()
        end)
        row:SetPoint("TOPLEFT", above, "BOTTOMLEFT", 0, i == 1 and -8 or -6)
        row.key = key
        window.sizes[#window.sizes + 1] = row
        above = row
    end
end

local function RaidSizes(panel, width)
    window.sizes = {}
    local column = math.floor((width - 14) / 2)
    SizeColumn(panel, "Cells", CELL_SIZES, 10, column)
    SizeColumn(panel, "Text and marks", PART_SIZES, 10 + column + 14, column)
    local reset = UI.Button(panel, "Default sizes", 110, "plain")
    reset:SetPoint("BOTTOMRIGHT", -10, 10)
    reset:SetScript("OnClick", function()
        for key, range in pairs(LA.Raid.SIZES) do
            Raid()[key] = range[3]
        end
        RaidChanged()
    end)
end

local function ShowRaidPart(index)
    LivingAzerothDB.raidPart = index
    window.raidParts:Select(index)
    for i, panel in ipairs(window.raidPanels) do
        LA.SetShown(panel, i == index)
    end
end

local function BuildRaid(page, width, height)
    local leftWidth = width - PREVIEW - 10
    local left = UI.Inset(page)
    left:SetPoint("TOPLEFT")
    left:SetSize(leftWidth, height)
    window.raidParts = UI.Segment(left, RAID_PARTS, leftWidth - 20, ShowRaidPart)
    window.raidParts:SetPoint("TOPLEFT", 10, -10)
    window.raidPanels = {}
    for i, build in ipairs({ RaidLayout, RaidLook, RaidSizes }) do
        local panel = CreateFrame("Frame", nil, left)
        panel:SetPoint("TOPLEFT", 0, -(UI.BUTTON_HEIGHT + 14))
        panel:SetPoint("BOTTOMRIGHT")
        build(panel, leftWidth - 20)
        window.raidPanels[i] = panel
    end

    local right = UI.Inset(page)
    right:SetPoint("TOPRIGHT")
    right:SetSize(PREVIEW, height)
    local heading = UI.Heading(right, "As it will look")
    heading:SetPoint("TOPLEFT", 10, -8)
    heading:SetWidth(PREVIEW - 20)
    local room = CreateFrame("Frame", nil, right)
    room:SetPoint("TOPLEFT", 10, -40)
    room:SetPoint("BOTTOMRIGHT", -10, 10)
    window.raidPreview = LA.Raid.Preview(room, PREVIEW - 20, height - 50)
    window.raidPreview:SetPoint("TOP")
end

-- All my bots

local function AllBotsChanged()
    LA.AllBots.Unlock(LA.unlocked)
    Settings.Refresh()
end

local function BuildAllBots(page, width, height)
    local inner = LEFT - 20
    local left = UI.Inset(page)
    left:SetPoint("TOPLEFT")
    left:SetSize(LEFT, height)
    local stack = UI.Stack(left)
    local show = UI.Heading(left, "\"All my bots\"")
    show:SetWidth(inner)
    stack:Add(show, 8, 10)
    window.allBots = UI.Check(left, "Show the button", "Orders for every bot of yours in the group; place it " ..
        "with Move frames", inner)
    stack:Add(window.allBots, 8, 10)
    window.allBots.onToggle = function(_, on)
        LivingAzerothDB.allBots.shown = on
        AllBotsChanged()
    end
    local look = UI.Heading(left, "Look")
    look:SetWidth(inner)
    stack:Add(look, 12, 10)
    window.allBotsLooks = {}
    for i, choice in ipairs(ALL_BOTS_LOOKS) do
        local radio = UI.Radio(left, choice.label, nil, inner)
        stack:Add(radio, i == 1 and 8 or 6, 10)
        radio:SetScript("OnClick", function()
            LivingAzerothDB.allBots.look = choice.id
            AllBotsChanged()
        end)
        radio.id = choice.id
        window.allBotsLooks[i] = radio
    end
    local range = LA.AllBots.SIZE
    window.allBotsSize = SliderRow(left, "Size", range[1], range[2], 1, inner, "%d", function(value)
        LivingAzerothDB.allBots.size = value
        AllBotsChanged()
    end)
    stack:Add(window.allBotsSize, 12, 10)

    local right = UI.Inset(page)
    right:SetPoint("TOPRIGHT")
    local rightWidth = width - LEFT - 10
    right:SetSize(rightWidth, height)
    local preview = UI.Heading(right, "As it will look")
    preview:SetPoint("TOPLEFT", 10, -8)
    preview:SetWidth(rightWidth - 20)
    window.allBotsPreview = LA.AllBots.Preview(right)
    window.allBotsPreview:SetPoint("TOP", preview, "BOTTOM", 0, -16)
end

-- Trading

local function SetTradeFreely(on)
    LA.Bridge.Request({ op = "trade_freely", on = on }, function(reply)
        if reply.ok then
            LivingAzerothDB.tradeFreely = reply.trading_freely
            LA.Bridge.trade.trading_freely = reply.trading_freely
        else
            LA.Print("trading freely needs a game master account.")
        end
        Settings.Refresh()
    end)
end

local function BuildTrade(page, width, height)
    local panel = UI.Inset(page)
    panel:SetPoint("TOPLEFT")
    panel:SetSize(width, height)
    local inner = width - 20
    local stack = UI.Stack(panel)
    local heading = UI.Heading(panel, "Trading with bots")
    heading:SetWidth(inner)
    stack:Add(heading, 8, 10)
    window.tradeBags = UI.Check(panel, "Show a bot's bags when trading",
        "Click its items to have them put in the trade. Your own characters give everything; other bots give " ..
        "what they got while grouped with you, and random bots sell the rest (\"Pay\" puts their price in).", inner)
    stack:Add(window.tradeBags, 8, 10)
    window.tradeBags.onToggle = function(_, on)
        LivingAzerothDB.tradeBags = on
    end
    window.tradeMissing = UI.Text(panel, "small", inner)
    window.tradeMissing:SetText("This server doesn't show bots' bags.")
    stack:Add(window.tradeMissing, 8, 10)
    window.gmHeading = UI.Heading(panel, "Game master")
    window.gmHeading:SetWidth(inner)
    stack:Add(window.gmHeading, 16, 10)
    window.tradeFreely = UI.Check(panel, "Trade freely with any bot",
        "Everything any bot carries can be taken, and its gold. For testing; only game master accounts have this.",
        inner)
    stack:Add(window.tradeFreely, 8, 10)
    window.tradeFreely.onToggle = function(_, on)
        SetTradeFreely(on)
    end
end

-- Once connected, a game master's choice to trade freely is told to the server again (it forgets at logout).
LA.On("bridge:state", function(state)
    local trade = LA.Bridge.trade
    if state == "ready" and trade and trade.may_trade_freely and LivingAzerothDB.tradeFreely
        and not trade.trading_freely then
        SetTradeFreely(true)
    end
end)

local function Build()
    window = LA.Window.Panel("LivingAzerothSettings", WIDTH, HEIGHT, "Living Azeroth · Settings", "settings")
    window:SetFrameStrata("DIALOG")
    local width, height = LA.Window.BodySize(WIDTH, HEIGHT)
    local footer = UI.BUTTON_HEIGHT + 8
    local pageHeight = height - TABS - footer
    window.pages = {}
    window.tabs = UI.TopTabs(window.body, { "Orders ring", "Raid frames", "All my bots", "Trading" },
        function(index)
            Settings.Show(index)
        end)
    window.tabs:SetPoint("TOPLEFT")
    window.tabs:SetPoint("TOPRIGHT")
    for index, build in ipairs({ BuildRing, BuildRaid, BuildAllBots, BuildTrade }) do
        local page = CreateFrame("Frame", nil, window.body)
        page:SetPoint("TOPLEFT", 0, -TABS)
        page:SetSize(width, pageHeight)
        build(page, width, pageHeight)
        window.pages[index] = page
    end
    ShowRaidPart(LivingAzerothDB.raidPart or 1)
    window:HookScript("OnHide", RingPreview)

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
    for _, row in ipairs(window.ringChoices) do
        local chosen = ring[row.choice.key]
        row.segment:Select(chosen)
        for _, option in ipairs(row.choice.options) do
            if option.id == chosen then
                row.hint:SetText(option.hint)
            end
        end
    end
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
    window.labels:SetChecked(raid.labels)
    for _, row in ipairs(window.looks) do
        row.segment:Select(raid[row.key])
    end
    for _, row in ipairs(window.sizes) do
        row:Set(raid[row.key])
    end
    window.raidPreview:Refresh()

    local allBots = LivingAzerothDB.allBots
    window.allBots:SetChecked(allBots.shown ~= false)
    for _, radio in ipairs(window.allBotsLooks) do
        radio:SetChecked(radio.id == allBots.look)
    end
    window.allBotsSize:Set(allBots.size)
    window.allBotsPreview:Arrange()

    local trade = LA.Bridge.trade or {}
    window.tradeBags:SetChecked(LivingAzerothDB.tradeBags ~= false)
    LA.SetShown(window.tradeBags, trade.shares)
    LA.SetShown(window.tradeMissing, not trade.shares)
    LA.SetShown(window.gmHeading, trade.may_trade_freely)
    LA.SetShown(window.tradeFreely, trade.may_trade_freely)
    window.tradeFreely:SetChecked(trade.trading_freely)

    window.move:SetText(LA.unlocked and "Lock frames" or "Move frames")
    window.move.glyphIcon:Set(LA.unlocked and "lock" or "move", Style.gold)
end

-- Shows the window at a tab (the last one shown, at first the ring's).
function Settings.Show(index)
    if not window then
        Build()
    end
    index = index or LivingAzerothDB.settingsTab or 1
    if not window.pages[index] then
        index = 1
    end
    LivingAzerothDB.settingsTab = index
    window.tabs:Select(index)
    for i, page in ipairs(window.pages) do
        LA.SetShown(page, i == index)
    end
    window:Show()
    Settings.Refresh()
    RingPreview()
end

function Settings.Toggle()
    if window and window:IsShown() then
        window:Hide()
    else
        Settings.Show()
    end
end

-- While frames are unlocked and the ring opens at a fixed spot, the ring itself shows there to be dragged.
function Settings.Unlock()
    RingPreview()
end
