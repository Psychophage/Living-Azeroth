-- SPDX-License-Identifier: GPL-2.0-or-later
-- The Guild tab: the guild's own account of itself, which its members carry into conversation the way a town
-- guard carries the Watch's (officers edit it; everyone reads it), and the rumours its members share (officers
-- correct, resolve or forget them). It sits beside the game's own guild window, not in place of it.
local _, LA = ...

local Guild = {}
LA.Guild = Guild

local FIELDS = {
    { "purpose", "What the guild is for" },
    { "values", "What members value" },
    { "traditions", "Traditions" },
    { "ambitions", "Current ambitions" },
    { "voice", "How members speak" },
}
local FADES = { { 6, "6 hours" }, { 24, "1 day" }, { 72, "3 days" }, { 168, "1 week" }, { 720, "30 days" } }

local page
local guild -- the server's last answer
local editing = false
local rumours = {}
local chosen -- the rumour shown

local function Status(text, r, g, b)
    page.status:SetText(text or "")
    page.status:SetTextColor(r or 0.8, g or 0.8, b or 0.8)
end

-- A text box of a fixed size: a multi-line edit box inside a frame (the edit box itself grows with its text).
local function Box(parent, height, letters)
    local frame = CreateFrame("Frame", nil, parent)
    frame:SetHeight(height)
    frame:SetBackdrop({ bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
        edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border", edgeSize = 10,
        insets = { left = 2, right = 2, top = 2, bottom = 2 } })
    frame:SetBackdropColor(0, 0, 0, 0.55)
    frame:SetBackdropBorderColor(0.5, 0.42, 0.25)
    local scroll = CreateFrame("ScrollFrame", nil, frame)
    scroll:SetPoint("TOPLEFT", 6, -5)
    scroll:SetPoint("BOTTOMRIGHT", -6, 5)
    local box = CreateFrame("EditBox", nil, scroll)
    box:SetMultiLine(true)
    box:SetMaxLetters(letters or 400)
    box:SetAutoFocus(false)
    box:SetFontObject("GameFontHighlightSmall")
    box:SetWidth(1)
    scroll:SetScrollChild(box)
    frame:SetScript("OnSizeChanged", function(self, width) box:SetWidth(width - 12) end)
    -- Keep the line being typed in view.
    box:SetScript("OnCursorChanged", function(_, _, y, _, lineHeight)
        local top = scroll:GetVerticalScroll()
        local visible = scroll:GetHeight()
        if -y < top then
            scroll:SetVerticalScroll(-y)
        elseif -y + lineHeight > top + visible then
            scroll:SetVerticalScroll(-y + lineHeight - visible)
        end
    end)
    scroll:EnableMouseWheel(true)
    scroll:SetScript("OnMouseWheel", function(self, delta)
        local range = math.max(0, box:GetHeight() - self:GetHeight())
        self:SetVerticalScroll(math.min(range, math.max(0, self:GetVerticalScroll() - delta * 14)))
    end)
    box:SetScript("OnEscapePressed", box.ClearFocus)
    frame:EnableMouse(true)
    frame:SetScript("OnMouseDown", function() box:SetFocus() end)
    box.frame = frame
    return box
end

local function FadeText(hours)
    for _, fade in ipairs(FADES) do
        if fade[1] == hours then
            return fade[2]
        end
    end
    return (hours or "?") .. " hours"
end

local function ShowIdentity()
    if not guild then
        return
    end
    local identity = guild.identity or {}
    LA.SetShown(page.reading, not editing)
    LA.SetShown(page.editing, editing)
    if editing then
        for _, field in ipairs(FIELDS) do
            local box = page.boxes[field[1]]
            if not box:HasFocus() and not box.dirty then
                box:SetText(identity[field[1]] or "")
            end
        end
    else
        local empty = true
        for _, field in ipairs(FIELDS) do
            local text = identity[field[1]] or ""
            page.texts[field[1]]:SetText(text ~= "" and text or "|cff9a8f7aNot written yet.|r")
            empty = empty and text == ""
        end
        LA.SetShown(page.emptyHint, empty)
    end
    UIDropDownMenu_SetText(page.fade, FadeText(identity.report_hours))
    page.edit:SetText(editing and SAVE or "Edit")
    LA.SetShown(page.edit, guild.may_edit)
    LA.SetShown(page.cancel, editing)
    page.who:SetText(guild.may_edit and "You may change it; every member reads it." or
        "Its officers write it; you can read it.")
end

local function Save(changes, done)
    Status("Saving...")
    LA.Bridge.Request({ op = "guild", change = changes }, function(reply)
        if reply.ok then
            guild = reply
            Status("Saved. Members carry it into their next conversation.", 0.5, 0.9, 0.5)
            if done then
                done()
            end
        else
            Status("Not saved: " .. (reply.reason or reply.error or "?"), 1, 0.3, 0.2)
        end
        ShowIdentity()
    end)
