-- SPDX-License-Identifier: GPL-2.0-or-later
-- The Roster tab: the player's other characters (bring one in as a bot to join the party, or send it home) and
-- the characters they have come to know best (invite the ones online). The chosen one shows on the parchment:
-- for a bot in the player's group its talents, gear, bags and money, and for anyone what they remember of you.
local _, LA = ...

local Roster = {}
LA.Roster = Roster

local page
local roster = { characters = {}, companions = {} }
local chosen -- an entry

local function Status(text, r, g, b)
    page.status:SetText(text or "")
    page.status:SetTextColor(r or 1, g or 0.82, b or 0)
end

local function Money(copper)
    return GetCoinTextureString(copper or 0)
end

-- "party" or "raid", whichever the player is in (the server's in_party means either).
local function GroupWord()
    return GetNumRaidMembers() > 0 and "raid" or "party"
end

local function Where(entry)
    if entry.in_party then
        return "In your " .. GroupWord()
    elseif entry.in_world or entry.online then
        return entry.zone ~= "" and entry.zone or "In the world"
    end
    return entry.isCharacter and "Resting" or "Away"
end

local function Familiarity(entry)
    -- Familiarity grows by seeing them around (1) and doing things together (4), up to 100.
    local value = entry.familiarity or 0
    return value >= 60 and "Old friends" or value >= 25 and "Well known to you" or value >= 8 and
        "Seen about often" or "A familiar face"
end

local Load

local function ShowDetail()
    local stack, d = page.stack, page.parts
    stack:Reset()
    for _, region in pairs(d) do
        region:Hide()
    end
    page.list:Choose(chosen and chosen.guid)
    page.act:Disable()
    page.whisper:Disable()
    page.act:SetText(chosen and chosen.isCharacter and (chosen.in_world and "Send home" or "Bring") or INVITE)
    if not chosen then
        stack:Add(d.intro)
        page:SetDetailHeight(stack:Height())
        return
    end
    local entry = chosen
    d.portrait:SetCharacter(entry.guid, entry.class)
    stack:Add(d.portrait, 0, 0, 0)
    d.name:SetText(entry.name)
    stack:Add(d.name, 4, 62)
    d.line:SetText("Level " .. (entry.level or "?") .. " " .. LA.UI.ClassName(entry.class))
    stack:Add(d.line, 2, 62)
    d.where:SetText(Where(entry))
    stack:Add(d.where, 2, 62)
    stack.y = math.min(stack.y, -66)

    stack:Add(d.aboutTitle, 12)
    local bot = LA.Bots.Mine()[entry.guid]
    if bot and bot.commandable and d.about.forGuid ~= entry.guid then
        d.about.forGuid = entry.guid
        d.about:SetText("Asking...")
        LA.Bridge.Request({ op = "inspect", bot = entry.guid }, function(reply)
            if chosen ~= entry or not reply.ok then
                return
            end
            d.about:SetText(table.concat({
                "Talents: " .. (reply.spec ~= "" and reply.spec or "none chosen"),
                "Gear: item level " .. (reply.item_level or 0),
                "Bags: " .. (reply.free_slots or 0) .. " free slots",
                "Money: " .. Money(reply.money),
                "Doing: " .. LA.Orders.Standing(reply.bot or bot).state,
            }, "\n"))
            ShowDetail()
        end)
    elseif bot and bot.commandable then
        -- already asked; the reply shows when it comes
    elseif entry.isCharacter then
        d.about.forGuid = nil
        d.about:SetText(entry.in_world and "With someone else." or
            "Resting. Bring them in and they join your party as a bot.")
    else
        d.about.forGuid = nil
        d.about:SetText(Familiarity(entry) .. ".")
    end
    stack:Add(d.about, 4)

    stack:Add(d.memoryTitle, 14)
    if not d.memory.forGuid or d.memory.forGuid ~= entry.guid then
        d.memory.forGuid = entry.guid
        d.memory:SetText("Asking...")
        LA.Bridge.Request({ op = "memory", character = entry.guid }, function(reply)
            if chosen ~= entry then
                return
            end
            local notes = reply.ok and reply.remembers_you or {}
            local lines = {}
            for _, note in ipairs(notes) do
                lines[#lines + 1] = "- " .. note.text
            end
            d.memory:SetText(#lines > 0 and table.concat(lines, "\n") or (entry.isCharacter and
                "Your own character: nothing to remember." or
                "Nothing yet. They remember what you do together and what you tell them."))
            ShowDetail()
        end)
    end
    stack:Add(d.memory, 4)
    page:SetDetailHeight(stack:Height())

    if entry.isCharacter then
        page.act:Enable()
    elseif entry.online and not entry.in_party then
        page.act:Enable()
    end
    if entry.in_world or entry.online then
        page.whisper:Enable()
    end
end

local function Act()
    local entry = chosen
    if not entry then
        return
    end
    if not entry.isCharacter then
        InviteUnit(entry.name)
        Status("Invited " .. entry.name .. ".")
        return
    end
    local bring = not entry.in_world
    Status(bring and ("Bringing " .. entry.name .. "...") or ("Sending " .. entry.name .. " home..."))
    LA.Bridge.Request({ op = bring and "bring" or "dismiss", name = entry.name }, function(reply)
        if reply.ok then
            Status(bring and (entry.name .. " is on the way.") or (entry.name .. " has gone home."))
        else
            Status(reply.reason or reply.error or "?", 1, 0.3, 0.2)
        end
        Load()
    end)
end

local function ShowList()
    local items = { { header = true, key = "mine", text = "Your characters" } }
    local inParty = 0
    local tag = GroupWord() == "raid" and "Raid" or "Party"
    for _, entry in ipairs(roster.characters) do
        entry.isCharacter = true
        inParty = inParty + (entry.in_party and 1 or 0)
        items[#items + 1] = { key = entry.guid, under = "mine", entry = entry, text = LA.UI.Coloured(entry.name,
            entry.class), tag = entry.in_party and tag or entry.in_world and "Out" or ("Level " .. entry.level),
            icon = function(texture) LA.UI.SetClassIcon(texture, entry.class) end }
    end
    items[#items + 1] = { header = true, key = "known", text = "Characters you know" }
    for _, entry in ipairs(roster.companions) do
        items[#items + 1] = { key = entry.guid, under = "known", entry = entry, text = LA.UI.Coloured(entry.name,
            entry.class), tag = entry.online and (entry.in_party and tag or "Online") or ("Level " .. entry.level),
            icon = function(texture) LA.UI.SetClassIcon(texture, entry.class) end }
    end
    page.list:SetItems(items)
    page.count:SetText(#roster.characters .. " other " .. (#roster.characters == 1 and "character" or "characters") ..
        ", " .. inParty .. " in your " .. GroupWord())
    if chosen then
        for _, item in ipairs(items) do
            if item.entry and item.entry.guid == chosen.guid then
                chosen = item.entry
            end
        end
    end
    ShowDetail()
