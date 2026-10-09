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
local guild -- the server's last answer; nil when the player is in no guild
local editing = false
local rumours = {}
local showing = "identity" -- "identity" or a rumour id

local function Status(text, r, g, b)
    page.status:SetText(text or "")
    page.status:SetTextColor(r or 1, g or 0.82, b or 0)
end

local function FadeText(hours)
    for _, fade in ipairs(FADES) do
        if fade[1] == hours then
            return fade[2]
        end
    end
    return (hours or "?") .. " hours"
end

local function Age(ms)
    local hours = math.floor((ms or 0) / 3600000)
    if hours < 1 then
        return "less than an hour"
    elseif hours < 48 then
        return hours .. (hours == 1 and " hour" or " hours")
    end
    return math.floor(hours / 24) .. " days"
end

-- A text box of a fixed size on the parchment: a multi-line edit box that scrolls inside its frame.
local function Box(parent, height, letters)
    local frame = CreateFrame("Frame", nil, parent)
    frame:SetSize(276, height)
    frame:SetBackdrop({ bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
        edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border", edgeSize = 12,
        insets = { left = 3, right = 3, top = 3, bottom = 3 } })
    frame:SetBackdropColor(0.08, 0.06, 0.04, 0.85)
    frame:SetBackdropBorderColor(0.45, 0.35, 0.2)
    local scroll = CreateFrame("ScrollFrame", nil, frame)
    scroll:SetPoint("TOPLEFT", 7, -6)
    scroll:SetPoint("BOTTOMRIGHT", -7, 6)
    local box = CreateFrame("EditBox", nil, scroll)
    box:SetMultiLine(true)
    box:SetMaxLetters(letters or 400)
    box:SetAutoFocus(false)
    box:SetFontObject("GameFontHighlightSmall")
    box:SetWidth(262)
    scroll:SetScrollChild(box)
    box:SetScript("OnCursorChanged", function(_, _, y, _, lineHeight)
        local top, visible = scroll:GetVerticalScroll(), scroll:GetHeight()
        if -y < top then
            scroll:SetVerticalScroll(-y)
        elseif -y + lineHeight > top + visible then
            scroll:SetVerticalScroll(-y + lineHeight - visible)
        end
    end)
    box:SetScript("OnEscapePressed", box.ClearFocus)
    frame:EnableMouse(true)
    frame:SetScript("OnMouseDown", function() box:SetFocus() end)
    box.frame = frame
    return box
end

local function ChosenRumour()
    for _, rumour in ipairs(rumours) do
        if rumour.id == showing then
            return rumour
        end
    end
end

