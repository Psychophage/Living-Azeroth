-- SPDX-License-Identifier: GPL-2.0-or-later
-- The Dialogue tab: what the player hears from characters (their own settings, kept by the server), and why a
-- character said a line the player heard. Administrators also see the dialogue budget (read-only).
local _, LA = ...

local Dialogue = {}
LA.Dialogue = Dialogue

local CHANNELS = {
    { "party", "Your party", "Bots you group with" },
    { "nearby", "Nearby", "Characters speaking around you" },
    { "guild", "Guild", "Guild chat" },
    { "general", "General", "The zone's General channel" },
}
local HEARING = { { "chatty", "Chatty" }, { "spoken", "When spoken to" }, { "silent", "Silent" } }
local REMARKS = { { "often", "Often" }, { "sometimes", "Sometimes" }, { "rarely", "Rarely" }, { "never", "Never" } }
local BANTER = { { "off", "Off" }, { "sometimes", "Sometimes" }, { "often", "Often" } }

local page
local settings -- the server's copy, once it has answered
local radios = {} -- setting -> { value -> radio }
local lines = {}
local chosenLine

local function Status(text, r, g, b)
    page.status:SetText(text or "")
    page.status:SetTextColor(r or 0.8, g or 0.8, b or 0.8)
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
    page.reading.updating = true
    page.reading:SetValue(settings.reading or 0)
    page.turns:SetValue(settings.turns or 0)
    page.reading.updating = false
    page.readingValue:SetText(settings.reading == 0 and "The realm's" or settings.reading .. " words a minute")
    page.turnsValue:SetText(settings.turns == 0 and "The realm's" or settings.turns .. (settings.turns == 1 and
        " reply" or " replies"))
end

local function Change(fields)
    Status("Saving...")
    LA.Bridge.Request({ op = "hearing", change = fields }, function(reply)
        if reply.ok then
            settings = reply.hearing
            Status("Saved.", 0.5, 0.9, 0.5)
        else
            Status("Not saved: " .. (reply.reason or reply.error or "?"), 1, 0.3, 0.2)
        end
        ShowSettings()
    end)
end

local function RadioRow(parent, key, options, x, y, width)
    radios[key] = radios[key] or {}
    for i, option in ipairs(options) do
        local radio = CreateFrame("CheckButton", nil, parent, "UIRadioButtonTemplate")
        radio:SetPoint("TOPLEFT", x + (i - 1) * width, y)
        local label = radio:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
        label:SetPoint("LEFT", radio, "RIGHT", 2, 0)
        label:SetText(option[2])
        radio:SetScript("OnClick", function()
            ShowSettings() -- the dot moves once the server has it
            Change({ [key] = option[1] })
        end)
        radios[key][option[1]] = radio
    end
end

local function Slider(parent, name, low, high, step, x, y)
    local slider = CreateFrame("Slider", "LivingAzerothDialogue" .. name, parent, "OptionsSliderTemplate")
    slider:SetPoint("TOPLEFT", x, y)
    slider:SetWidth(200)
    slider:SetMinMaxValues(low, high)
    slider:SetValueStep(step)
    _G[slider:GetName() .. "Low"]:SetText("")
    _G[slider:GetName() .. "High"]:SetText("")
    _G[slider:GetName() .. "Text"]:SetText("")
    return slider
end

-- Why did they say that?