end

function Load()
    if page and page.parts then
        page.parts.about.forGuid, page.parts.memory.forGuid = nil, nil
    end
    LA.Bridge.Request({ op = "roster" }, function(reply)
        if reply.ok then
            roster.characters = reply.characters or {}
            roster.companions = reply.companions or {}
            ShowList()
        else
            Status("Not available: " .. (reply.reason or reply.error or "?"), 1, 0.3, 0.2)
        end
    end)
end

local function Build(self)
    page = self
    local UI = LA.UI

    page.count = page.top:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    page.count:SetPoint("LEFT", 4, 0)
    page.status = page.top:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    page.status:SetPoint("RIGHT", -6, 0)

    page.list = UI.List(page.left, function(item)
        chosen = item.entry
        LA.Manager.ScrollToTop()
        ShowDetail()
    end)

    local d = {}
    d.intro = UI.Paper(page.detail, "body", 280)
    d.intro:SetText("Your other characters can join you as bots: choose one and press Bring. The characters " ..
        "you spend time with are listed too, with what they remember of you.")
    d.portrait = UI.RoundButton(page.detail, 54)
    d.portrait:EnableMouse(false)
    d.name = UI.Paper(page.detail, "title", 220)
    d.line = UI.Paper(page.detail, "body", 220)
    d.where = UI.Paper(page.detail, "small", 220)
    d.aboutTitle = UI.Paper(page.detail, "title", 280)
    d.aboutTitle:SetText("About them")
    d.about = UI.Paper(page.detail, "body", 280)
    d.memoryTitle = UI.Paper(page.detail, "title", 280)
    d.memoryTitle:SetText("What they remember of you")
    d.memory = UI.Paper(page.detail, "body", 280)
    page.parts = d
    page.stack = UI.Stack(page.detail)

    page.act = UI.Button(page.controls, "Bring", 100)
    page.act:SetPoint("LEFT", 0, 1)
    page.act:SetScript("OnClick", Act)
    page.whisper = UI.Button(page.controls, WHISPER, 100)
    page.whisper:SetPoint("LEFT", page.act, "RIGHT", 0, 0)
    page.whisper:SetScript("OnClick", function()
        if chosen then
            ChatFrame_SendTell(chosen.name)
        end
    end)
    local refresh = UI.Button(page.controls, "Refresh", 100)
    refresh:SetPoint("RIGHT", -3, 1)
    refresh:SetScript("OnClick", Load)
end

LA.On("loaded", function()
    LA.Manager.AddPage("Roster", Build, Load)
end)
