-- SPDX-License-Identifier: GPL-2.0-or-later
-- The Guild tab: the guild's own account of itself, which its members carry into conversation the way a town
-- guard carries the Watch's (officers edit it; everyone reads it), and the rumours its members share (officers
-- correct, resolve or forget them). It sits beside the game's own guild window, not in place of it.
local _, LA = ...

local Guild = {}
LA.Guild = Guild

local UI, Style = LA.UI, LA.Style

local FIELDS = {
    { "purpose", "What the guild is for" },
    { "values", "What members value" },
    { "traditions", "Traditions" },
    { "ambitions", "Current ambitions" },
    { "voice", "How members speak" },
}
local FADES = { { id = 6, label = "6 hours" }, { id = 24, label = "1 day" }, { id = 72, label = "3 days" },
    { id = 168, label = "1 week" }, { id = 720, label = "30 days" } }
local SIDE = 276 -- the right-hand column's width
local DETAIL = 270 -- the rumour panel's width
local TOP = 62 -- where the parts start, under the header and the tabs

local page
local guild -- the server's last answer; nil when the player is in no guild
local editing = false
local rumours = {}
local chosenId -- the rumour on the right
local correcting = false

local function Status(text, colour)
    colour = colour or Style.T().muted
    page.status:SetText(text or "")
    page.status:SetTextColor(colour[1], colour[2], colour[3])
end

local function FadeText(hours)
    for _, fade in ipairs(FADES) do
        if fade.id == hours then
            return fade.label
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

local function ChosenRumour()
    for _, rumour in ipairs(rumours) do
        if rumour.id == chosenId then
            return rumour
        end
    end
end

local function Witnesses(rumour)
    local by = type(rumour.by) == "table" and table.concat(rumour.by, ", ") or rumour.by or ""
    return by ~= "" and by or "?"
end

-- Who we are

local function ShowIdentity()
    local d = page.identity
    local identity = guild.identity or {}
    local stack = d.stack
    stack:Reset()
    for _, field in ipairs(FIELDS) do
        local key = field[1]
        stack:Add(d.labels[key], key == "purpose" and 0 or 12)
        local value = identity[key] or ""
        if editing then
            d.texts[key]:Hide()
            if not d.boxes[key].dirty then
                d.boxes[key]:SetText(value)
            end
            stack:Add(d.boxes[key], 4)
        else
            d.boxes[key]:Hide()
            d.texts[key]:SetText(value ~= "" and value or "Not written yet.")
            local colour = value ~= "" and Style.T().text or Style.T().muted
            d.texts[key]:SetTextColor(colour[1], colour[2], colour[3])
            stack:Add(d.texts[key], 3)
        end
    end
    d.scroll:SetContentHeight(stack:Height() + 4)

    local officer = guild.may_edit
    LA.SetShown(d.edit, officer)
    LA.SetShown(d.cancel, officer and editing)
    d.edit:SetText(editing and SAVE or "Edit")
    LA.SetShown(page.seedPanel, officer)
    page.fade:Select(identity.report_hours)
    page.fade:SetUsable(officer)
    page.fadeNote:SetText(officer and "How long members keep passing a rumour on." or
        "Your guild's officers choose this.")
    page.whoCan:SetText("The guild master and officers can change it; every member can read it.")
end

-- Rumours

