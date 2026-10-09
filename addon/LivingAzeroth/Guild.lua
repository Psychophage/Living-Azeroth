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
local rumours = {}
local chosen -- the rumour shown

local function Status(text, r, g, b)
    page.status:SetText(text or "")
    page.status:SetTextColor(r or 0.8, g or 0.8, b or 0.8)
end

-- A text box of a fixed size: a multi-line edit box inside a frame (the edit box itself grows with its text).
-- Returns the edit box; place it with box.frame.
local function Box(parent, height)
    local frame = CreateFrame("Frame", nil, parent)
    frame:SetHeight(height)
    frame:SetBackdrop({ bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
        edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border", edgeSize = 10,
        insets = { left = 2, right = 2, top = 2, bottom = 2 } })
    frame:SetBackdropColor(0, 0, 0, 0.6)
    frame:SetBackdropBorderColor(0.4, 0.4, 0.4)
    local box = CreateFrame("EditBox", nil, frame)
    box:SetMultiLine(true)
    box:SetMaxLetters(400)
    box:SetAutoFocus(false)
    box:SetFontObject("GameFontHighlightSmall")
    box:SetPoint("TOPLEFT", 6, -5)
    box:SetPoint("TOPRIGHT", -6, -5)
    box:SetHeight(height - 10)
    box:SetScript("OnEscapePressed", box.ClearFocus)
    frame:EnableMouse(true)
    frame:SetScript("OnMouseDown", function()
        if box:IsMouseEnabled() then
            box:SetFocus()
        end
    end)
    box.frame = frame
    return box
end

local function ShowIdentity()
    if not guild then
        return
    end
    page.name:SetText("<" .. (guild.name or "?") .. ">")
    local identity = guild.identity or {}
    for _, field in ipairs(FIELDS) do
        local box = page.boxes[field[1]]
        if not box:HasFocus() then
            box:SetText(identity[field[1]] or "")
        end
        if guild.may_edit then
            box:EnableMouse(true)
            box:SetTextColor(1, 1, 1)
        else
            box:EnableMouse(false)
            box:ClearFocus()
            box:SetTextColor(0.85, 0.82, 0.75)
        end
    end
    UIDropDownMenu_SetText(page.fade, "?")
    for _, fade in ipairs(FADES) do
        if fade[1] == identity.report_hours then
            UIDropDownMenu_SetText(page.fade, fade[2])
        end
    end
    if not UIDropDownMenu_GetText(page.fade) or UIDropDownMenu_GetText(page.fade) == "?" then
        UIDropDownMenu_SetText(page.fade, (identity.report_hours or "?") .. " hours")
    end
    LA.SetShown(page.save, guild.may_edit)
    LA.SetShown(page.draftArea, guild.may_edit)
    page.who:SetText(guild.may_edit and "You may change it; every member reads it." or
        "Its officers write it; you can read it.")
end

