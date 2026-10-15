-- SPDX-License-Identifier: GPL-2.0-or-later
-- The settings window, in tabs. Orders ring: where the ring opens, its size, and which orders it holds (up to eight,
-- in ring order). Raid frames: how the groups are laid out, what they show and their look (bars, colours, role
-- icons). Raid sizes: the cells, the spaces between them, and the size of each part of a cell. All my bots: that
-- button's look and size. The raid tabs and All my bots show what they change as it will look. Trading: the bot
-- bags window, and for game masters, trading freely with any bot. "Move frames" unlocks the party and raid frames
-- and the "All my bots" button and, with the ring at "a fixed spot", shows the ring itself to drag.
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
local ALL_BOTS_LOOKS = { { id = "both", label = "Medal and name" }, { id = "medal", label = "Medal" },
    { id = "name", label = "Name" } }
local WIDTH, HEIGHT = 560, 450
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
    window.labels = UI.Check(left, "Group names", "Click one for that group's orders", inner)
    stack:Add(window.labels, 6, 10)
    window.labels.onToggle = function(_, on)
        Raid().labels = on
        RaidChanged()
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
    local lookHeading = UI.Heading(right, "Look")
    lookHeading:SetPoint("TOPLEFT", 10, -8)
    lookHeading:SetWidth(rightWidth - 20)
    window.looks = {}
    local above = lookHeading
    local labelWidth = 46
    for i, look in ipairs(LOOKS) do
        local row = CreateFrame("Frame", nil, right)
        row:SetSize(rightWidth - 20, UI.BUTTON_HEIGHT)
        row:SetPoint("TOPLEFT", above, "BOTTOMLEFT", 0, i == 1 and -8 or -6)
        local label = UI.Text(row, "label")
        label:SetPoint("LEFT")
        label:SetText(look.label)
        row.segment = UI.Segment(row, look.options, rightWidth - 20 - labelWidth, function(id)
            Raid()[look.key] = id
            RaidChanged()
        end)
        row.segment:SetPoint("LEFT", labelWidth, 0)
        row.key = look.key
        window.looks[i] = row
        above = row
    end
    UI.Tooltip(window.looks[1].segment.chips[1], "Gradient", "Dark at the left, full colour where a full bar ends, " ..
        "with dark rims: the look of DragonUI's party bars.")
    local preview = UI.Heading(right, "How a cell looks")
    preview:SetPoint("TOPLEFT", above, "BOTTOMLEFT", 0, -12)
    preview:SetWidth(rightWidth - 20)
    window.previews = { LA.Raid.Preview(right) }
    window.previews[1]:SetPoint("TOP", preview, "BOTTOM", 0, -10)
end

-- Raid sizes

local function SizeColumn(panel, title, sizes, width)
    local heading = UI.Heading(panel, title)
    heading:SetPoint("TOPLEFT", 10, -8)
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
    return above
end

local function BuildRaidSizes(page, width, height)
    window.sizes = {}
    local left = UI.Inset(page)
    left:SetPoint("TOPLEFT")
    left:SetSize(LEFT, height)
    local last = SizeColumn(left, "Cells", CELL_SIZES, LEFT - 20)
    local right = UI.Inset(page)
    right:SetPoint("TOPRIGHT")
    local rightWidth = width - LEFT - 10
    right:SetSize(rightWidth, height)
    SizeColumn(right, "Text and marks", PART_SIZES, rightWidth - 20)
    local preview = UI.Heading(left, "How a cell looks")
    preview:SetPoint("TOPLEFT", last, "BOTTOMLEFT", 0, -12)
    preview:SetWidth(LEFT - 20)
    local cell = LA.Raid.Preview(left)
    cell:SetPoint("TOP", preview, "BOTTOM", 0, -10)
    window.previews[#window.previews + 1] = cell
    local reset = UI.Button(right, "Default sizes", 110, "plain")
    reset:SetPoint("BOTTOMRIGHT", -10, 10)
    reset:SetScript("OnClick", function()
        for key, range in pairs(LA.Raid.SIZES) do
            Raid()[key] = range[3]
        end
        RaidChanged()
    end)
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
    local preview = UI.Heading(right, "How it looks")
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
        "what they got while grouped with you, and random bots sell the rest.", inner)
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
    window.tabs = UI.TopTabs(window.body, { "Orders ring", "Raid frames", "Raid sizes", "All my bots", "Trading" },
        function(index)
        Settings.Show(index)
    end)
    window.tabs:SetPoint("TOPLEFT")
    window.tabs:SetPoint("TOPRIGHT")
    for index, build in ipairs({ BuildRing, BuildRaid, BuildRaidSizes, BuildAllBots, BuildTrade }) do
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
    window.labels:SetChecked(raid.labels)
    for _, row in ipairs(window.looks) do
        row.segment:Select(raid[row.key])
    end
    for _, row in ipairs(window.sizes) do
        row:Set(raid[row.key])
    end
    for _, preview in ipairs(window.previews) do
        preview:Refresh()
    end

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