local function ShowRumour()
    local d = page.rumour
    local rumour = ChosenRumour()
    for _, region in ipairs(d.parts) do
        region:Hide()
    end
    for _, button in ipairs({ d.correct, d.resolve, d.forget, d.cancel }) do
        button:Hide()
    end
    local stack = d.stack
    stack:Reset()
    if not rumour then
        d.title:SetText("Rumours")
        stack:Add(d.title, 3, 28)
        d.text:SetText(#rumours > 0 and "Choose a rumour to see who saw it and when it fades." or
            "No rumours yet. Only what a member saw for themselves becomes a rumour; private chat never does.")
        stack:Add(d.text, 12)
        return
    end
    d.title:SetText(rumour.resolved and "A resolved rumour" or rumour.corrected and "A corrected rumour" or
        "A rumour")
    stack:Add(d.title, 3, 28)
    if correcting then
        d.correctLabel:SetText("What actually happened")
        stack:Add(d.correctLabel, 12)
        if d.box.forId ~= rumour.id then
            d.box.forId = rumour.id
            d.box:SetText(rumour.text or "")
        end
        stack:Add(d.box, 4)
    else
        d.text:SetText(rumour.text or "")
        stack:Add(d.text, 12)
    end
    local facts = { { "Witnessed by", Witnesses(rumour) } }
    if rumour.zone and rumour.zone ~= "" then
        facts[#facts + 1] = { "Where", rumour.zone }
    end
    facts[#facts + 1] = { "Seen", Age(rumour.age_ms) .. " ago" }
    facts[#facts + 1] = { "Fades", rumour.resolved and "No longer shared" or "In " .. Age(rumour.fades_in_ms) }
    local y = stack:Height() + 12
    for i, fact in ipairs(facts) do
        d.keys[i]:SetText(fact[1])
        d.keys[i]:ClearAllPoints()
        d.keys[i]:SetPoint("TOPLEFT", 0, -y)
        d.keys[i]:Show()
        d.values[i]:SetText(fact[2])
        d.values[i]:ClearAllPoints()
        d.values[i]:SetPoint("TOPLEFT", 76, -y)
        d.values[i]:Show()
        y = y + LA.Draw.Ceil(math.max(15, d.values[i]:GetStringHeight() + 3))
    end
    local officer = page.mayChange
    LA.SetShown(d.correct, officer)
    LA.SetShown(d.resolve, officer and not correcting)
    LA.SetShown(d.forget, officer and not correcting)
    LA.SetShown(d.cancel, officer and correcting)
    d.correct:SetText(correcting and "Save correction" or "Correct it")
    d.correct:FitText()
    d.resolve:SetText(rumour.resolved and "Mark current" or "Resolve")
    d.resolve:FitText()
end

local function ShowRumours()
    local items = {}
    for _, rumour in ipairs(rumours) do
        local state, colour = "Current", Style.green
        if rumour.resolved then
            state, colour = "Resolved", Style.T().muted
        elseif rumour.corrected then
            state, colour = "Corrected", Style.gold
        end
        items[#items + 1] = { key = rumour.id, rumour = rumour, text = rumour.text or "", tag = state,
            tagColour = colour, dim = rumour.resolved,
            sub = "Seen " .. Age(rumour.age_ms) .. " ago · by " .. Witnesses(rumour) }
    end
    page.rumours:SetItems(items, "No rumours yet. Only what a member saw for themselves becomes a rumour; " ..
        "private chat never does.")
    page.rumours:Choose(chosenId)
    ShowRumour()
end

-- The whole page

local function ShowPart(index)
    LA.SetShown(page.identityPart, guild and index == 1)
    LA.SetShown(page.rumourPart, guild and index == 2)
end

local function Layout()
    LA.SetShown(page.header, guild ~= nil)
    LA.SetShown(page.tabs, guild ~= nil)
    LA.SetShown(page.none, guild == nil)
    if not guild then
        ShowPart(0)
        return
    end
    local members, bots = guild.members or 0, guild.bots_online or 0
    page.name:SetText("<" .. (guild.name or "?") .. ">")
    page.counts:SetText(members .. (members == 1 and " member" or " members") ..
        (bots > 0 and (" · " .. bots .. (bots == 1 and " bot" or " bots") .. " online") or "") ..
        (guild.may_edit and " · you can change who the guild is" or ""))
    ShowPart(page.tabs.selected or 1)
    ShowIdentity()
    ShowRumours()
end

local function Save(changes, done)
    Status("Saving...")
    LA.Bridge.Request({ op = "guild", change = changes }, function(reply)
        if reply.ok then
            guild = reply
            Status("Saved. Members carry it into their next conversation.", Style.green)
            if done then
                done()
            end
        else
            Status("Not saved: " .. (reply.reason or reply.error or "?"), Style.red)
        end
        Layout()
    end)
end

local function LoadRumours()
    LA.Bridge.Request({ op = "rumours" }, function(reply)
        rumours = reply.ok and reply.rumours or {}
        page.mayChange = reply.ok and reply.may_change
        if chosenId and not ChosenRumour() then
            chosenId, correcting = nil, false
        end
        ShowRumours()
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
            Status(action == "forget" and "Forgotten." or "Saved.", Style.green)
            correcting = false
            page.rumour.box.forId = nil
            if action == "forget" then
                chosenId = nil
            end
        else
            Status("Not saved: " .. (reply.reason or reply.error or "?"), Style.red)
        end
        LoadRumours()
    end)
end

-- Building

local function BuildIdentity(parent, width, height)
    local d = { labels = {}, texts = {}, boxes = {} }
    local left = UI.Inset(parent)
    left:SetPoint("TOPLEFT")
    local leftWidth = width - SIDE - 10
    left:SetSize(leftWidth, height)
    local textWidth = leftWidth - 30
    d.edit = UI.Button(left, "Edit", 60)
    d.edit:SetPoint("TOPRIGHT", -10, -6)
    d.edit:SetScript("OnClick", function()
        if not editing then
            editing = true
            for _, box in pairs(d.boxes) do
                box.dirty = false
            end
            ShowIdentity()
            return
        end
        local changes = {}
        for _, field in ipairs(FIELDS) do
            changes[field[1]] = d.boxes[field[1]]:GetText()
            d.boxes[field[1]].box:ClearFocus()
        end
        Save(changes, function() editing = false end)
    end)
    d.cancel = UI.Button(left, CANCEL, 60, "plain")
    d.cancel:SetPoint("RIGHT", d.edit, "LEFT", -4, 0)
    d.cancel:SetScript("OnClick", function()
        editing = false
        Status("")
        ShowIdentity()
    end)
    local title = UI.Heading(left, "Who we are")
    title:SetPoint("TOPLEFT", 10, -8)
    title:SetWidth(leftWidth - 20)
    d.scroll = UI.Scroll(left)
    d.scroll:SetPoint("TOPLEFT", 10, -34)
    d.scroll:SetPoint("BOTTOMRIGHT", -4, 10)
    for _, field in ipairs(FIELDS) do
        local key = field[1]
        d.labels[key] = UI.Text(d.scroll.child, "small", textWidth)
        d.labels[key]:SetText(field[2])
        d.texts[key] = UI.Text(d.scroll.child, "label", textWidth)
        local box = UI.EditBox(d.scroll.child, textWidth, key == "voice" and 34 or 46, true)
        box.box:SetMaxLetters(400)
        box.box:SetScript("OnTextChanged", function(_, typed)
            if typed then
                box.dirty = true
            end
        end)
        d.boxes[key] = box
    end
    d.stack = UI.Stack(d.scroll.child)
    page.identity = d

    -- The right-hand column: writing a draft (officers), how long rumours last, who can change it.
    local right = CreateFrame("Frame", nil, parent)
    right:SetPoint("TOPRIGHT")
    right:SetSize(SIDE, height)
    local inner = SIDE - 20

    page.seedPanel = UI.Inset(right)
    page.seedPanel:SetPoint("TOPLEFT")
    page.seedPanel:SetWidth(SIDE)
    local seedTitle = UI.Heading(page.seedPanel, "Start from a few words")
    seedTitle:SetPoint("TOPLEFT", 10, -8)
    seedTitle:SetWidth(inner)
    page.seed = UI.EditBox(page.seedPanel, inner, 34, true)
    page.seed.box:SetMaxLetters(120)
    page.seed:SetPoint("TOPLEFT", 10, -32)
    page.draft = UI.Button(page.seedPanel, "Write a draft")
    page.draft:SetPoint("TOPLEFT", page.seed, "BOTTOMLEFT", 0, -6)
    local seedHint = UI.Text(page.seedPanel, "small", inner)
    seedHint:SetPoint("TOPLEFT", page.draft, "BOTTOMLEFT", 0, -6)
    seedHint:SetText("One model call from the dialogue budget. The draft fills the fields; nothing is saved " ..
        "until you press Save.")
    page.seedPanel:SetHeight(LA.Draw.Ceil(32 + 34 + 6 + UI.BUTTON_HEIGHT + 6 + seedHint:GetStringHeight() + 10))
    page.draft:SetScript("OnClick", function()
        local seed = strtrim(page.seed:GetText())
        if seed == "" then
            Status("Write a few words first.", Style.red)
            return
        end
        Status("Writing a draft...", Style.gold)
        page.draft:Disable()
        LA.Bridge.Request({ op = "guild", draft = seed }, function(reply)
            page.draft:Enable()
            if reply.ok and reply.draft then
                editing = true
                for _, field in ipairs(FIELDS) do
                    d.boxes[field[1]]:SetText(reply.draft[field[1]] or "")
                    d.boxes[field[1]].dirty = true
                end
                Status("A draft is in the fields. Change it, then Save.", Style.green)
                ShowIdentity()
            else
                Status("No draft: " .. (reply.reason or reply.error or "?"), Style.red)
            end
        end, 60)
    end)

    local fadePanel = UI.Inset(right)
    fadePanel:SetPoint("TOPLEFT", page.seedPanel, "BOTTOMLEFT", 0, -8)
    fadePanel:SetSize(SIDE, 78)
    local fadeTitle = UI.Heading(fadePanel, "Rumours fade after")
    fadeTitle:SetPoint("TOPLEFT", 10, -8)
    fadeTitle:SetWidth(inner)
    page.fade = UI.Segment(fadePanel, FADES, inner, function(hours)
        page.fade:Select(guild.identity and guild.identity.report_hours)
        Save({ report_hours = hours })
    end)
    page.fade:SetPoint("TOPLEFT", 10, -32)
    page.fadeNote = UI.Text(fadePanel, "small", inner)
    page.fadeNote:SetPoint("TOPLEFT", page.fade, "BOTTOMLEFT", 0, -6)

    local whoPanel = UI.Inset(right)
    whoPanel:SetPoint("TOPLEFT", fadePanel, "BOTTOMLEFT", 0, -8)
    whoPanel:SetPoint("BOTTOMRIGHT")
    local whoTitle = UI.Heading(whoPanel, "Who can change it")
    whoTitle:SetPoint("TOPLEFT", 10, -8)
    whoTitle:SetWidth(inner)
    local leader = UI.Icon(whoPanel, 12, "plain")
    leader:SetPoint("TOPLEFT", 10, -33)
    leader:Set("leader", Style.gold)
    page.whoCan = UI.Text(whoPanel, "body", inner - 18)
    page.whoCan:SetPoint("TOPLEFT", 28, -32)
    local carry = UI.Text(whoPanel, "small", inner)
    carry:SetPoint("TOPLEFT", page.whoCan, "BOTTOMLEFT", -18, -6)
    carry:SetText("Members carry it into conversation, the way a guard carries the Watch's description.")
end

local function BuildRumours(parent, width, height)
    local listWidth = width - DETAIL - 10
    page.rumours = UI.List(parent, function(item)
        chosenId = item.key
        correcting = false
        page.rumours:Choose(chosenId)
        ShowRumour()
    end, 30)
    page.rumours:SetPoint("TOPLEFT")
    page.rumours:SetSize(listWidth, height)

    local inset = UI.Inset(parent)
    inset:SetPoint("TOPRIGHT")
    inset:SetSize(DETAIL, height)
    local tile = UI.Icon(inset, 20, "tile")
    tile:SetPoint("TOPLEFT", 10, -9)
    tile:Set("rumour", "teal")
    local area = CreateFrame("Frame", nil, inset)
    area:SetPoint("TOPLEFT", 10, -10)
    area:SetPoint("BOTTOMRIGHT", -10, UI.BUTTON_HEIGHT + 18)
    local textWidth = DETAIL - 20
    local d = { parts = {}, keys = {}, values = {} }
    local function Part(region)
        d.parts[#d.parts + 1] = region
        return region
    end
    d.title = Part(UI.Text(area, "heading", textWidth - 28))
    d.text = Part(UI.Text(area, "label", textWidth))
    d.correctLabel = Part(UI.Text(area, "small", textWidth))
    d.box = Part(UI.EditBox(area, textWidth, 80, true))
    d.box.box:SetMaxLetters(4000)
    for i = 1, 4 do
        d.keys[i] = Part(UI.Text(area, "small", 72))
        d.values[i] = Part(UI.Text(area, "value", textWidth - 76))
    end
    d.stack = UI.Stack(area)
    d.correct = UI.Button(inset, "Correct it")
    d.correct:SetPoint("BOTTOMLEFT", 10, 10)
    d.correct:SetScript("OnClick", function()
        if not correcting then
            correcting = true
            ShowRumour()
            d.box.box:SetFocus()
            return
        end
        ChangeRumour("correct", d.box:GetText())
    end)
    d.resolve = UI.Button(inset, "Resolve", nil, "plain")
    d.resolve:SetPoint("LEFT", d.correct, "RIGHT", 4, 0)
    d.resolve:SetScript("OnClick", function() ChangeRumour("resolve") end)
    d.forget = UI.Button(inset, "Forget it", nil, "plain")
    d.forget:SetPoint("LEFT", d.resolve, "RIGHT", 4, 0)
    d.forget:SetScript("OnClick", function() ChangeRumour("forget") end)
    d.cancel = UI.Button(inset, CANCEL, nil, "plain")
    d.cancel:SetPoint("LEFT", d.correct, "RIGHT", 4, 0)
    d.cancel:SetScript("OnClick", function()
        correcting = false
        d.box.forId = nil
        ShowRumour()
    end)
    page.rumour = d
end

local function Build(self)
    page = self
    local width, height = LA.Manager.BodySize()

    page.header = CreateFrame("Frame", nil, page.frame)
    page.header:SetPoint("TOPLEFT")
    page.header:SetPoint("TOPRIGHT")
    page.header:SetHeight(28)
    local crest = UI.Icon(page.header, 28, "tile")
    crest:SetPoint("LEFT")
    crest:Set("guild", "blue")
    page.name = UI.Text(page.header, "name")
    page.name:SetPoint("TOPLEFT", crest, "TOPRIGHT", 8, 0)
    page.name:SetTextColor(0.25, 1, 0.25)
    page.counts = UI.Text(page.header, "small")
    page.counts:SetPoint("TOPLEFT", page.name, "BOTTOMLEFT", 0, -2)
    local guildWindow = UI.Button(page.header, "Guild window", nil, "plain", "guild")
    guildWindow:SetPoint("RIGHT")
    guildWindow:SetScript("OnClick", function() ToggleFriendsFrame(3) end)
    page.status = UI.Text(page.header, "small", 220)
    page.status:SetPoint("RIGHT", guildWindow, "LEFT", -10, 0)
    page.status:SetJustifyH("RIGHT")

    page.tabs = UI.TopTabs(page.frame, { "Who we are", "Rumours" }, ShowPart)
    page.tabs:SetPoint("TOPLEFT", 0, -34)
    page.tabs:SetPoint("TOPRIGHT", 0, -34)

    page.identityPart = CreateFrame("Frame", nil, page.frame)
    page.identityPart:SetPoint("TOPLEFT", 0, -TOP)
    page.identityPart:SetSize(width, height - TOP)
    BuildIdentity(page.identityPart, width, height - TOP)
    page.rumourPart = CreateFrame("Frame", nil, page.frame)
    page.rumourPart:SetPoint("TOPLEFT", 0, -TOP)
    page.rumourPart:SetSize(width, height - TOP)
    BuildRumours(page.rumourPart, width, height - TOP)
    page.tabs:Select(1)

    page.none = UI.Inset(page.frame)
    page.none:SetAllPoints()
    local noneCrest = UI.Icon(page.none, 40, "tile")
    noneCrest:SetPoint("TOP", 0, -96)
    noneCrest:Set("guild", "blue")
    noneCrest:SetDim(true)
    local noneTitle = UI.Text(page.none, "name")
    noneTitle:SetPoint("TOP", noneCrest, "BOTTOM", 0, -12)
    noneTitle:SetText("You are not in a guild")
    local noneText = UI.Text(page.none, "body", 380)
    noneText:SetJustifyH("CENTER")
    noneText:SetPoint("TOP", noneTitle, "BOTTOM", 0, -10)
    noneText:SetText("When you are, this is where your guild says who it is: what it is for, what its members " ..
        "value, how they speak. The rumours its members share are kept here too. Members, bots among them, " ..
        "carry it into conversation.")
end

local function Show()
    Status("")
    LA.Bridge.Request({ op = "guild" }, function(reply)
        if reply.ok then
            guild = reply
            LoadRumours()
        else
            guild = nil
            rumours = {}
            editing, correcting, chosenId = false, false, nil
        end
        Layout()
    end)
end

LA.On("loaded", function()
    LA.Manager.AddPage("Guild", "guild", Build, Show)
end)
