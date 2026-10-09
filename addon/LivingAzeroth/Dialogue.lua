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

local function Radio(parent, key, value, label)
    radios[key] = radios[key] or {}
    local radio = CreateFrame("CheckButton", nil, parent, "UIRadioButtonTemplate")
    if label then
        local text = radio:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
        text:SetPoint("LEFT", radio, "RIGHT", 2, 0)
        text:SetText(label)
        radio:SetHitRectInsets(0, -text:GetStringWidth() - 4, 0, 0)
    end
    radio:SetScript("OnClick", function()
        ShowSettings() -- the dot moves once the server has it
        Change({ [key] = value })
    end)
    radios[key][value] = radio
    return radio
end

local function Slider(parent, name, low, high, step)
    local slider = CreateFrame("Slider", "LivingAzerothDialogue" .. name, parent, "OptionsSliderTemplate")
    slider:SetWidth(220)
    slider:SetMinMaxValues(low, high)
    slider:SetValueStep(step)
    _G[slider:GetName() .. "Low"]:SetText("")
    _G[slider:GetName() .. "High"]:SetText("")
    _G[slider:GetName() .. "Text"]:SetText("")
    return slider
end

local function Explained(slider, title, text)
    slider:SetScript("OnEnter", function(self)
        GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
        GameTooltip:AddLine(title, 1, 1, 1)
        GameTooltip:AddLine(text, nil, nil, nil, true)
        GameTooltip:Show()
    end)
    slider:SetScript("OnLeave", GameTooltip_Hide)
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

local CHANNEL_NAMES = { whisper = "Whisper", party = "Party", raid = "Raid", say = "Say", yell = "Yell",
    guild = "Guild", general = "General", emote = "Emote" }

