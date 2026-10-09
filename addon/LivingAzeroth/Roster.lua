-- SPDX-License-Identifier: GPL-2.0-or-later
-- The Roster tab: the player's other characters (bring one in as a bot to join the party, or send it home) and
-- the characters they have come to know best (invite the ones online). The chosen character shows in the header;
-- for a bot in the player's group, its talents, gear, bags and money, and for anyone, what they remember of you.
local _, LA = ...

local Roster = {}
LA.Roster = Roster

local ROWS = 11
local ROW_HEIGHT = 26

local page
local roster = { characters = {}, companions = {} }
local section = 1 -- 1: your characters, 2: known characters
local chosen -- an entry

local function Status(text, r, g, b)
    page.status:SetText(text or "")
    page.status:SetTextColor(r or 0.8, g or 0.8, b or 0.8)
end

local function Money(copper)
    copper = copper or 0
    return GetCoinTextureString and GetCoinTextureString(copper) or
        string.format("%dg %ds %dc", math.floor(copper / 10000), math.floor(copper / 100) % 100, copper % 100)
end

local function Where(entry)
    if entry.in_party then
        return "In your party"
    elseif entry.in_world or entry.online then
        return entry.zone ~= "" and entry.zone or "In the world"
    end
    return entry.isCharacter and "At rest" or "Away"
end

local Load

local function Act(entry)
    if entry.isCharacter then
        local bring = not entry.in_world
        Status(bring and ("Bringing " .. entry.name .. "...") or ("Sending " .. entry.name .. " home..."))
        LA.Bridge.Request({ op = bring and "bring" or "dismiss", name = entry.name }, function(reply)
            if reply.ok then
                Status(bring and (entry.name .. " is on the way.") or (entry.name .. " has gone home."), 0.5, 0.9, 0.5)
            else
                Status(reply.reason or reply.error or "?", 1, 0.3, 0.2)
            end
            Load()
        end)
    else
        InviteUnit(entry.name)
        Status("Invited " .. entry.name .. ".", 0.5, 0.9, 0.5)
    end
end

local function ActionFor(entry)
    if entry.isCharacter then
        return entry.in_world and "Send home" or "Bring"
    end
    return (entry.online and not entry.in_party) and "Invite" or nil
end