local function Save(changes)
    Status("Saving...")
    LA.Bridge.Request({ op = "guild", change = changes }, function(reply)
        if reply.ok then
            guild = reply
            Status("Saved. Members carry it into their next conversation.", 0.5, 0.9, 0.5)
            ShowIdentity()
        else
            Status("Not saved: " .. (reply.reason or reply.error or "?"), 1, 0.3, 0.2)
        end
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
    for _, row in ipairs(page.rows) do
        if row.id and row.id == chosen then
            row:LockHighlight()
        else
            row:UnlockHighlight()
        end
    end
    if not rumour then
        page.rumour:SetText(#rumours == 0 and "No rumours yet. Only what a member saw for themselves becomes a " ..
            "rumour; private chat never does." or "Pick a rumour.")
        page.correction.frame:Hide()
        for _, button in pairs(page.actions) do
            button:Hide()
        end
        return
    end
    local lines = { rumour.text or "" }
    local by = type(rumour.by) == "table" and table.concat(rumour.by, ", ") or rumour.by
    lines[#lines + 1] = "\n|cffffd100Witnessed by|r " .. (by ~= "" and by or "?")
    if rumour.zone and rumour.zone ~= "" then
        lines[#lines + 1] = "|cffffd100Where|r " .. rumour.zone
    end
    lines[#lines + 1] = "|cffffd100Seen|r " .. Age(rumour.age_ms) .. " ago"
    if rumour.resolved then
        lines[#lines + 1] = "|cff80c080Resolved|r"
    else
        lines[#lines + 1] = "|cffffd100Fades in|r " .. Age(rumour.fades_in_ms)
    end
    if rumour.corrected then
        lines[#lines + 1] = "|cff80c0ffCorrected by an officer|r"
    end
    page.rumour:SetText(table.concat(lines, "\n"))
    local mayChange = page.mayChange
    for _, button in pairs(page.actions) do
        LA.SetShown(button, mayChange)
    end
    LA.SetShown(page.correction.frame, mayChange)
    if mayChange and not page.correction:HasFocus() then
        page.correction:SetText(rumour.text or "")
    end
    page.chosenRumour = rumour
end

local function ShowRumours()
    for i, row in ipairs(page.rows) do
        local rumour = rumours[i]
        if rumour then
            row.id = rumour.id
            local text = rumour.text or ""
            if #text > 48 then
                text = text:sub(1, 46) .. "..."
            end
            row.text:SetText((rumour.resolved and "|cff808080" or "") .. text)
            row:Show()
        else
            row.id = nil
            row:Hide()
        end
    end
    ShowRumour()
end

local function LoadRumours()
    LA.Bridge.Request({ op = "rumours" }, function(reply)
        if reply.ok then
            rumours = reply.rumours or {}
            page.mayChange = reply.may_change
        else
            rumours = {}
            page.mayChange = false
        end
        ShowRumours()
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

local function Build(self)
    page = self
    page.name = page:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
    page.name:SetPoint("TOPLEFT", 0, -4)
    page.status = page:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    page.status:SetPoint("TOPRIGHT", 0, -8)

    page.none = page:CreateFontString(nil, "OVERLAY", "GameFontDisable")
    page.none:SetPoint("CENTER")
    page.none:SetText("You are not in a guild.")

    page.body = CreateFrame("Frame", nil, page)
    page.body:SetAllPoints()
    local body = page.body

    LA.Manager.Heading(body, "Who we are", 0, -30)
    page.who = body:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
    page.who:SetPoint("TOPLEFT", 90, -32)
    page.boxes = {}
    local y = -48
    for _, field in ipairs(FIELDS) do
        local label = body:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
        label:SetPoint("TOPLEFT", 2, y)
        label:SetText(field[2])
        local box = Box(body, 40)
        box.frame:SetPoint("TOPLEFT", 0, y - 12)
        box.frame:SetWidth(340)
        page.boxes[field[1]] = box
        y = y - 58
    end
    local fadeLabel = body:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    fadeLabel:SetPoint("TOPLEFT", 2, y - 6)
    fadeLabel:SetText("Rumours fade after")
    page.fade = CreateFrame("Frame", "LivingAzerothGuildFade", body, "UIDropDownMenuTemplate")
    page.fade:SetPoint("LEFT", fadeLabel, "RIGHT", -8, -2)
    UIDropDownMenu_SetWidth(page.fade, 90)
    UIDropDownMenu_Initialize(page.fade, function()
        for _, fade in ipairs(FADES) do
            local info = UIDropDownMenu_CreateInfo()
            info.text = fade[2]
            info.checked = guild and guild.identity and guild.identity.report_hours == fade[1]
            info.disabled = not (guild and guild.may_edit)
            info.func = function()
                Save({ report_hours = fade[1] })
            end
            UIDropDownMenu_AddButton(info)
        end
    end)
    page.save = LA.Manager.Button(body, "Save", 90)
    page.save:SetPoint("TOPRIGHT", body, "TOPLEFT", 340, y - 6)
    page.save:SetScript("OnClick", function()
        local changes = {}
        for _, field in ipairs(FIELDS) do
            changes[field[1]] = page.boxes[field[1]]:GetText()
            page.boxes[field[1]]:ClearFocus()
        end
        Save(changes)
    end)

    page.draftArea = CreateFrame("Frame", nil, body)
    page.draftArea:SetPoint("TOPLEFT", 0, y - 34)
    page.draftArea:SetSize(340, 40)
    local seedLabel = page.draftArea:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    seedLabel:SetPoint("TOPLEFT", 2, 0)
    seedLabel:SetText("Start from a few words")
    page.seed = CreateFrame("EditBox", nil, page.draftArea, "InputBoxTemplate")
    page.seed:SetSize(200, 20)
    page.seed:SetPoint("TOPLEFT", 6, -14)
    page.seed:SetAutoFocus(false)
    page.seed:SetMaxLetters(120)
    page.draft = LA.Manager.Button(page.draftArea, "Write a draft", 120)
    page.draft:SetPoint("LEFT", page.seed, "RIGHT", 8, 0)
    page.draft:SetScript("OnEnter", function(self)
        GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
        GameTooltip:AddLine("Write a draft", 1, 0.82, 0)
        GameTooltip:AddLine("One model call from the dialogue budget. The draft fills the boxes; nothing is " ..
            "saved until you press Save.", 1, 1, 1, true)
        GameTooltip:Show()
    end)
    page.draft:SetScript("OnLeave", function()
        GameTooltip:Hide()
    end)
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
                end
                Status("A draft is in the boxes. Change it, then Save.", 0.5, 0.9, 0.5)
            else
                Status("No draft: " .. (reply.reason or reply.error or "?"), 1, 0.3, 0.2)
            end
        end, 60)
    end)

    LA.Manager.Heading(body, "Rumours", 360, -30)
    local list = LA.Manager.Inset(body)
    list:SetPoint("TOPLEFT", 356, -48)
    list:SetPoint("TOPRIGHT", 0, -48)
    list:SetHeight(150)
    page.rows = {}
    for i = 1, 8 do
        local row = CreateFrame("Button", nil, list)
        row:SetSize(300, 17)
        row:SetPoint("TOPLEFT", 6, -6 - (i - 1) * 17)
        row.text = row:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
        row.text:SetAllPoints()
        row.text:SetJustifyH("LEFT")
        row:SetHighlightTexture("Interface\\QuestFrame\\UI-QuestTitleHighlight", "ADD")
        row:SetScript("OnClick", function(self)
            chosen = self.id
            ShowRumour()
        end)
        page.rows[i] = row
    end
    local detail = LA.Manager.Inset(body)
    detail:SetPoint("TOPLEFT", list, "BOTTOMLEFT", 0, -6)
    detail:SetPoint("BOTTOMRIGHT", 0, 0)
    page.rumour = detail:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    page.rumour:SetPoint("TOPLEFT", 10, -10)
    page.rumour:SetPoint("BOTTOMRIGHT", -10, 84)
    page.rumour:SetJustifyH("LEFT")
    page.rumour:SetJustifyV("TOP")
    page.correction = Box(detail, 40)
    page.correction.frame:SetPoint("BOTTOMLEFT", 8, 36)
    page.correction.frame:SetPoint("BOTTOMRIGHT", -8, 36)
    page.actions = {}
    local function Action(name, text, x)
        local button = LA.Manager.Button(detail, text, 96)
        button:SetPoint("BOTTOMLEFT", x, 8)
        page.actions[name] = button
        return button
    end
    Action("correct", "Correct", 8):SetScript("OnClick", function()
        ChangeRumour("correct", page.correction:GetText())
    end)
    Action("resolve", "Resolve", 108):SetScript("OnClick", function()
        ChangeRumour("resolve")
    end)
    Action("forget", "Forget", 208):SetScript("OnClick", function()
        ChangeRumour("forget")
    end)
end

local function Show()
    LA.Bridge.Request({ op = "guild" }, function(reply)
        if reply.ok then
            guild = reply
            page.none:Hide()
            page.body:Show()
            ShowIdentity()
            LoadRumours()
        else
            guild = nil
            page.name:SetText("")
            page.body:Hide()
            page.none:Show()
            page.none:SetText(reply.reason == "not in a guild" and "You are not in a guild." or
                ("Not available: " .. (reply.reason or reply.error or "?")))
        end
    end)
end

LA.On("loaded", function()
    LA.Manager.AddPage("Guild", Build, Show)
end)