local function Explain(line)
    chosenLine = line
    page.lines:Choose(line)
    page.why:SetText("Asking...")
    LA.Bridge.Request({ op = "why", line = line }, function(reply)
        if chosenLine ~= line then
            return
        end
        if not reply.ok then
            page.why:SetText("|cffff5040" .. (reply.reason or reply.error or "?") .. "|r")
            return
        end
        local speaker = "|cffffd100" .. (reply.speaker or "?") .. "|r"
        local spoken = Spoken(reply.text, reply.speaker)
        -- An emote is an action ("grips his staff"), not a quote.
        local text = { reply.channel == "emote" and (speaker .. " " .. spoken) or (speaker .. " said: " .. spoken) }
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

local function LoadLines()
    LA.Bridge.Request({ op = "why" }, function(reply)
        local items = {}
        for _, line in ipairs(reply.ok and reply.lines or {}) do
            line.key = line.line
            items[#items + 1] = line
        end
        page.lines:SetItems(items)
        LA.SetShown(page.noLines, #items == 0)
    end)
end

local function ShowPart(index)
    LA.SetShown(page.settingsPart, index == 1)
    LA.SetShown(page.whyPart, index == 2)
    LA.SetShown(page.refresh, index == 2)
    if index == 2 then
        LoadLines()
    end
end

local function BuildSettings(part)
    local UI = LA.UI
    local heading = UI.Heading(part, "Who talks to you")
    heading:SetPoint("TOPLEFT", 10, -8)
    local columns = { 300, 390, 500 }
    -- Each column's heading is centred over its buttons (a radio button is 16 wide, its dot in the middle).
    for i, option in ipairs(HEARING) do
        local label = part:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
        label:SetPoint("BOTTOM", part, "TOPLEFT", columns[i] + 8, -46)
        label:SetText(option[2])
    end
    for row, channel in ipairs(CHANNELS) do
        local y = -48 - (row - 1) * 24
        local name = part:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
        name:SetPoint("TOPLEFT", 14, y - 4)
        name:SetWidth(80)
        name:SetJustifyH("LEFT")
        name:SetText(channel[2])
        local hint = part:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
        hint:SetPoint("LEFT", name, "RIGHT", 4, 0)
        hint:SetTextColor(0.75, 0.7, 0.6)
        hint:SetText(channel[3])
        for i, option in ipairs(HEARING) do
            local radio = Radio(part, channel[1], option[1])
            radio:SetSize(16, 16)
            radio:SetPoint("TOPLEFT", columns[i], y - 2)
        end
    end
    local note = UI.Text(part, 600, "GameFontDisableSmall")
    note:SetPoint("TOPLEFT", 14, -148)
    note:SetTextColor(0.75, 0.7, 0.6)
    note:SetText("Your own choice; other players keep theirs. Whispers always reach you.")

    local pace = UI.Heading(part, "Pace")
    pace:SetPoint("TOPLEFT", 10, -170)
    local remarks = part:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
    remarks:SetPoint("TOPLEFT", 14, -198)
    remarks:SetText("Remarks on their own")
    for i, option in ipairs(REMARKS) do
        Radio(part, "remarks", option[1], option[2]):SetPoint("TOPLEFT", 180 + (i - 1) * 100, -194)
    end
    local banter = part:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
    banter:SetPoint("TOPLEFT", 14, -224)
    banter:SetText("Combat banter")
    for i, option in ipairs(BANTER) do
        Radio(part, "banter", option[1], option[2]):SetPoint("TOPLEFT", 180 + (i - 1) * 100, -220)
    end

    local reading = part:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
    reading:SetPoint("TOPLEFT", 14, -260)
    reading:SetText("Reading speed")
    page.reading = Slider(part, "Reading", 0, 400, 20)
    page.reading:SetPoint("TOPLEFT", 184, -258)
    page.readingValue = part:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    page.readingValue:SetPoint("LEFT", page.reading, "RIGHT", 12, 0)
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
    Explained(page.reading, "Reading speed", "Sets the pause before the next line, so you can read each one.")

    local turns = part:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
    turns:SetPoint("TOPLEFT", 14, -294)
    turns:SetText("Longest exchange")
    page.turns = Slider(part, "Turns", 0, 6, 1)
    page.turns:SetPoint("TOPLEFT", 184, -292)
    page.turnsValue = part:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    page.turnsValue:SetPoint("LEFT", page.turns, "RIGHT", 12, 0)
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
    Explained(page.turns, "Longest exchange", "How many characters may answer one another before they stop.")
end

local function BuildWhy(part)
    local UI = LA.UI
    local heading = UI.Heading(part, "Lines said to you")
    heading:SetPoint("TOPLEFT", 10, -8)
    page.lines = UI.List(part, 12, 24, function(row)
        row.channel = row:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
        row.channel:SetPoint("LEFT", 4, 0)
        row.channel:SetWidth(50)
        row.channel:SetJustifyH("LEFT")
        row.channel:SetTextColor(0.75, 0.7, 0.6)
        row.text = row:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
        row.text:SetPoint("LEFT", row.channel, "RIGHT", 2, 0)
        row.text:SetPoint("RIGHT", -4, 0)
        row.text:SetJustifyH("LEFT")
    end, function(row, line)
        row.channel:SetText(CHANNEL_NAMES[line.channel] or line.channel or "")
        row.text:SetText("|cffffd100" .. (line.speaker or "?") .. ":|r " .. Spoken(line.text, line.speaker))
    end, function(line)
        Explain(line.line)
    end)
    page.lines.frame:SetPoint("TOPLEFT", 4, -30)
    page.lines.frame:SetSize(330, 12 * 24)
    page.noLines = UI.Text(part, 300, "GameFontDisable")
    page.noLines:SetPoint("TOPLEFT", 12, -36)
    page.noLines:SetText("No lines said to you yet.")

    local explanation = UI.Heading(part, "Why did they say that?")
    explanation:SetPoint("TOPLEFT", 360, -8)
    page.why = UI.Text(part, 270)
    page.why:SetPoint("TOPLEFT", explanation, "BOTTOMLEFT", 0, -6)
    page.why:SetText("Pick a line to see what started it, what the speaker remembers about you, and what it cost.")
end

local function Build(self)
    page = self
    local UI = LA.UI

    -- Header: what this is, and the budget for administrators.
    local intro = UI.Text(page.header, 360)
    intro:SetPoint("TOPLEFT", 64, -12)
    intro:SetText("Choose how much characters say to you, and see why they said what they did.")
    page.budget = page.header:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    page.budget:SetPoint("TOPRIGHT", -20, -14)
    page.budgetBar = CreateFrame("StatusBar", nil, page.header)
    page.budgetBar:SetStatusBarTexture("Interface\\TargetingFrame\\UI-StatusBar")
    page.budgetBar:SetStatusBarColor(0.9, 0.7, 0.1)
    page.budgetBar:SetSize(190, 12)
    page.budgetBar:SetPoint("TOPRIGHT", page.budget, "BOTTOMRIGHT", 0, -6)
    page.budgetBar:SetMinMaxValues(0, 1)
    local barBack = page.budgetBar:CreateTexture(nil, "BACKGROUND")
    barBack:SetTexture(0, 0, 0, 0.6)
    barBack:SetAllPoints()
    page.budgetBar:Hide()

    page.parts = UI.SubTabs(page.bar, { "What you hear", "Why did they say that?" }, ShowPart)
    page.parts:Select(1)

    page.settingsPart = CreateFrame("Frame", nil, page.content)
    page.settingsPart:SetAllPoints()
    BuildSettings(page.settingsPart)
    page.whyPart = CreateFrame("Frame", nil, page.content)
    page.whyPart:SetAllPoints()
    BuildWhy(page.whyPart)

    page.refresh = UI.Button(page.footer, "Refresh", 100)
    page.refresh:SetPoint("LEFT", 0, 0)
    page.refresh:SetScript("OnClick", LoadLines)
    page.status = page.footer:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    page.status:SetPoint("LEFT", 110, 0)
    LA.Manager.CloseButton(page)
    ShowPart(1)
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
            page.budgetBar:SetValue(reply.ceiling_dollars > 0 and reply.spent_dollars / reply.ceiling_dollars or 0)
            page.budgetBar:Show()
        else
            page.budget:SetText("")
            page.budgetBar:Hide()
        end
    end)
    if page.parts.selected == 2 then
        LoadLines()
    end
end

LA.On("loaded", function()
    LA.Manager.AddPage("Dialogue", Build, Show)
end)
