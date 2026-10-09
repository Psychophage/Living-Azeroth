-- SPDX-License-Identifier: GPL-2.0-or-later
-- The Roster tab: the player's other characters (bring one in as a bot to join the party, or send it home), the
-- characters they have come to know best, and a read-only look at a bot in their group: talents, gear, bags,
-- money and where it is.
local _, LA = ...

local Roster = {}
LA.Roster = Roster

local CLASSES = { "WARRIOR", "PALADIN", "HUNTER", "ROGUE", "PRIEST", "DEATHKNIGHT", "SHAMAN", "MAGE", "WARLOCK",
    nil, "DRUID" }
local ROWS = 8

local page
local roster = { characters = {}, companions = {} }
local chosen -- a row's entry

local function Status(text, r, g, b)
    page.status:SetText(text or "")
    page.status:SetTextColor(r or 0.8, g or 0.8, b or 0.8)
end

local function Coloured(entry)
    local class = CLASSES[entry.class or 0]
    local color = class and RAID_CLASS_COLORS[class] or NORMAL_FONT_COLOR
    return string.format("|cff%02x%02x%02x%s|r", color.r * 255, color.g * 255, color.b * 255, entry.name or "?")
end

local function ClassName(entry)
    local class = CLASSES[entry.class or 0]
    return class and LOCALIZED_CLASS_NAMES_MALE[class] or ""
end

local function Money(copper)
    copper = copper or 0
    return string.format("%dg %ds %dc", math.floor(copper / 10000), math.floor(copper / 100) % 100, copper % 100)
end

local Load