local function ShowDetail()
    local entry = chosen
    page.list:Choose(entry and entry.guid)
    if not entry then
        page.card:Hide()
        page.cardHint:Show()
        page.details:SetText("")
        page.memory:SetText("")
        page.whisper:Disable()
        page.act:Hide()
        return
    end
    page.cardHint:Hide()
    page.card:Show()
    page.card.button:SetCharacter(entry.guid, entry.class)
    page.card.name:SetText(LA.UI.Coloured(entry.name, entry.class))
    local class = LA.UI.ClassToken(entry.class)
    local className = class and LOCALIZED_CLASS_NAMES_MALE[class] or ""
    page.card.line:SetText("Level " .. (entry.level or "?") .. " " .. className)
    page.card.where:SetText(Where(entry))
    if entry.in_world or entry.online then
        page.whisper:Enable()
    else
        page.whisper:Disable()
    end
    local action = ActionFor(entry)
    page.act:SetText(action or "")
    LA.SetShown(page.act, action ~= nil)

    page.details:SetText("")
    local bot = LA.Bots.Mine()[entry.guid]
    if bot and bot.commandable then
        page.details:SetText("Asking...")
        LA.Bridge.Request({ op = "inspect", bot = entry.guid }, function(reply)
            if chosen ~= entry then
                return
            end
            if not reply.ok then
                page.details:SetText("")
                return
            end
            page.details:SetText(table.concat({
                "|cffffd100Talents|r  " .. (reply.spec ~= "" and reply.spec or "none chosen"),
                "|cffffd100Gear|r  item level " .. (reply.item_level or 0),
                "|cffffd100Bags|r  " .. (reply.free_slots or 0) .. " free slots",
                "|cffffd100Money|r  " .. Money(reply.money),
                "|cffffd100Doing|r  " .. LA.Orders.Standing(reply.bot or bot).state,
            }, "\n"))
        end)
    elseif not entry.isCharacter then
        -- Familiarity grows by seeing them around (1) and doing things together (4), up to 100.
        local familiarity = entry.familiarity or 0
        local how = familiarity >= 60 and "Old friends" or familiarity >= 25 and "Well known to you" or
            familiarity >= 8 and "Seen about often" or "A familiar face"
        page.details:SetText("|cffffd100How well you know them|r  " .. how)
    end

    page.memory:SetText("Asking...")
    LA.Bridge.Request({ op = "memory", character = entry.guid }, function(reply)
        if chosen ~= entry then
            return
        end
        local notes = reply.ok and reply.remembers_you or {}
        if #notes == 0 then
            page.memory:SetText(entry.isCharacter and "Your own character: nothing to remember." or
                "Nothing yet. They remember what you do together and what you tell them.")
            return
        end
        local lines = {}
        for _, note in ipairs(notes) do
            lines[#lines + 1] = "- " .. note.text
        end
        page.memory:SetText(table.concat(lines, "\n"))
    end)
end

local function ShowList()
    local entries = section == 1 and roster.characters or roster.companions
    local items = {}
    for _, entry in ipairs(entries) do
        entry.key = entry.guid
        items[#items + 1] = entry
    end
    page.list:SetItems(items)
    page.empty:SetText(#items > 0 and "" or (section == 1 and
        "This account has no other characters on this realm." or
        "Nobody yet. Spend time with characters and they show here."))
    if chosen then
        for _, entry in ipairs(roster.characters) do
            if entry.guid == chosen.guid then
                chosen = entry
            end
        end
        for _, entry in ipairs(roster.companions) do
            if entry.guid == chosen.guid then
                chosen = entry
            end
        end
    end
    ShowDetail()
end

function Load()
    LA.Bridge.Request({ op = "roster" }, function(reply)
        if reply.ok then
            roster.characters = reply.characters or {}
            roster.companions = reply.companions or {}
            for _, entry in ipairs(roster.characters) do
                entry.isCharacter = true
            end
            ShowList()
        else
            Status("Not available: " .. (reply.reason or reply.error or "?"), 1, 0.3, 0.2)
        end
    end)
end

local function Build(self)
    page = self
    local UI = LA.UI

    -- Header: the chosen character.
    page.cardHint = page.header:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
    page.cardHint:SetPoint("LEFT", 64, 0)
    page.cardHint:SetText("Pick a character below.")
    page.card = CreateFrame("Frame", nil, page.header)
    page.card:SetAllPoints()
    page.card.button = UI.RoundButton(page.card, 58)
    page.card.button:SetPoint("LEFT", 60, 0)
    page.card.button:EnableMouse(false)
    page.card.name = page.card:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
    page.card.name:SetPoint("TOPLEFT", page.card.button, "TOPRIGHT", 12, -6)
    page.card.line = page.card:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
    page.card.line:SetPoint("TOPLEFT", page.card.name, "BOTTOMLEFT", 0, -4)
    page.card.where = page.card:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    page.card.where:SetPoint("TOPLEFT", page.card.line, "BOTTOMLEFT", 0, -3)
    page.card.where:SetTextColor(0.8, 0.8, 0.8)

    -- Bar: which list.
    page.sections = UI.SubTabs(page.bar, { "Your characters", "Characters you know" }, function(index)
        section = index
        chosen = nil
        ShowList()
    end)
    page.sections:Select(1)

    -- Paper: the list on the left, the details on the right.
    page.list = UI.List(page.content, ROWS, ROW_HEIGHT, function(row)
        row.icon = row:CreateTexture(nil, "ARTWORK")
        row.icon:SetSize(18, 18)
        row.icon:SetPoint("LEFT", 4, 0)
        row.name = row:CreateFontString(nil, "OVERLAY", "GameFontNormal")
        row.name:SetPoint("LEFT", row.icon, "RIGHT", 6, 0)
        row.name:SetWidth(110)
        row.name:SetJustifyH("LEFT")
        row.info = row:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
        row.info:SetPoint("LEFT", row.name, "RIGHT", 4, 0)
        row.info:SetWidth(150)
        row.info:SetJustifyH("LEFT")
    end, function(row, entry)
        UI.SetClassIcon(row.icon, entry.class)
        row.name:SetText(UI.Coloured(entry.name, entry.class))
        row.info:SetText("Level " .. (entry.level or "?") .. " - " .. Where(entry))
    end, function(entry)
        chosen = entry
        ShowDetail()
    end)
    page.list.frame:SetPoint("TOPLEFT", 4, -6)
    page.list.frame:SetSize(330, ROWS * ROW_HEIGHT)
    page.empty = UI.Text(page.content, 300, "GameFontDisable")
    page.empty:SetPoint("TOPLEFT", 12, -14)

    local divider = page.content:CreateTexture(nil, "ARTWORK")
    divider:SetTexture("Interface\\Buttons\\WHITE8X8")
    divider:SetVertexColor(0, 0, 0, 0.35)
    divider:SetSize(1, ROWS * ROW_HEIGHT)
    divider:SetPoint("TOPLEFT", 346, -6)

    local detailsHeading = UI.Heading(page.content, "About them")
    detailsHeading:SetPoint("TOPLEFT", 360, -8)
    page.details = UI.Text(page.content, 270)
    page.details:SetPoint("TOPLEFT", detailsHeading, "BOTTOMLEFT", 0, -6)
    page.details:SetSpacing(3)
    local memoryHeading = UI.Heading(page.content, "What they remember of you")
    memoryHeading:SetPoint("TOPLEFT", 360, -132)
    page.memory = UI.Text(page.content, 270)
    page.memory:SetPoint("TOPLEFT", memoryHeading, "BOTTOMLEFT", 0, -6)
    page.memory:SetSpacing(2)

    -- Bottom bar.
    page.act = UI.Button(page.footer, "", 120)
    page.act:SetPoint("LEFT", 0, 0)
    page.act:SetScript("OnClick", function()
        if chosen then
            Act(chosen)
        end
    end)
    page.whisper = UI.Button(page.footer, WHISPER, 100)
    page.whisper:SetPoint("LEFT", page.act, "RIGHT", 4, 0)
    page.whisper:SetScript("OnClick", function()
        if chosen then
            ChatFrame_SendTell(chosen.name)
        end
    end)
    page.status = page.footer:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    page.status:SetPoint("LEFT", page.whisper, "RIGHT", 10, 0)
    LA.Manager.CloseButton(page)
end

LA.On("loaded", function()
    LA.Manager.AddPage("Roster", Build, Load)
end)
