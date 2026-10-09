-- SPDX-License-Identifier: GPL-2.0-or-later
-- The Dialogue tab: what the player hears from characters (their own settings, kept by the server), and why a
-- character said a line the player heard. Administrators also see the dialogue budget (read-only).
local _, LA = ...

local Dialogue = {}
LA.Dialogue = Dialogue

local CHANNELS = {
    { "party", "Your party" },
    { "nearby", "Characters nearby" },
    { "guild", "Guild chat" },
    { "general", "The General channel" },
}
local HEARING = { { "chatty", "Chatty" }, { "spoken", "When spoken to" }, { "silent", "Silent" } }
local REMARKS = { { "often", "Often" }, { "sometimes", "Sometimes" }, { "rarely", "Rarely" }, { "never", "Never" } }
local BANTER = { { "off", "Off" }, { "sometimes", "Sometimes" }, { "often", "Often" } }
local CHANNEL_NAMES = { whisper = "Whisper", party = "Party", raid = "Raid", say = "Say", yell = "Yell",
    guild = "Guild", general = "General", emote = "Emote" }

local page
local settings -- the server's copy, once it has answered
local radios = {} -- setting -> { value -> radio }
local showing = "hear" -- "hear" or a line number
local lines = {}

local function Status(text, r, g, b)
    page.status:SetText(text or "")
    page.status:SetTextColor(r or 1, g or 0.82, b or 0)
end

