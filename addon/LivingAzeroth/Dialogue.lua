-- SPDX-License-Identifier: GPL-2.0-or-later
-- The Dialogue tab: what the player hears from characters (their own settings, kept by the server), and why a
-- character said a line the player heard. Administrators also see the dialogue budget (read-only).
local _, LA = ...

local Dialogue = {}
LA.Dialogue = Dialogue

local UI, Style = LA.UI, LA.Style

local CHANNELS = {
    { "party", "Your party", "Your bots and companions in party chat" },
    { "nearby", "Characters nearby", "Characters speaking aloud near you" },
    { "guild", "Guild chat", "Guild members who are bots" },
    { "general", "The General channel", "Characters in the zone's channel" },
}
local HEARING = { { "chatty", "Chatty" }, { "spoken", "When spoken to" }, { "silent", "Silent" } }
local REMARKS = { { id = "often", label = "Often" }, { id = "sometimes", label = "Now and then" },
    { id = "rarely", label = "Rarely" }, { id = "never", label = "Never" } }
local BANTER = { { id = "off", label = "Off" }, { id = "sometimes", label = "Now and then" },
    { id = "often", label = "Often" } }
local CHANNEL_NAMES = { whisper = "Whisper", party = "Party", raid = "Raid", say = "Say", yell = "Yell",
    guild = "Guild", general = "General", emote = "Emote" }
local PACE = 248 -- the pace panel's width
local WHY = 270 -- the explanation panel's width

local page
local settings -- the server's copy, once it has answered
local radios = {} -- channel -> { value -> radio }
local chosenLine
local lines = {}

local function Status(text, colour)
    colour = colour or Style.T().muted
    page.status:SetText(text or "")
    page.status:SetTextColor(colour[1], colour[2], colour[3])
end

