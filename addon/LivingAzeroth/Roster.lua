-- SPDX-License-Identifier: GPL-2.0-or-later
-- The Roster tab: who is in the player's group, the player's other characters (bring one in as a bot, or send
-- it home) and the characters they have come to know best (invite the ones online). The chosen one shows on the
-- right: for a bot of yours its talents, gear, bags, money and what it is doing, and for anyone what they
-- remember of you.
local _, LA = ...

local Roster = {}
LA.Roster = Roster

local UI, Style = LA.UI, LA.Style

local DETAIL = 220 -- the details panel's width
local page
local roster = { characters = {}, companions = {} }
local chosen -- an entry
local about, memory = {}, {} -- guid -> text, asked once per load

local function Status(text, colour)
    colour = colour or Style.T().muted
    page.status:SetText(text or "")
    page.status:SetTextColor(colour[1], colour[2], colour[3])
end

-- "party" or "raid", whichever the player is in (the server's in_party means either).
local function GroupWord()
    return GetNumRaidMembers() > 0 and "raid" or "party"
end

local function Where(entry)
    if entry.in_party then
        local bot = LA.Bots.Mine()[entry.guid]
        return bot and LA.Orders.Standing(bot).state or "In your " .. GroupWord()
    elseif entry.in_world or entry.online then
        return entry.zone ~= "" and entry.zone or "In the world"
    end
    return entry.isCharacter and "Resting" or "Away"
end

local function Familiarity(entry)
    -- Familiarity grows by seeing them around (1) and doing things together (4), up to 100.
    local value = entry.familiarity or 0
    return value >= 60 and "Old friends" or value >= 25 and "Well known to you" or value >= 8 and
        "Seen about often" or "A familiar face"
end

local Load, ShowList

-- What the server says about them (asked once), or what we know from the roster.
local function About(entry)
    local bot = LA.Bots.Mine()[entry.guid]
    if bot and bot.commandable then
        if not about[entry.guid] then
            about[entry.guid] = { asking = true }
            LA.Bridge.Request({ op = "inspect", bot = entry.guid }, function(reply)
                about[entry.guid] = reply.ok and reply or { failed = true }
                if chosen == entry then
                    Roster.ShowDetail()
                end
            end)
        end
        return about[entry.guid]
    end
    return nil
end

local function Memory(entry)
    if not memory[entry.guid] then
        memory[entry.guid] = "Asking..."
        LA.Bridge.Request({ op = "memory", character = entry.guid }, function(reply)
            local lines = {}
            for _, note in ipairs(reply.ok and reply.remembers_you or {}) do
                lines[#lines + 1] = "• " .. note.text
            end
            memory[entry.guid] = #lines > 0 and table.concat(lines, "\n") or (entry.isCharacter and
                "Your own character: nothing to remember." or
                "Nothing yet. They remember what you do together and what you tell them.")
            if chosen == entry then
                Roster.ShowDetail()
            end
        end)
    end
    return memory[entry.guid]
end

function Roster.ShowDetail()
    local d = page.detail
    local stack = d.stack
    stack:Reset()
    for _, region in ipairs(d.parts) do
        region:Hide()
    end
    page.list:Choose(chosen and chosen.guid)
    if not chosen then
        stack:Add(d.introTitle)
        stack:Add(d.intro, 6)
        return
    end
    local entry = chosen
    d.portrait:SetCharacter(entry.guid, entry.class)
    stack:Add(d.portrait, 0, 0, 40)
    d.name:SetText(entry.name)
    local colour = UI.ClassColor(entry.class)
    d.name:SetTextColor(colour.r, colour.g, colour.b)
    d.name:ClearAllPoints()
    d.name:SetPoint("TOPLEFT", d.portrait, "TOPRIGHT", 8, -3)
    d.name:Show()
    d.line:SetText("Level " .. (entry.level or "?") .. " " .. UI.ClassName(entry.class))
    d.line:ClearAllPoints()
    d.line:SetPoint("TOPLEFT", d.name, "BOTTOMLEFT", 0, -2)
    d.line:Show()
    d.where:SetText(Where(entry))
    d.where:ClearAllPoints()
    d.where:SetPoint("TOPLEFT", d.line, "BOTTOMLEFT", 0, -2)
    d.where:Show()

    local facts = About(entry)
    if facts and not facts.asking and not facts.failed then
        local bot = LA.Bots.Mine()[entry.guid]
        local values = {
            facts.spec ~= "" and facts.spec or "None chosen",
            "Item level " .. (facts.item_level or 0),
            (facts.free_slots or 0) .. " free slots",
            GetCoinTextureString(facts.money or 0),
            LA.Orders.Standing(bot or facts.bot or {}).state,
        }
        local y = stack:Height() + 10
        for i, value in ipairs(values) do
            d.keys[i]:ClearAllPoints()
            d.keys[i]:SetPoint("TOPLEFT", 0, -y)
            d.keys[i]:Show()
            d.values[i]:ClearAllPoints()
            d.values[i]:SetPoint("TOPLEFT", 58, -y)
            d.values[i]:SetText(value)
            d.values[i]:Show()
            y = y + 15
        end
        stack.y = y
    else
        d.about:SetText(facts and facts.asking and "Asking..." or entry.isCharacter and
            (entry.in_world and "With someone else." or "Resting. Bring them in and they join you as a bot.") or
            Familiarity(entry) .. ".")
        stack:Add(d.about, 10)
    end
    stack:Add(d.memoryTitle, 12)
    d.memoryTitle:SetWidth(stack.parent:GetWidth())
    d.memory:SetText(Memory(entry))
    stack:Add(d.memory, 5)

    page.whisper:SetUsable(entry.in_world or entry.online or entry.in_party)
end

local function Act(entry)
    if not entry.isCharacter then
        InviteUnit(entry.name)
        Status("Invited " .. entry.name .. ".", Style.gold)
        return
    end
    local bring = not entry.in_world
    Status(bring and ("Bringing " .. entry.name .. "...") or ("Sending " .. entry.name .. " home..."), Style.gold)
    LA.Bridge.Request({ op = bring and "bring" or "dismiss", name = entry.name }, function(reply)
        if reply.ok then
            Status(bring and (entry.name .. " is on the way.") or (entry.name .. " has gone home."), Style.green)
        else
            Status(reply.reason or reply.error or "?", Style.red)
        end
        Load()
    end)
end

local function Row(entry)
    local status, colour = Where(entry), nil
    if entry.in_party then
        colour = Style.T().chipText
    elseif entry.online or entry.in_world then
        colour = Style.green
    end
    local action
    if entry.isCharacter then
        action = { label = entry.in_world and "Send home" or "Bring in", onClick = function() Act(entry) end }
    elseif entry.online and not entry.in_party then
        action = { label = "Invite", onClick = function() Act(entry) end }
    end
    local sub = "Level " .. (entry.level or "?") .. " " .. UI.ClassName(entry.class)
    if not entry.in_party and (entry.in_world or entry.online) and entry.zone and entry.zone ~= "" then
        sub = sub .. " · " .. entry.zone
    end
    return {
        key = entry.guid, entry = entry, text = UI.Coloured(entry.name, entry.class), sub = sub,
        tag = (entry.in_party or not action) and status or nil, tagColour = colour, action = action,
        dim = not (entry.in_party or entry.online or entry.in_world),
        icon = function(row)
            row.portrait:SetCharacter(entry.guid, entry.class)
            row.portrait:Show()
        end,
    }
end

function ShowList()
    local group, mine, known = {}, {}, {}
    for _, entry in ipairs(roster.characters) do
        entry.isCharacter = true
        table.insert(entry.in_party and group or mine, entry)
    end
    for _, entry in ipairs(roster.companions) do
        table.insert(entry.in_party and group or known, entry)
    end
    local items = {}
    local function Section(title, note, entries)
        if #entries > 0 then
            items[#items + 1] = { header = true, text = title, note = note }
            for _, entry in ipairs(entries) do
                items[#items + 1] = Row(entry)
            end
        end
    end
    Section("In your " .. GroupWord(), #group .. (#group == 1 and " character" or " characters"), group)
    Section("Your characters", "play them as bots", mine)
    Section("Characters you know", "the ones you spend time with", known)
    page.list:SetItems(items, "No other characters yet. Your other characters on this realm, and the " ..
        "characters you spend time with, show here.")
    if chosen then
        local found
        for _, item in ipairs(items) do
            if item.entry and item.entry.guid == chosen.guid then
                found = item.entry
            end
        end
        chosen = found
    end
    Roster.ShowDetail()
end

function Load()
    about, memory = {}, {}
    LA.Bridge.Request({ op = "roster" }, function(reply)
        if reply.ok then
            roster.characters = reply.characters or {}
            roster.companions = reply.companions or {}
            ShowList()
        else
            Status("Not available: " .. (reply.reason or reply.error or "?"), Style.red)
        end
    end)
end

local function Build(self)
    page = self
    local width, height = LA.Manager.BodySize()
    local listWidth = width - DETAIL - 12

    page.list = UI.List(page.frame, function(item)
        chosen = item.entry
        Roster.ShowDetail()
    end, 30)
    page.list:SetPoint("TOPLEFT")
    page.list:SetSize(listWidth, height - UI.BUTTON_HEIGHT - 8)

    local refresh = UI.Button(page.frame, "Refresh", 70, "plain")
    refresh:SetPoint("BOTTOMRIGHT", page.frame, "BOTTOMLEFT", listWidth - 8, 0)
    page.status = UI.Text(page.frame, "small")
    page.status:SetPoint("LEFT", page.frame, "BOTTOMLEFT", 2, UI.BUTTON_HEIGHT / 2)
    page.status:SetPoint("RIGHT", refresh, "LEFT", -8, 0)
    page.status:SetWordWrap(false)
    refresh:SetScript("OnClick", Load)

    local inset = UI.Inset(page.frame)
    inset:SetPoint("TOPRIGHT")
    inset:SetSize(DETAIL, height)
    local scroll = UI.Scroll(inset)
    scroll:SetPoint("TOPLEFT", 10, -10)
    scroll:SetPoint("BOTTOMRIGHT", -4, UI.BUTTON_HEIGHT + 18)
    local area = scroll.child
    local d = { parts = {} }
    local function Part(region)
        d.parts[#d.parts + 1] = region
        return region
    end
    local textWidth = DETAIL - 26
    d.introTitle = Part(UI.Text(area, "heading", textWidth))
    d.introTitle:SetText("Choose someone")
    d.intro = Part(UI.Text(area, "body", textWidth))
    d.intro:SetText("Your other characters can join you as bots: press Bring in beside one. The characters you " ..
        "spend time with are listed too, with what they remember of you.")
    d.portrait = Part(UI.Portrait(area, 40))
    d.name = Part(UI.Text(area, "name", textWidth - 48))
    d.line = Part(UI.Text(area, "body", textWidth - 48))
    d.where = Part(UI.Text(area, "small", textWidth - 48))
    d.keys, d.values = {}, {}
    for i, key in ipairs({ "Talents", "Gear", "Bags", "Money", "Doing" }) do
        d.keys[i] = Part(UI.Text(area, "small", 56))
        d.keys[i]:SetText(key)
        d.values[i] = Part(UI.Text(area, "value", textWidth - 58))
    end
    d.about = Part(UI.Text(area, "body", textWidth))
    d.memoryTitle = Part(UI.Heading(area, "What they remember of you"))
    d.memory = Part(UI.Text(area, "body", textWidth))
    d.stack = UI.Stack(area)
    local show = Roster.ShowDetail
    Roster.ShowDetail = function()
        show()
        scroll:SetContentHeight(d.stack:Height() + 4)
    end
    page.detail = d
    page.detailScroll = scroll

    page.whisper = UI.Button(inset, WHISPER, 90, nil, "dialogue")
    page.whisper:SetPoint("BOTTOMLEFT", 10, 10)
    page.whisper:SetScript("OnClick", function()
        if chosen then
            ChatFrame_SendTell(chosen.name)
        end
    end)
    page.whisper:Disable()
end

-- Keeps the "In your party" section and what bots are doing current while the tab is open.
local function Changed()
    if page and LA.Manager.IsShown(page) then
        ShowList()
    end
end
LA.On("bots:changed", Changed)
LA.On("orders:changed", Changed)

LA.On("loaded", function()
    LA.Manager.AddPage("Roster", "roster", Build, function()
        Status("")
        Load()
    end)
end)