local function ShowDetail()
    local entry = chosen
    for _, row in ipairs(page.rowsAll) do
        if row.entry and entry and row.entry.guid == entry.guid then
            row:LockHighlight()
        else
            row:UnlockHighlight()
        end
    end
    if not entry then
        page.detailName:SetText("")
        page.detail:SetText("Pick a character.")
        page.whisper:Hide()
        return
    end
    page.detailName:SetText(Coloured(entry))
    local lines = { "Level " .. (entry.level or "?") .. " " .. ClassName(entry) }
    if entry.zone and entry.zone ~= "" then
        lines[#lines + 1] = entry.zone
    end
    page.detail:SetText(table.concat(lines, "\n"))
    LA.SetShown(page.whisper, entry.in_world or entry.online)
    local bot = LA.Bots.Mine()[entry.guid]
    if not (bot and bot.commandable) then
        return
    end
    LA.Bridge.Request({ op = "inspect", bot = entry.guid }, function(reply)
        if chosen ~= entry or not reply.ok then
            return
        end
        lines[#lines + 1] = ""
        lines[#lines + 1] = "|cffffd100Talents|r " .. (reply.spec ~= "" and reply.spec or "none chosen")
        lines[#lines + 1] = "|cffffd100Gear|r item level " .. (reply.item_level or 0)
        lines[#lines + 1] = "|cffffd100Bags|r " .. (reply.free_slots or 0) .. " free slots"
        lines[#lines + 1] = "|cffffd100Money|r " .. Money(reply.money)
        lines[#lines + 1] = "|cffffd100Doing|r " .. LA.Orders.Standing(reply.bot or bot).state
        page.detail:SetText(table.concat(lines, "\n"))
    end)
end

local function Act(entry)
    if entry.isCharacter then
        local bring = not entry.in_world
        Status(bring and ("Bringing " .. entry.name .. "...") or ("Sending " .. entry.name .. " home..."))
        LA.Bridge.Request({ op = bring and "bring" or "dismiss", name = entry.name }, function(reply)
            if reply.ok then
                Status(bring and (entry.name .. " is on the way.") or (entry.name .. " has gone home."), 0.5, 0.9,
                    0.5)
            else
                Status((reply.messages and reply.messages[1]) or reply.reason or reply.error or "?", 1, 0.3, 0.2)
            end
            Load()
        end)
    else
        InviteUnit(entry.name)
    end
end

local function Row(parent, index, y)
    local row = CreateFrame("Button", nil, parent)
    row:SetSize(400, 22)
    row:SetPoint("TOPLEFT", 6, y - (index - 1) * 22)
    row.name = row:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
    row.name:SetPoint("LEFT", 4, 0)
    row.name:SetWidth(120)
    row.name:SetJustifyH("LEFT")
    row.info = row:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    row.info:SetPoint("LEFT", 128, 0)
    row.info:SetWidth(170)
    row.info:SetJustifyH("LEFT")
    row.action = LA.Manager.Button(row, "", 92)
    row.action:SetHeight(20)
    row.action:SetPoint("RIGHT", -2, 0)
    row.action:SetScript("OnClick", function()
        Act(row.entry)
    end)
    row:SetHighlightTexture("Interface\\QuestFrame\\UI-QuestTitleHighlight", "ADD")
    row:SetScript("OnClick", function(self)
        chosen = self.entry
        ShowDetail()
    end)
    page.rowsAll[#page.rowsAll + 1] = row
    return row
end

local function Fill(rows, entries, describe)
    for i, row in ipairs(rows) do
        local entry = entries[i]
        row.entry = entry
        if entry then
            row.name:SetText(Coloured(entry))
            local info, action = describe(entry)
            row.info:SetText(info)
            if action then
                row.action:SetText(action)
                row.action:Show()
            else
                row.action:Hide()
            end
            row:Show()
        else
            row:Hide()
        end
    end
end

local function ShowRoster()
    for _, entry in ipairs(roster.characters) do
        entry.isCharacter = true
    end
    Fill(page.characterRows, roster.characters, function(entry)
        local where = entry.in_party and "In your party" or entry.in_world and "In the world" or "At rest"
        return "Level " .. entry.level .. " - " .. where, entry.in_world and "Send home" or "Bring"
    end)
    Fill(page.companionRows, roster.companions, function(entry)
        local where = entry.in_party and "In your party" or entry.online and (entry.zone ~= "" and entry.zone or
            "Online") or "Offline"
        return "Level " .. entry.level .. " - " .. where, (entry.online and not entry.in_party) and "Invite" or nil
    end)
    LA.SetShown(page.noCharacters, #roster.characters == 0)
    LA.SetShown(page.noCompanions, #roster.companions == 0)
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
            ShowRoster()
        else
            Status("Not available: " .. (reply.reason or reply.error or "?"), 1, 0.3, 0.2)
        end
    end)
end

local function Build(self)
    page = self
    page.rowsAll = {}
    page.status = page:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    page.status:SetPoint("BOTTOMLEFT", 4, 4)

    LA.Manager.Heading(page, "Your other characters", 0, -4)
    local mine = LA.Manager.Inset(page)
    mine:SetPoint("TOPLEFT", 0, -22)
    mine:SetSize(416, ROWS * 22 + 12)
    page.characterRows = {}
    for i = 1, ROWS do
        page.characterRows[i] = Row(mine, i, -6)
    end
    page.noCharacters = mine:CreateFontString(nil, "OVERLAY", "GameFontDisable")
    page.noCharacters:SetPoint("CENTER")
    page.noCharacters:SetText("This account has no other characters on this realm.")

    LA.Manager.Heading(page, "Characters you know best", 0, -218)
    local known = LA.Manager.Inset(page)
    known:SetPoint("TOPLEFT", 0, -236)
    known:SetSize(416, ROWS * 22 + 12)
    page.companionRows = {}
    for i = 1, ROWS do
        page.companionRows[i] = Row(known, i, -6)
    end
    page.noCompanions = known:CreateFontString(nil, "OVERLAY", "GameFontDisable")
    page.noCompanions:SetPoint("CENTER")
    page.noCompanions:SetText("Nobody yet. Spend time with characters and they will show here.")

    local detail = LA.Manager.Inset(page)
    detail:SetPoint("TOPLEFT", 428, -22)
    detail:SetPoint("BOTTOMRIGHT", 0, 24)
    page.detailName = detail:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
    page.detailName:SetPoint("TOPLEFT", 12, -12)
    page.detail = detail:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
    page.detail:SetPoint("TOPLEFT", 12, -38)
    page.detail:SetPoint("TOPRIGHT", -12, -38)
    page.detail:SetJustifyH("LEFT")
    page.detail:SetSpacing(3)
    page.whisper = LA.Manager.Button(detail, "Whisper", 100)
    page.whisper:SetPoint("BOTTOMLEFT", 12, 12)
    page.whisper:SetScript("OnClick", function()
        if chosen then
            ChatFrame_SendTell(chosen.name)
        end
    end)
end

LA.On("loaded", function()
    LA.Manager.AddPage("Roster", Build, Load)
end)