end

-- Rumours

local function Age(ms)
    local hours = math.floor((ms or 0) / 3600000)
    if hours < 1 then
        return "less than an hour"
    elseif hours < 48 then
        return hours .. (hours == 1 and " hour" or " hours")
    end
    return math.floor(hours / 24) .. " days"
end

local function ShowRumour()
    local rumour
    for _, candidate in ipairs(rumours) do
        if candidate.id == chosen then
            rumour = candidate
        end
    end
    page.rumourList:Choose(chosen)
    page.chosenRumour = rumour
    local mayChange = page.mayChange and rumour ~= nil
    for _, button in pairs(page.actions) do
        LA.SetShown(button, mayChange)
    end
    LA.SetShown(page.correction.frame, mayChange)
    LA.SetShown(page.correctionLabel, mayChange)
    if not rumour then
        page.rumour:SetText(#rumours == 0 and
            "No rumours yet. Only what a member saw for themselves becomes a rumour; private chat never does." or
            "Pick a rumour.")
        return
    end
    local by = type(rumour.by) == "table" and table.concat(rumour.by, ", ") or rumour.by
    local lines = { rumour.text or "", "" }
    lines[#lines + 1] = "|cffffd100Witnessed by|r  " .. ((by and by ~= "") and by or "?")
    if rumour.zone and rumour.zone ~= "" then
        lines[#lines + 1] = "|cffffd100Where|r  " .. rumour.zone
    end
    lines[#lines + 1] = "|cffffd100Seen|r  " .. Age(rumour.age_ms) .. " ago"
    lines[#lines + 1] = rumour.resolved and "|cff80c080Resolved|r" or ("|cffffd100Fades in|r  " ..
        Age(rumour.fades_in_ms))
    if rumour.corrected then
        lines[#lines + 1] = "|cff80c0ffCorrected by an officer|r"
    end
    page.rumour:SetText(table.concat(lines, "\n"))
    if mayChange and not page.correction:HasFocus() then
        page.correction:SetText(rumour.text or "")
    end
end

local function LoadRumours()
    LA.Bridge.Request({ op = "rumours" }, function(reply)
        rumours = reply.ok and reply.rumours or {}
        page.mayChange = reply.ok and reply.may_change
        local items = {}
        for _, rumour in ipairs(rumours) do
            rumour.key = rumour.id
            items[#items + 1] = rumour
        end
        page.rumourList:SetItems(items)
        ShowRumour()
    end)
end

local function ChangeRumour(action, text)
    local rumour = page.chosenRumour
    if not rumour then
        return
    end
    Status("Saving...")
    LA.Bridge.Request({ op = "rumours", change = { note = rumour.id, version = rumour.version, action = action,
        text = text } }, function(reply)
        if reply.ok then
            Status(action == "forget" and "Forgotten." or "Saved.", 0.5, 0.9, 0.5)
            if action == "forget" then
                chosen = nil
            end
        else
            Status("Not saved: " .. (reply.reason or reply.error or "?"), 1, 0.3, 0.2)
        end
        LoadRumours()
    end)
end

local function ShowPart(index)
    LA.SetShown(page.identityPart, index == 1)
    LA.SetShown(page.rumourPart, index == 2)
    LA.SetShown(page.edit, index == 1 and guild and guild.may_edit)
    LA.SetShown(page.cancel, index == 1 and editing)
    if index == 2 then
        LoadRumours()
    end
end

local function BuildIdentity(part)
    local UI = LA.UI
    page.reading = CreateFrame("Frame", nil, part)
    page.reading:SetAllPoints()
    page.texts = {}
    local y = -8
    for i, field in ipairs(FIELDS) do
        local column, row = (i - 1) % 2, math.floor((i - 1) / 2)
        local heading = UI.Heading(page.reading, field[2])
        heading:SetPoint("TOPLEFT", 10 + column * 320, -8 - row * 92)
        local text = UI.Text(page.reading, 300)
        text:SetPoint("TOPLEFT", heading, "BOTTOMLEFT", 0, -4)
        text:SetHeight(64)
        page.texts[field[1]] = text
    end
    page.emptyHint = UI.Text(page.reading, 300, "GameFontNormal")
    page.emptyHint:SetPoint("TOPLEFT", 330, -196)
    page.emptyHint:SetText("Officers can write it with Edit, or start from a few words.")
    local carry = UI.Text(page.reading, 300, "GameFontDisableSmall")
    carry:SetPoint("BOTTOMLEFT", 330, 8)
    carry:SetTextColor(0.75, 0.7, 0.6)
    carry:SetText("Members carry this into conversation, the way a guard carries the Watch's description.")

    page.editing = CreateFrame("Frame", nil, part)
    page.editing:SetAllPoints()
    page.boxes = {}
    for i, field in ipairs(FIELDS) do
        local column, row = (i - 1) % 2, math.floor((i - 1) / 2)
        local label = page.editing:CreateFontString(nil, "OVERLAY", "GameFontNormal")
        label:SetPoint("TOPLEFT", 10 + column * 320, -8 - row * 82)
        label:SetText(field[2])
        local box = Box(page.editing, 58)
        box.frame:SetPoint("TOPLEFT", label, "BOTTOMLEFT", -2, -3)
        box.frame:SetWidth(300)
        box:SetWidth(288)
        box:SetScript("OnTextChanged", function(self, typed)
            if typed then
                self.dirty = true
            end
        end)
        page.boxes[field[1]] = box
    end
    local seedLabel = page.editing:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    seedLabel:SetPoint("TOPLEFT", 330, -172)
    seedLabel:SetText("Start from a few words")
    page.seed = CreateFrame("EditBox", nil, page.editing, "InputBoxTemplate")
    page.seed:SetSize(180, 20)
    page.seed:SetPoint("TOPLEFT", seedLabel, "BOTTOMLEFT", 6, -4)
    page.seed:SetAutoFocus(false)
    page.seed:SetMaxLetters(120)
    page.draft = UI.Button(page.editing, "Write a draft", 110)
    page.draft:SetPoint("LEFT", page.seed, "RIGHT", 6, 0)
    local draftHint = UI.Text(page.editing, 300, "GameFontDisableSmall")
    draftHint:SetPoint("TOPLEFT", page.seed, "BOTTOMLEFT", -6, -6)
    draftHint:SetTextColor(0.75, 0.7, 0.6)
    draftHint:SetText("One model call from the dialogue budget. The draft fills the boxes; nothing is saved until " ..
        "you press Save.")
    page.draft:SetScript("OnClick", function()
        local seed = strtrim(page.seed:GetText())
        if seed == "" then
            Status("Write a few words first.", 1, 0.8, 0.2)
            return
        end
        Status("Writing a draft...")
        page.draft:Disable()
        LA.Bridge.Request({ op = "guild", draft = seed }, function(reply)
            page.draft:Enable()
            if reply.ok and reply.draft then
                for _, field in ipairs(FIELDS) do
                    page.boxes[field[1]]:SetText(reply.draft[field[1]] or "")
                    page.boxes[field[1]].dirty = true
                end
                Status("A draft is in the boxes. Change it, then Save.", 0.5, 0.9, 0.5)
            else
                Status("No draft: " .. (reply.reason or reply.error or "?"), 1, 0.3, 0.2)
            end
        end, 60)
    end)
end

local function BuildRumours(part)
    local UI = LA.UI
    local heading = UI.Heading(part, "What members have seen")
    heading:SetPoint("TOPLEFT", 10, -8)
    page.rumourList = UI.List(part, 8, 34, function(row)
        row.text = row:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
        row.text:SetPoint("TOPLEFT", 6, -3)
        row.text:SetPoint("BOTTOMRIGHT", -60, 3)
        row.text:SetJustifyH("LEFT")
        row.text:SetJustifyV("TOP")
        row.state = row:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
        row.state:SetPoint("RIGHT", -4, 0)
    end, function(row, rumour)
        row.text:SetText((rumour.resolved and "|cff9a8f7a" or "") .. (rumour.text or ""))
        row.state:SetText(rumour.resolved and "Resolved" or (rumour.corrected and "Corrected" or ""))
    end, function(rumour)
        chosen = rumour.id
        ShowRumour()
    end)
    page.rumourList.frame:SetPoint("TOPLEFT", 4, -30)
    page.rumourList.frame:SetSize(330, 8 * 34)

    local detail = UI.Heading(part, "The rumour")
    detail:SetPoint("TOPLEFT", 360, -8)
    page.rumour = UI.Text(part, 270)
    page.rumour:SetPoint("TOPLEFT", detail, "BOTTOMLEFT", 0, -6)
    page.rumour:SetPoint("BOTTOM", part, "BOTTOM", 0, 118)
    page.correction = Box(part, 64, 4000)
    page.correction.frame:SetPoint("BOTTOMLEFT", part, "BOTTOMLEFT", 356, 34)
    page.correction.frame:SetWidth(280)
    page.correction:SetWidth(268)
    local correctionLabel = part:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    correctionLabel:SetPoint("BOTTOMLEFT", page.correction.frame, "TOPLEFT", 2, 2)
    correctionLabel:SetText("Correct it to")
    page.correctionLabel = correctionLabel
    page.actions = {}
    local function Action(name, text, x, run)
        local button = UI.Button(part, text, 90)
        button:SetPoint("BOTTOMLEFT", part, "BOTTOMLEFT", x, 6)
        button:SetScript("OnClick", run)
        page.actions[name] = button
    end
    Action("correct", "Correct", 356, function() ChangeRumour("correct", page.correction:GetText()) end)
    Action("resolve", "Resolve", 450, function() ChangeRumour("resolve") end)
    Action("forget", "Forget", 544, function() ChangeRumour("forget") end)
end

local function Build(self)
    page = self
    local UI = LA.UI

    -- Header: the guild's name and who is in it.
    page.name = page.header:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
    page.name:SetPoint("TOPLEFT", 64, -12)
    page.members = page.header:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
    page.members:SetPoint("TOPLEFT", page.name, "BOTTOMLEFT", 0, -6)
    page.who = page.header:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    page.who:SetPoint("TOPLEFT", page.members, "BOTTOMLEFT", 0, -4)
    page.who:SetTextColor(0.8, 0.8, 0.8)
    local guildWindow = UI.Button(page.header, "Guild window", 120)
    guildWindow:SetPoint("TOPRIGHT", -20, -12)
    guildWindow:SetScript("OnClick", function() ToggleFriendsFrame(3) end)

    page.parts = UI.SubTabs(page.bar, { "Who we are", "Rumours" }, ShowPart)
    page.parts:Select(1)
    local fadeLabel = page.bar:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    fadeLabel:SetPoint("RIGHT", -170, 0)
    fadeLabel:SetText("Rumours fade after")
    page.fade = CreateFrame("Frame", "LivingAzerothGuildFade", page.bar, "UIDropDownMenuTemplate")
    page.fade:SetPoint("LEFT", fadeLabel, "RIGHT", -10, -2)
    UIDropDownMenu_SetWidth(page.fade, 90)
    UIDropDownMenu_Initialize(page.fade, function()
        for _, fade in ipairs(FADES) do
            local info = UIDropDownMenu_CreateInfo()
            info.text = fade[2]
            info.checked = guild and guild.identity and guild.identity.report_hours == fade[1]
            info.disabled = not (guild and guild.may_edit)
            info.func = function() Save({ report_hours = fade[1] }) end
            UIDropDownMenu_AddButton(info)
        end
    end)

    page.none = UI.Text(page.content, 600, "GameFontNormal")
    page.none:SetPoint("TOPLEFT", 10, -10)
    page.identityPart = CreateFrame("Frame", nil, page.content)
    page.identityPart:SetAllPoints()
    BuildIdentity(page.identityPart)
    page.rumourPart = CreateFrame("Frame", nil, page.content)
    page.rumourPart:SetAllPoints()
    BuildRumours(page.rumourPart)

    page.edit = UI.Button(page.footer, "Edit", 100)
    page.edit:SetPoint("LEFT", 0, 0)
    page.edit:SetScript("OnClick", function()
        if not editing then
            editing = true
            for _, field in ipairs(FIELDS) do
                page.boxes[field[1]].dirty = false
            end
            ShowIdentity()
            return
        end
        local changes = {}
        for _, field in ipairs(FIELDS) do
            changes[field[1]] = page.boxes[field[1]]:GetText()
            page.boxes[field[1]]:ClearFocus()
        end
        Save(changes, function() editing = false end)
    end)
    page.cancel = UI.Button(page.footer, CANCEL, 100)
    page.cancel:SetPoint("LEFT", page.edit, "RIGHT", 4, 0)
    page.cancel:SetScript("OnClick", function()
        editing = false
        Status("")
        ShowIdentity()
    end)
    page.status = page.footer:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    page.status:SetPoint("LEFT", page.cancel, "RIGHT", 10, 0)
    LA.Manager.CloseButton(page)
end

local function Show()
    LA.Bridge.Request({ op = "guild" }, function(reply)
        if reply.ok then
            guild = reply
            page.none:Hide()
            page.name:SetText("<" .. (reply.name or "?") .. ">")
            local members = reply.members or 0
            local bots = reply.bots_online or 0
            page.members:SetText(members .. (members == 1 and " member" or " members") .. (bots > 0 and
                (" - " .. bots .. (bots == 1 and " bot" or " bots") .. " online now") or ""))
            ShowIdentity()
            ShowPart(page.parts.selected or 1)
        else
            guild = nil
            page.name:SetText("")
            page.members:SetText("")
            page.who:SetText("")
            page.identityPart:Hide()
            page.rumourPart:Hide()
            page.edit:Hide()
            page.cancel:Hide()
            page.none:Show()
            page.none:SetText(reply.reason == "not in a guild" and "You are not in a guild." or
                ("Not available: " .. (reply.reason or reply.error or "?")))
        end
    end)
end

LA.On("loaded", function()
    LA.Manager.AddPage("Guild", Build, Show)
end)