local function Layout()
    local d, stack = page.parts, page.stack
    stack:Reset()
    for _, region in pairs(d) do
        region:Hide()
    end
    for _, box in pairs(page.boxes) do
        box.frame:Hide()
    end
    page.correction.frame:Hide()
    for _, button in pairs(page.buttons) do
        button:Hide()
    end
    page.guildWindow:Hide()

    if not guild then
        d.none:SetText("You are not in a guild.")
        stack:Add(d.none)
        d.noneText:SetText("When you are, this is where your guild says who it is - what it is for, what its " ..
            "members value, how they speak - and where the rumours its members share are kept. Members, bots " ..
            "among them, carry it into conversation.")
        stack:Add(d.noneText, 6)
        page:SetDetailHeight(stack:Height())
        return
    end
    page.guildWindow:Show()

    if showing == "identity" then
        local identity = guild.identity or {}
        for _, field in ipairs(FIELDS) do
            local title = d["title:" .. field[1]]
            stack:Add(title, title == d["title:purpose"] and 0 or 12)
            if editing then
                local box = page.boxes[field[1]]
                if not box.dirty then
                    box:SetText(identity[field[1]] or "")
                end
                stack:Add(box.frame, 4)
            else
                local text = d["text:" .. field[1]]
                local value = identity[field[1]] or ""
                text:SetText(value ~= "" and value or "Not written yet.")
                text:SetTextColor(value ~= "" and 0 or 0.35, value ~= "" and 0 or 0.3, value ~= "" and 0 or 0.25)
                stack:Add(text, 4)
            end
        end
        stack:Add(d.fadeTitle, 12)
        d.fade:SetText("Rumours fade after " .. FadeText(identity.report_hours) .. ".")
        stack:Add(d.fade, 4)
        if editing then
            stack:Add(page.fadeChoice, 2, -16, 32)
            stack:Add(d.seedTitle, 12)
            stack:Add(page.seed, 6, 6, 22)
            page.draft:Show()
            stack:Add(d.seedHint, 6)
        else
            stack:Add(d.carry, 14)
        end
        if guild.may_edit then
            page.buttons.edit:SetText(editing and SAVE or "Edit")
            page.buttons.edit:Show()
            LA.SetShown(page.buttons.cancel, editing)
        end
    else
        local rumour = ChosenRumour()
        if not rumour then
            showing = "identity"
            return Layout()
        end
        d.rumourTitle:SetText(rumour.resolved and "A resolved rumour" or "A rumour")
        stack:Add(d.rumourTitle)
        d.rumour:SetText(rumour.text or "")
        stack:Add(d.rumour, 6)
        local by = type(rumour.by) == "table" and table.concat(rumour.by, ", ") or rumour.by or ""
        local facts = { "Witnessed by: " .. (by ~= "" and by or "?") }
        if rumour.zone and rumour.zone ~= "" then
            facts[#facts + 1] = "Where: " .. rumour.zone
        end
        facts[#facts + 1] = "Seen " .. Age(rumour.age_ms) .. " ago."
        facts[#facts + 1] = rumour.resolved and "Resolved." or ("Fades in " .. Age(rumour.fades_in_ms) .. ".")
        if rumour.corrected then
            facts[#facts + 1] = "Corrected by an officer."
        end
        d.facts:SetText(table.concat(facts, "\n"))
        stack:Add(d.facts, 10)
        if page.mayChange then
            stack:Add(d.correctTitle, 14)
            if not page.correction:HasFocus() and page.correction.forId ~= rumour.id then
                page.correction.forId = rumour.id
                page.correction:SetText(rumour.text or "")
            end
            stack:Add(page.correction.frame, 4)
            page.buttons.correct:Show()
            page.buttons.resolve:Show()
            page.buttons.forget:Show()
        end
    end
    page:SetDetailHeight(stack:Height())
end

local function ShowList()
    if not guild then
        page.list:SetItems({}, "You are not in a guild.")
        return
    end
    local items = {
        { header = true, key = "guild", text = guild.name or "?" },
        { key = "identity", under = "guild", text = "Who we are", icon = "Interface\\Icons\\INV_Misc_Book_09" },
        { header = true, key = "rumours", text = "Rumours", tag = #rumours > 0 and tostring(#rumours) or "" },
    }
    for _, rumour in ipairs(rumours) do
        items[#items + 1] = { key = rumour.id, under = "rumours", text = (rumour.resolved and "|cff888888" or "") ..
            (rumour.text or ""), tag = rumour.resolved and "Resolved" or rumour.corrected and "Corrected" or "" }
    end
    if #rumours == 0 then
        items[#items + 1] = { key = "none", under = "rumours", text = "|cff888888None yet|r",
            tooltip = { "Rumours",
                "Only what a member saw for themselves becomes a rumour; private chat never does." } }
    end
    page.list:SetItems(items)
    page.list:Choose(showing)
end

local function Save(changes, done)
    Status("Saving...")
    LA.Bridge.Request({ op = "guild", change = changes }, function(reply)
        if reply.ok then
            guild = reply
            Status("Saved. Members carry it into their next conversation.")
            if done then
                done()
            end
        else
            Status("Not saved: " .. (reply.reason or reply.error or "?"), 1, 0.3, 0.2)
        end
        Layout()
    end)
end

local function LoadRumours()
    LA.Bridge.Request({ op = "rumours" }, function(reply)
        rumours = reply.ok and reply.rumours or {}
        page.mayChange = reply.ok and reply.may_change
        ShowList()
        Layout()
    end)
end

local function ChangeRumour(action, text)
    local rumour = ChosenRumour()
    if not rumour then
        return
    end
    Status("Saving...")
    LA.Bridge.Request({ op = "rumours", change = { note = rumour.id, version = rumour.version, action = action,
        text = text } }, function(reply)
        if reply.ok then
            Status(action == "forget" and "Forgotten." or "Saved.")
            if action == "forget" then
                showing = "identity"
            end
            page.correction.forId = nil
        else
            Status("Not saved: " .. (reply.reason or reply.error or "?"), 1, 0.3, 0.2)
        end
        LoadRumours()
    end)
end

local function Build(self)
    page = self
    local UI = LA.UI

    page.name = page.top:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    page.name:SetPoint("LEFT", 4, 0)
    page.status = page.top:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    page.status:SetPoint("RIGHT", -6, 0)

    page.list = UI.List(page.left, function(item)
        if item.key == "none" then
            return
        end
        showing = item.key
        editing = false
        LA.Manager.ScrollToTop()
        ShowList()
        Layout()
    end)

    local parent = page.detail
    local d = {}
    d.none = UI.Paper(parent, "title", 280)
    d.noneText = UI.Paper(parent, "body", 280)
    for _, field in ipairs(FIELDS) do
        d["title:" .. field[1]] = UI.Paper(parent, "title", 280)
        d["title:" .. field[1]]:SetText(field[2])
        d["text:" .. field[1]] = UI.Paper(parent, "body", 280)
    end
    d.fadeTitle = UI.Paper(parent, "title", 280)
    d.fadeTitle:SetText("Rumours")
    d.fade = UI.Paper(parent, "body", 280)
    d.carry = UI.Paper(parent, "small", 280)
    d.carry:SetText("Members carry this into conversation, the way a guard carries the Watch's description.")
    d.seedTitle = UI.Paper(parent, "title", 280)
    d.seedTitle:SetText("Start from a few words")
    d.seedHint = UI.Paper(parent, "small", 280)
    d.seedHint:SetText("One model call from the dialogue budget. The draft fills the boxes; nothing is saved " ..
        "until you press Save.")
    d.rumourTitle = UI.Paper(parent, "title", 280)
    d.rumour = UI.Paper(parent, "body", 280)
    d.facts = UI.Paper(parent, "small", 280)
    d.correctTitle = UI.Paper(parent, "title", 280)
    d.correctTitle:SetText("Correct it to")
    page.parts = d
    page.stack = UI.Stack(parent)

    page.boxes = {}
    for _, field in ipairs(FIELDS) do
        local box = Box(parent, 56)
        box:SetScript("OnTextChanged", function(b, typed)
            if typed then
                b.dirty = true
            end
        end)
        page.boxes[field[1]] = box
    end
    page.correction = Box(parent, 70, 4000)

    page.fadeChoice = CreateFrame("Frame", "LivingAzerothGuildFade", parent, "UIDropDownMenuTemplate")
    UIDropDownMenu_SetWidth(page.fadeChoice, 110)
    UIDropDownMenu_Initialize(page.fadeChoice, function()
        for _, fade in ipairs(FADES) do
            local info = UIDropDownMenu_CreateInfo()
            info.text = fade[2]
            info.checked = guild and guild.identity and guild.identity.report_hours == fade[1]
            info.func = function() Save({ report_hours = fade[1] }) end
            UIDropDownMenu_AddButton(info)
        end
    end)
    page.fadeChoice:SetScript("OnShow", function(self)
        UIDropDownMenu_SetText(self, guild and FadeText(guild.identity and guild.identity.report_hours) or "")
    end)

    page.seed = CreateFrame("EditBox", nil, parent, "InputBoxTemplate")
    page.seed:SetSize(160, 20)
    page.seed:SetAutoFocus(false)
    page.seed:SetMaxLetters(120)
    page.draft = UI.Button(parent, "Write a draft", 100)
    page.draft:SetPoint("LEFT", page.seed, "RIGHT", 8, 0)
    page.draft:SetScript("OnClick", function()
        local seed = strtrim(page.seed:GetText())
        if seed == "" then
            Status("Write a few words first.", 1, 0.3, 0.2)
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
                Status("A draft is in the boxes. Change it, then Save.")
            else
                Status("No draft: " .. (reply.reason or reply.error or "?"), 1, 0.3, 0.2)
            end
        end, 60)
    end)
    -- The seed box and its button come and go with the edit view.
    page.seed:SetScript("OnShow", function() page.draft:Show() end)
    page.seed:SetScript("OnHide", function() page.draft:Hide() end)
    d.seedBox = page.seed
    d.fadeChoice = page.fadeChoice

    page.buttons = {}
    local function Button(key, text, anchor)
        local button = UI.Button(page.controls, text, 100)
        button:SetPoint(unpack(anchor))
        page.buttons[key] = button
        return button
    end
    Button("edit", "Edit", { "LEFT", 0, 1 }):SetScript("OnClick", function()
        if not editing then
            editing = true
            for _, box in pairs(page.boxes) do
                box.dirty = false
            end
            Layout()
            return
        end
        local changes = {}
        for _, field in ipairs(FIELDS) do
            changes[field[1]] = page.boxes[field[1]]:GetText()
            page.boxes[field[1]]:ClearFocus()
        end
        Save(changes, function() editing = false end)
    end)
    Button("cancel", CANCEL, { "LEFT", 100, 1 }):SetScript("OnClick", function()
        editing = false
        Status("")
        Layout()
    end)
    Button("correct", "Correct", { "LEFT", 0, 1 }):SetScript("OnClick", function()
        ChangeRumour("correct", page.correction:GetText())
    end)
    Button("resolve", "Resolve", { "LEFT", 100, 1 }):SetScript("OnClick", function() ChangeRumour("resolve") end)
    Button("forget", "Forget", { "RIGHT", -3, 1 }):SetScript("OnClick", function() ChangeRumour("forget") end)

    page.guildWindow = UI.Button(page.actions, GUILD, 100)
    page.guildWindow:SetPoint("RIGHT", -84, 1)
    page.guildWindow:SetScript("OnClick", function() ToggleFriendsFrame(3) end)
end

local function Show()
    LA.Bridge.Request({ op = "guild" }, function(reply)
        if reply.ok then
            guild = reply
            local members, bots = reply.members or 0, reply.bots_online or 0
            page.name:SetText("<" .. (reply.name or "?") .. ">  " .. members .. (members == 1 and " member" or
                " members") .. (bots > 0 and (", " .. bots .. (bots == 1 and " bot" or " bots") .. " online") or ""))
            LoadRumours()
        else
            guild = nil
            rumours = {}
            editing = false
            showing = "identity"
            page.name:SetText("")
            ShowList()
        end
        Layout()
    end)
end

LA.On("loaded", function()
    LA.Manager.AddPage("Guild", Build, Show)
end)