-- A recorded line without the speaker's name in front, which the records keep.
local function Spoken(text, speaker)
    text = text or ""
    if speaker and text:sub(1, #speaker + 2) == speaker .. ": " then
        return text:sub(#speaker + 3)
    end
    return text
end

local function Explain(line)
    chosenLine = line
    for _, row in ipairs(page.rows) do
        if row.line == line then
            row:LockHighlight()
        else
            row:UnlockHighlight()
        end
    end
    page.why:SetText("Asking...")
    LA.Bridge.Request({ op = "why", line = line }, function(reply)
        if chosenLine ~= line then
            return
        end
        if not reply.ok then
            page.why:SetText("|cffff5040" .. (reply.reason or reply.error or "?") .. "|r")
            return
        end
        local text = { "|cffffd100" .. (reply.speaker or "?") .. "|r said: " .. Spoken(reply.text, reply.speaker) }
        if reply.started_by then
            local who = reply.started_by.who
            text[#text + 1] = "\n|cffffd100What started it|r\n" .. who .. ": " .. Spoken(reply.started_by.text, who)
        end
        if reply.remembers_you and #reply.remembers_you > 0 then
            text[#text + 1] = "\n|cffffd100What " .. (reply.speaker or "they") .. " remembers about you|r"
            for _, note in ipairs(reply.remembers_you) do
                text[#text + 1] = "- " .. note.text
            end
        end
        if reply.actions and #reply.actions > 0 then
            text[#text + 1] = "\n|cffffd100What they did|r"
            for _, action in ipairs(reply.actions) do
                text[#text + 1] = "- " .. (action.action or "?") .. " (" .. (action.status or "?") .. ")"
            end
        end
        if reply.cost_dollars then
            text[#text + 1] = string.format("\n|cffffd100Cost|r $%.4f", reply.cost_dollars)
        end
        page.why:SetText(table.concat(text, "\n"))
    end)
end

local function ShowLines()
    for i, row in ipairs(page.rows) do
        local line = lines[i]
        if line then
            row.line = line.line
            row.text:SetText("|cffffd100" .. (line.speaker or "?") .. "|r " .. Spoken(line.text, line.speaker))
            row:Show()
        else
            row.line = nil
            row:Hide()
        end
    end
    LA.SetShown(page.noLines, #lines == 0)
end

local function LoadLines()
    LA.Bridge.Request({ op = "why" }, function(reply)
        lines = reply.ok and reply.lines or {}
        ShowLines()
    end)
end

local function Build(self)
    page = self
    local inset = LA.Manager.Inset(page)
    inset:SetPoint("TOPLEFT", 0, -2)
    inset:SetPoint("BOTTOMLEFT", 0, 0)
    inset:SetWidth(390)

    LA.Manager.Heading(page, "Who talks to you", 12, -12)
    local y = -34
    for _, channel in ipairs(CHANNELS) do
        local label = page:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
        label:SetPoint("TOPLEFT", 14, y - 2)
        label:SetText(channel[2])
        RadioRow(page, channel[1], HEARING, 96, y, 110)
        y = y - 24
    end
    LA.Manager.Heading(page, "Remarks on their own", 12, y - 10)
    RadioRow(page, "remarks", REMARKS, 14, y - 30, 90)
    LA.Manager.Heading(page, "Combat banter", 12, y - 60)
    RadioRow(page, "banter", BANTER, 14, y - 80, 90)

    LA.Manager.Heading(page, "Reading speed", 12, y - 116)
    page.reading = Slider(page, "Reading", 0, 400, 20, 16, y - 138)
    page.readingValue = page:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    page.readingValue:SetPoint("LEFT", page.reading, "RIGHT", 10, 0)
    page.reading:SetScript("OnValueChanged", function(slider, value)
        if slider.updating then
            return
        end
        value = math.floor(value + 0.5)
        if value > 0 and value < 60 then
            value = 60
        end
        page.readingValue:SetText(value == 0 and "The realm's" or value .. " words a minute")
    end)
    page.reading:SetScript("OnMouseUp", function(slider)
        local value = math.floor(slider:GetValue() + 0.5)
        Change({ reading = (value > 0 and value < 60) and 60 or value })
    end)
    local readingHint = page:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
    readingHint:SetPoint("TOPLEFT", page.reading, "BOTTOMLEFT", 0, -4)
    readingHint:SetText("The pause before the next line, so you can read each one.")

    LA.Manager.Heading(page, "Longest exchange", 12, y - 182)
    page.turns = Slider(page, "Turns", 0, 6, 1, 16, y - 204)
    page.turnsValue = page:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    page.turnsValue:SetPoint("LEFT", page.turns, "RIGHT", 10, 0)
    page.turns:SetScript("OnValueChanged", function(slider, value)
        if slider.updating then
            return
        end
        value = math.floor(value + 0.5)
        page.turnsValue:SetText(value == 0 and "The realm's" or value .. (value == 1 and " reply" or " replies"))
    end)
    page.turns:SetScript("OnMouseUp", function(slider)
        Change({ turns = math.floor(slider:GetValue() + 0.5) })
    end)
    local turnsHint = page:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
    turnsHint:SetPoint("TOPLEFT", page.turns, "BOTTOMLEFT", 0, -4)
    turnsHint:SetText("How many characters may answer one another before they stop.")

    page.status = page:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    page.status:SetPoint("BOTTOMLEFT", 14, 10)
    page.budget = page:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
    page.budget:SetPoint("BOTTOMRIGHT", inset, "BOTTOMRIGHT", -12, 10)

    LA.Manager.Heading(page, "Why did they say that?", 406, -4)
    local list = LA.Manager.Inset(page)
    list:SetPoint("TOPLEFT", 400, -22)
    list:SetPoint("TOPRIGHT", 0, -22)
    list:SetHeight(190)
    page.rows = {}
    for i = 1, 10 do
        local row = CreateFrame("Button", nil, list)
        row:SetSize(270, 17)
        row:SetPoint("TOPLEFT", 6, -6 - (i - 1) * 18)
        row.text = row:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
        row.text:SetAllPoints()
        row.text:SetJustifyH("LEFT")
        row:SetHighlightTexture("Interface\\QuestFrame\\UI-QuestTitleHighlight", "ADD")
        row:SetScript("OnClick", function(self)
            Explain(self.line)
        end)
        page.rows[i] = row
    end
    page.noLines = list:CreateFontString(nil, "OVERLAY", "GameFontDisable")
    page.noLines:SetPoint("CENTER")
    page.noLines:SetText("No lines said to you yet.")
    local explanation = LA.Manager.Inset(page)
    explanation:SetPoint("TOPLEFT", list, "BOTTOMLEFT", 0, -6)
    explanation:SetPoint("BOTTOMRIGHT", 0, 0)
    page.why = explanation:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    page.why:SetPoint("TOPLEFT", 10, -10)
    page.why:SetPoint("BOTTOMRIGHT", -10, 10)
    page.why:SetJustifyH("LEFT")
    page.why:SetJustifyV("TOP")
    page.why:SetText("Pick a line to see what started it, what the speaker remembers about you, and what it cost.")
end

local function Show()
    LA.Bridge.Request({ op = "hearing" }, function(reply)
        if reply.ok then
            settings = reply.hearing
            ShowSettings()
        else
            Status("Not available: " .. (reply.reason or reply.error or "?"), 1, 0.3, 0.2)
        end
    end)
    LA.Bridge.Request({ op = "budget" }, function(reply)
        if reply.ok then
            page.budget:SetText(string.format("Dialogue budget: $%.2f of $%.2f", reply.spent_dollars,
                reply.ceiling_dollars))
        else
            page.budget:SetText("")
        end
    end)
    LoadLines()
end

LA.On("loaded", function()
    LA.Manager.AddPage("Dialogue", Build, Show)
end)