-- A recorded line without the speaker's name in front, which the records keep.
local function Spoken(text, speaker)
    text = text or ""
    if speaker and text:sub(1, #speaker + 2) == speaker .. ": " then
        return text:sub(#speaker + 3)
    end
    return text
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
    page.reading.updating, page.turns.updating = true, true
    page.reading:SetValue(settings.reading or 0)
    page.turns:SetValue(settings.turns or 0)
    page.reading.updating, page.turns.updating = false, false
    page.readingValue:SetText(settings.reading == 0 and "As the realm sets it" or settings.reading ..
        " words a minute")
    page.turnsValue:SetText(settings.turns == 0 and "As the realm sets it" or settings.turns ..
        (settings.turns == 1 and " reply" or " replies"))
end

local function Change(fields)
    Status("Saving...")
    LA.Bridge.Request({ op = "hearing", change = fields }, function(reply)
        if reply.ok then
            settings = reply.hearing
            Status("Saved.")
        else
            Status("Not saved: " .. (reply.reason or reply.error or "?"), 1, 0.3, 0.2)
        end
        ShowSettings()
    end)
end

local function Radio(parent, key, value, label)
    radios[key] = radios[key] or {}
    local radio = LA.UI.Radio(parent, label, true)
    radio:SetScript("OnClick", function()
        ShowSettings() -- the dot moves once the server has it
        Change({ [key] = value })
    end)
    radios[key][value] = radio
    return radio
end

local function Slider(parent, name, low, high, step, title, explanation)
    local slider = CreateFrame("Slider", "LivingAzerothDialogue" .. name, parent, "OptionsSliderTemplate")
    slider:SetWidth(240)
    slider:SetMinMaxValues(low, high)
    slider:SetValueStep(step)
    _G[slider:GetName() .. "Low"]:SetText("")
    _G[slider:GetName() .. "High"]:SetText("")
    _G[slider:GetName() .. "Text"]:SetText("")
    slider:SetScript("OnEnter", function(self)
        GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
        GameTooltip:AddLine(title, 1, 1, 1)
        GameTooltip:AddLine(explanation, nil, nil, nil, true)
        GameTooltip:Show()
    end)
    slider:SetScript("OnLeave", GameTooltip_Hide)
    return slider
end

-- The settings, laid out down the parchment; a row of radio buttons is one frame holding them side by side.
local function RadioRow(parent, key, options, widths)
    local row = CreateFrame("Frame", nil, parent)
    row:SetSize(280, 18)
    local x = 0
    for i, option in ipairs(options) do
        Radio(row, key, option[1], option[2]):SetPoint("LEFT", x, 0)
        x = x + (widths and widths[i] or 90)
    end
    return row
end

local function Layout()
    local d, stack = page.parts, page.stack
    stack:Reset()
    for _, region in pairs(d) do
        region:Hide()
    end
    if showing == "hear" then
        stack:Add(d.hearTitle)
        stack:Add(d.hearNote, 4)
        for _, channel in ipairs(CHANNELS) do
            stack:Add(d["label:" .. channel[1]], 10)
            stack:Add(d["row:" .. channel[1]], 3, 4)
        end
        stack:Add(d.paceTitle, 16)
        stack:Add(d.remarksLabel, 8)
        stack:Add(d.remarksRow, 3, 4)
        stack:Add(d.remarksRow2, 2, 4)
        stack:Add(d.banterLabel, 10)
        stack:Add(d.banterRow, 3, 4)
        stack:Add(d.readingLabel, 12)
        stack:Add(page.reading, 6, 8, 17)
        stack:Add(page.readingValue, 4, 8)
        stack:Add(d.turnsLabel, 12)
        stack:Add(page.turns, 6, 8, 17)
        stack:Add(page.turnsValue, 4, 8)
    else
        stack:Add(d.whyTitle)
        stack:Add(d.why, 6)
    end
    -- Regions that belong to the other view stay hidden.
    LA.SetShown(page.reading, showing == "hear")
    LA.SetShown(page.turns, showing == "hear")
    LA.SetShown(page.readingValue, showing == "hear")
    LA.SetShown(page.turnsValue, showing == "hear")
    page:SetDetailHeight(stack:Height())
end

local function ShowList()
    local items = { { header = true, key = "settings", text = "Settings" },
        { key = "hear", under = "settings", text = "What you hear", icon = "Interface\\Icons\\Spell_Holy_Silence" },
        { header = true, key = "lines", text = "Lines said to you", tag = #lines > 0 and tostring(#lines) or "" } }
    for _, line in ipairs(lines) do
        items[#items + 1] = { key = line.line, line = line, under = "lines",
            text = "|cffffd100" .. (line.speaker or "?") .. ":|r " .. Spoken(line.text, line.speaker),
            tag = CHANNEL_NAMES[line.channel] or "" }
    end
    page.list:SetItems(items)
    page.list:Choose(showing)
end

local function Explain(line)
    showing = line.line
    ShowList()
    local d = page.parts
    d.why:SetText("Asking...")
    Layout()
    LA.Bridge.Request({ op = "why", line = line.line }, function(reply)
        if showing ~= line.line then
            return
        end
        if not reply.ok then
            d.why:SetText(reply.reason or reply.error or "?")
            Layout()
            return
        end
        local text = { (reply.speaker or "?") .. " said:", Spoken(reply.text, reply.speaker) }
        if reply.started_by then
            local who = reply.started_by.who
            text[#text + 1] = "\nWhat started it"
            text[#text + 1] = who .. ": " .. Spoken(reply.started_by.text, who)
        end
        if reply.remembers_you and #reply.remembers_you > 0 then
            text[#text + 1] = "\nWhat " .. (reply.speaker or "they") .. " remembers about you"
            for _, note in ipairs(reply.remembers_you) do
                text[#text + 1] = "- " .. note.text
            end
        end
        if reply.actions and #reply.actions > 0 then
            text[#text + 1] = "\nWhat they did"
            for _, action in ipairs(reply.actions) do
                text[#text + 1] = "- " .. (action.action or "?") .. " (" .. (action.status or "?") .. ")"
            end
        end
        if reply.cost_dollars then
            text[#text + 1] = string.format("\nCost: $%.4f", reply.cost_dollars)
        end
        d.why:SetText(table.concat(text, "\n"))
        Layout()
    end)
end

local function LoadLines()
    LA.Bridge.Request({ op = "why" }, function(reply)
        lines = reply.ok and reply.lines or {}
        ShowList()
    end)
end

local function Build(self)
    page = self
    local UI = LA.UI

    page.budget = page.top:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    page.budget:SetPoint("LEFT", 4, 0)
    page.status = page.top:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    page.status:SetPoint("RIGHT", -6, 0)

    page.list = UI.List(page.left, function(item)
        LA.Manager.ScrollToTop()
        if item.line then
            Explain(item.line)
        else
            showing = "hear"
            ShowList()
            Layout()
        end
    end)

    local d = {}
    local parent = page.detail
    d.hearTitle = UI.Paper(parent, "title", 280)
    d.hearTitle:SetText("Who talks to you")
    d.hearNote = UI.Paper(parent, "small", 280)
    d.hearNote:SetText("Your own choice; other players keep theirs. Whispers always reach you.")
    for _, channel in ipairs(CHANNELS) do
        local label = UI.Paper(parent, "body", 280)
        label:SetText(channel[2])
        d["label:" .. channel[1]] = label
        d["row:" .. channel[1]] = RadioRow(parent, channel[1], HEARING, { 78, 142, 70 })
    end
    d.paceTitle = UI.Paper(parent, "title", 280)
    d.paceTitle:SetText("Pace")
    d.remarksLabel = UI.Paper(parent, "body", 280)
    d.remarksLabel:SetText("Remarks on their own")
    d.remarksRow = RadioRow(parent, "remarks", { REMARKS[1], REMARKS[2] }, { 110, 110 })
    d.remarksRow2 = RadioRow(parent, "remarks", { REMARKS[3], REMARKS[4] }, { 110, 110 })
    d.banterLabel = UI.Paper(parent, "body", 280)
    d.banterLabel:SetText("Combat banter")
    d.banterRow = RadioRow(parent, "banter", BANTER, { 76, 110, 70 })
    d.readingLabel = UI.Paper(parent, "body", 280)
    d.readingLabel:SetText("Reading speed")
    d.turnsLabel = UI.Paper(parent, "body", 280)
    d.turnsLabel:SetText("Longest exchange")
    d.whyTitle = UI.Paper(parent, "title", 280)
    d.whyTitle:SetText("Why did they say that?")
    d.why = UI.Paper(parent, "body", 280)
    page.parts = d
    page.stack = UI.Stack(parent)

    page.reading = Slider(parent, "Reading", 0, 400, 20, "Reading speed",
        "Sets the pause before the next line, so you can read each one.")
    page.readingValue = UI.Paper(parent, "small", 260)
    page.reading:SetScript("OnValueChanged", function(slider, value)
        if slider.updating then
            return
        end
        value = math.floor(value + 0.5)
        value = (value > 0 and value < 60) and 60 or value
        page.readingValue:SetText(value == 0 and "As the realm sets it" or value .. " words a minute")
    end)
    page.reading:SetScript("OnMouseUp", function(slider)
        local value = math.floor(slider:GetValue() + 0.5)
        Change({ reading = (value > 0 and value < 60) and 60 or value })
    end)
    page.turns = Slider(parent, "Turns", 0, 6, 1, "Longest exchange",
        "How many characters may answer one another before they stop.")
    page.turnsValue = UI.Paper(parent, "small", 260)
    page.turns:SetScript("OnValueChanged", function(slider, value)
        if slider.updating then
            return
        end
        value = math.floor(value + 0.5)
        page.turnsValue:SetText(value == 0 and "As the realm sets it" or value .. (value == 1 and " reply" or
            " replies"))
    end)
    page.turns:SetScript("OnMouseUp", function(slider)
        Change({ turns = math.floor(slider:GetValue() + 0.5) })
    end)

    local refresh = UI.Button(page.controls, "Refresh", 100)
    refresh:SetPoint("LEFT", 0, 1)
    refresh:SetScript("OnClick", LoadLines)
end

local function Show()
    Layout()
    LoadLines()
    LA.Bridge.Request({ op = "hearing" }, function(reply)
        if reply.ok then
            settings = reply.hearing
            ShowSettings()
        else
            Status("Not available: " .. (reply.reason or reply.error or "?"), 1, 0.3, 0.2)
        end
    end)
    LA.Bridge.Request({ op = "budget" }, function(reply)
        page.budget:SetText(reply.ok and string.format("Dialogue budget: $%.2f spent of $%.2f", reply.spent_dollars,
            reply.ceiling_dollars) or "")
    end)
end

LA.On("loaded", function()
    LA.Manager.AddPage("Dialogue", Build, Show)
end)