-- A recorded line without the speaker's name in front, which the records keep.
local function Spoken(text, speaker)
    text = text or ""
    if speaker and text:sub(1, #speaker + 2) == speaker .. ": " then
        return text:sub(#speaker + 3)
    end
    return text
end

local function ReadingText(value)
    return value == 0 and "As the realm sets it" or value .. " words a minute"
end

local function TurnsText(value)
    return value == 0 and "As the realm sets it" or value .. (value == 1 and " reply" or " replies")
end

local function ShowSettings()
    if not settings then
        return
    end
    for key, group in pairs(radios) do
        for value, radio in pairs(group) do
            radio:SetChecked(settings[key] == value)
        end
    end
    page.remarks:Select(settings.remarks)
    page.banter:Select(settings.banter)
    page.reading:SetQuietly(settings.reading or 0)
    page.readingValue:SetText(ReadingText(settings.reading or 0))
    page.turns:SetQuietly(settings.turns or 0)
    page.turnsValue:SetText(TurnsText(settings.turns or 0))
end

local function Change(fields)
    Status("Saving...")
    LA.Bridge.Request({ op = "hearing", change = fields }, function(reply)
        if reply.ok then
            settings = reply.hearing
            Status("Saved.", Style.green)
        else
            Status("Not saved: " .. (reply.reason or reply.error or "?"), Style.red)
        end
        ShowSettings()
    end)
end

-- Saves a slider's value once it has stopped moving for a moment.
local function SaveLater(field, value)
    page.waiting = page.waiting or {}
    page.waiting[field] = { value = value, at = GetTime() + 0.6 }
    page.frame:SetScript("OnUpdate", function(self)
        for name, wait in pairs(page.waiting) do
            if GetTime() >= wait.at then
                page.waiting[name] = nil
                Change({ [name] = wait.value })
            end
        end
        if not next(page.waiting) then
            self:SetScript("OnUpdate", nil)
        end
    end)
end

-- Why

local function Section(parts, title, text)
    parts[#parts + 1] = { title, text }
end

local function ShowWhy(reply)
    local d = page.why
    for _, region in ipairs(d.used) do
        region:Hide()
    end
    local stack = d.stack
    stack:Reset()
    if not chosenLine then
        d.heading:SetText("Why did they say that?")
        stack:Add(d.heading, 3, 28)
        local note = d.Text(1, "body")
        note:SetText(#lines > 0 and "Choose a line on the left to see what led a character to say it." or
            "Lines characters say to you show here, and why they said them.")
        stack:Add(note, 12)
        d.scroll:SetContentHeight(stack:Height())
        return
    end
    d.heading:SetText("Why " .. (chosenLine.speaker or "they") .. " said it")
    stack:Add(d.heading, 3, 28)
    local parts = {}
    if not reply then
        Section(parts, nil, "Asking...")
    elseif not reply.ok then
        Section(parts, nil, reply.reason or reply.error or "?")
    else
        Section(parts, "What was said", Spoken(reply.text, reply.speaker))
        if reply.started_by then
            local who = reply.started_by.who
            Section(parts, "What started it", who .. ": " .. Spoken(reply.started_by.text, who))
        end
        if reply.remembers_you and #reply.remembers_you > 0 then
            local notes = {}
            for _, note in ipairs(reply.remembers_you) do
                notes[#notes + 1] = "• " .. note.text
            end
            Section(parts, "What " .. (reply.speaker or "they") .. " remembered", table.concat(notes, "\n"))
        end
        if reply.actions and #reply.actions > 0 then
            local done = {}
            for _, action in ipairs(reply.actions) do
                done[#done + 1] = "• " .. (action.action or "?") .. " (" .. (action.status or "?") .. ")"
            end
            Section(parts, "What they did", table.concat(done, "\n"))
        end
        if reply.cost_dollars then
            Section(parts, "Cost", string.format("$%.4f from the dialogue budget", reply.cost_dollars))
        end
    end
    local n = 0
    for _, part in ipairs(parts) do
        if part[1] then
            n = n + 1
            local title = d.Text(n, "small")
            title:SetText(part[1])
            stack:Add(title, 12)
        end
        n = n + 1
        local text = d.Text(n, "body")
        text:SetText(part[2])
        stack:Add(text, part[1] and 2 or 12)
    end
    d.scroll:SetContentHeight(stack:Height() + 4)
end

local function ShowLines()
    local items = {}
    for _, line in ipairs(lines) do
        items[#items + 1] = { key = line.line, line = line,
            text = "|cffffd100" .. (line.speaker or "?") .. "|r  |cffa89f8c" ..
                (CHANNEL_NAMES[line.channel] or "") .. "|r",
            sub = Spoken(line.text, line.speaker) }
    end
    page.lines:SetItems(items, "No lines yet. When characters speak to you, their lines show here.")
    page.lines:Choose(chosenLine and chosenLine.line)
end

local function Explain(line)
    chosenLine = line
    ShowLines()
    ShowWhy(nil)
    LA.Bridge.Request({ op = "why", line = line.line }, function(reply)
        if chosenLine == line then
            ShowWhy(reply)
        end
    end)
end

local function LoadLines()
    LA.Bridge.Request({ op = "why" }, function(reply)
        lines = reply.ok and reply.lines or {}
        ShowLines()
        if not chosenLine then
            ShowWhy(nil)
        end
    end)
end

local function ShowPart(index)
    LA.SetShown(page.hear, index == 1)
    LA.SetShown(page.whyPart, index == 2)
    if index == 2 then
        LoadLines()
    end
end

-- Building

local function BuildHearing(parent, width, height)
    local who = UI.Inset(parent)
    who:SetPoint("TOPLEFT")
    local whoWidth = width - PACE - 10
    who:SetSize(whoWidth, height)
    local inner = whoWidth - 20
    local title = UI.Heading(who, "Who talks to you")
    title:SetPoint("TOPLEFT", 10, -8)
    title:SetWidth(inner)
    local note = UI.Text(who, "small", inner)
    note:SetPoint("TOPLEFT", title, "BOTTOMLEFT", 0, -5)
    note:SetText("Your own choice; other players keep theirs. Whispers always reach you.")
    page.status = UI.Text(who, "small", inner)
    page.status:SetPoint("BOTTOMLEFT", 10, 10)
    local column = 68
    local labelWidth = inner - 3 * column
    for i, option in ipairs(HEARING) do
        local head = UI.Text(who, "small", column)
        head:SetJustifyH("CENTER")
        head:SetWordWrap(false)
        head:SetPoint("TOPLEFT", 10 + labelWidth + (i - 1) * column, -62)
        head:SetText(option[2])
    end
    for row, channel in ipairs(CHANNELS) do
        local y = -84 - (row - 1) * 44
        local label = UI.Text(who, "label", labelWidth)
        label:SetPoint("TOPLEFT", 10, y)
        label:SetText(channel[2])
        local hint = UI.Text(who, "small", labelWidth - 6)
        hint:SetPoint("TOPLEFT", label, "BOTTOMLEFT", 0, -2)
        hint:SetText(channel[3])
        if row > 1 then
            local line = UI.Divider(who)
            line:SetPoint("TOPLEFT", 10, y + 9)
            line:SetPoint("TOPRIGHT", -10, y + 9)
        end
        radios[channel[1]] = {}
        for i, option in ipairs(HEARING) do
            local radio = UI.Radio(who)
            radio:SetSize(40, 26)
            radio:SetPoint("TOP", who, "TOPLEFT", 10 + labelWidth + (i - 0.5) * column, y + 6)
            radio:SetScript("OnClick", function()
                ShowSettings() -- the dot moves once the server has it
                Change({ [channel[1]] = option[1] })
            end)
            radios[channel[1]][option[1]] = radio
        end
    end

    local pace = UI.Inset(parent)
    pace:SetPoint("TOPRIGHT")
    pace:SetSize(PACE, height)
    inner = PACE - 20
    local stack = UI.Stack(pace)
    local function Add(region, gap)
        stack:Add(region, gap, 10)
    end
    local paceTitle = UI.Heading(pace, "Pace")
    paceTitle:SetWidth(inner)
    Add(paceTitle, 8)
    local remarksLabel = UI.Text(pace, "label")
    remarksLabel:SetText("Remarks on their own")
    Add(remarksLabel, 10)
    page.remarks = UI.Segment(pace, REMARKS, inner, function(id)
        page.remarks:Select(settings and settings.remarks)
        Change({ remarks = id })
    end)
    Add(page.remarks, 6)
    local banterLabel = UI.Text(pace, "label")
    banterLabel:SetText("Combat banter")
    Add(banterLabel, 12)
    page.banter = UI.Segment(pace, BANTER, inner, function(id)
        page.banter:Select(settings and settings.banter)
        Change({ banter = id })
    end)
    Add(page.banter, 6)

    local readingLabel = UI.Text(pace, "label")
    readingLabel:SetText("Reading speed")
    Add(readingLabel, 14)
    page.readingValue = UI.Text(pace, "small")
    page.readingValue:SetPoint("BOTTOMRIGHT", readingLabel, "BOTTOMLEFT", inner, 1)
    page.readingValue:SetJustifyH("RIGHT")
    page.reading = UI.Slider(pace, 0, 400, 20, inner, function(value)
        value = (value > 0 and value < 60) and 60 or value
        page.readingValue:SetText(ReadingText(value))
        SaveLater("reading", value)
    end)
    Add(page.reading, 6)
    local readingHint = UI.Text(pace, "small", inner)
    readingHint:SetText("Sets the pause before the next line, so you can read each one.")
    Add(readingHint, 4)

    local turnsLabel = UI.Text(pace, "label")
    turnsLabel:SetText("Longest exchange")
    Add(turnsLabel, 14)
    page.turnsValue = UI.Text(pace, "small")
    page.turnsValue:SetPoint("BOTTOMRIGHT", turnsLabel, "BOTTOMLEFT", inner, 1)
    page.turnsValue:SetJustifyH("RIGHT")
    page.turns = UI.Slider(pace, 0, 6, 1, inner, function(value)
        page.turnsValue:SetText(TurnsText(value))
        SaveLater("turns", value)
    end)
    Add(page.turns, 6)
    local turnsHint = UI.Text(pace, "small", inner)
    turnsHint:SetText("How many characters may answer one another before they stop.")
    Add(turnsHint, 4)
end

local function BuildWhy(parent, width, height)
    local listWidth = width - WHY - 10
    page.lines = UI.List(parent, function(item)
        Explain(item.line)
    end, 30)
    page.lines:SetPoint("TOPLEFT")
    page.lines:SetSize(listWidth, height - UI.BUTTON_HEIGHT - 8)
    local refresh = UI.Button(parent, "Refresh", 70, "plain")
    refresh:SetPoint("BOTTOMRIGHT", parent, "BOTTOMLEFT", listWidth - 8, 0)
    refresh:SetScript("OnClick", LoadLines)

    local inset = UI.Inset(parent)
    inset:SetPoint("TOPRIGHT")
    inset:SetSize(WHY, height)
    local eye = UI.Icon(inset, 20, "tile")
    eye:SetPoint("TOPLEFT", 10, -9)
    eye:Set("eye", "violet")
    local scroll = UI.Scroll(inset)
    scroll:SetPoint("TOPLEFT", 10, -10)
    scroll:SetPoint("BOTTOMRIGHT", -4, 10)
    local textWidth = WHY - 24
    local d = { used = {}, scroll = scroll }
    d.heading = UI.Text(scroll.child, "heading", textWidth - 28)
    d.stack = UI.Stack(scroll.child)
    -- The explanation's texts, made as they are needed.
    function d.Text(n, kind)
        local key = kind .. n
        if not d[key] then
            d[key] = UI.Text(scroll.child, kind, textWidth)
            d.used[#d.used + 1] = d[key]
        end
        return d[key]
    end
    page.why = d
end

local function Build(self)
    page = self
    local width, height = LA.Manager.BodySize()
    page.tabs = UI.TopTabs(page.frame, { "What you hear", "Why did they say that?" }, ShowPart)
    page.tabs:SetPoint("TOPLEFT")
    page.tabs:SetPoint("TOPRIGHT")

    -- The dialogue budget, for administrators.
    page.budget = CreateFrame("Frame", nil, page.frame)
    page.budget:SetPoint("TOPRIGHT", 0, -2)
    page.budget:SetSize(240, 16)
    page.budget.text = UI.Text(page.budget, "body")
    page.budget.text:SetPoint("RIGHT")
    page.budget.bar = CreateFrame("Frame", nil, page.budget)
    page.budget.bar:SetSize(90, 6)
    page.budget.bar:SetPoint("RIGHT", page.budget.text, "LEFT", -8, 0)
    UI.Surface(page.budget.bar, "box")
    page.budget.fill = page.budget.bar:CreateTexture(nil, "ARTWORK")
    page.budget.fill:SetTexture(Style.WHITE)
    page.budget.fill:SetPoint("TOPLEFT", LA.Draw.Px(1), -LA.Draw.Px(1))
    page.budget.fill:SetPoint("BOTTOMLEFT", LA.Draw.Px(1), LA.Draw.Px(1))
    page.budget.fill:SetGradientAlpha("VERTICAL", 0.6, 0.45, 0.13, 1, 0.94, 0.82, 0.44, 1)
    page.budget.label = UI.Text(page.budget, "small")
    page.budget.label:SetPoint("RIGHT", page.budget.bar, "LEFT", -8, 0)
    page.budget.label:SetText("Dialogue budget")
    page.budget:Hide()


    local top = 28
    page.hear = CreateFrame("Frame", nil, page.frame)
    page.hear:SetPoint("TOPLEFT", 0, -top)
    page.hear:SetSize(width, height - top)
    BuildHearing(page.hear, width, height - top)
    page.whyPart = CreateFrame("Frame", nil, page.frame)
    page.whyPart:SetPoint("TOPLEFT", 0, -top)
    page.whyPart:SetSize(width, height - top)
    BuildWhy(page.whyPart, width, height - top)
    page.tabs:Select(1)
    ShowPart(1)
end

local function Show()
    Status("")
    LA.Bridge.Request({ op = "hearing" }, function(reply)
        if reply.ok then
            settings = reply.hearing
            ShowSettings()
        else
            Status("Not available: " .. (reply.reason or reply.error or "?"), Style.red)
        end
    end)
    LA.Bridge.Request({ op = "budget" }, function(reply)
        if reply.ok and reply.ceiling_dollars and reply.ceiling_dollars > 0 then
            page.budget.text:SetText(string.format("$%.2f of $%.2f", reply.spent_dollars, reply.ceiling_dollars))
            local share = math.min(1, reply.spent_dollars / reply.ceiling_dollars)
            page.budget.fill:SetWidth(LA.Draw.Ceil(math.max(1, (90 - LA.Draw.Px(2)) * share)))
            page.budget:Show()
        else
            page.budget:Hide()
        end
    end)
    if page.tabs.selected == 2 then
        LoadLines()
    end
end

LA.On("loaded", function()
    LA.Manager.AddPage("Dialogue", "dialogue", Build, Show)
end)
