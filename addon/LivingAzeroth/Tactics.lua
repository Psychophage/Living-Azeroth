-- SPDX-License-Identifier: GPL-2.0-or-later
-- The Tactics tab: who the changes are for (all the player's bots or one), tactic sets that set many switches
-- at once (the player can save their own), the switches themselves, and Undo. Every box shows the bots' real
-- state: a change shows once the server says the bots have made it.
local _, LA = ...

local Tactics = {}
LA.Tactics = Tactics

local GROUPS = {
    { title = "Fighting", items = {
        { "join", "Join in when I attack", "Before my first hit lands" },
        { "aoe", "Use area attacks", "When enemies bunch up" },
        { "behind", "Attack from behind", "Melee stays out of the front" },
        { "threat", "Wait for the tank", "Hold damage until it has threat" },
        { "passive", "Stay out of fights", "Follow, but never fight" },
    } },
    { title = "Staying alive", items = {
        { "avoid_aoe", "Avoid area damage", "Step out of fire and void zones" },
        { "potions", "Use potions", "Healing and mana potions when low" },
        { "run", "Run when outmatched", "Fall back instead of dying" },
        { "save_mana", "Save mana", "Healers skip small heals" },
    } },
    { title = "Out of combat", items = {
        { "loot", "Pick up loot", "After fights" },
        { "gather", "Gather herbs and ore", "When their professions allow" },
        { "food", "Eat and drink", "Rest after fights" },
        { "buffs", "Keep buffs up", "Their own and the group's" },
        { "mount", "Mount when I mount", "Ride with me" },
    } },
}

local SETS = {
    { name = "Questing", glyph = "follow", tone = "gold", text = "Keep up, help out, pick things up.",
        on = { "join", "aoe", "behind", "avoid_aoe", "potions", "run", "loot", "gather", "food", "buffs", "mount" } },
    { name = "Dungeon", glyph = "tank", tone = "steel", text = "Let the tank lead; mind the threat.",
        on = { "aoe", "behind", "threat", "avoid_aoe", "potions", "save_mana", "loot", "food", "buffs" } },
    { name = "Raid", glyph = "leader", tone = "violet", text = "Threat and mana discipline.",
        on = { "behind", "threat", "avoid_aoe", "potions", "save_mana", "food", "buffs" } },
    { name = "Grinding", glyph = "attack", tone = "red", text = "Fight everything nearby, loot it all.",
        on = { "join", "aoe", "avoid_aoe", "potions", "save_mana", "loot", "gather", "food", "buffs" } },
    { name = "Quietly", glyph = "passive", tone = "violet", text = "Follow and stay out of fights.",
        on = { "passive", "avoid_aoe", "potions", "run", "food", "mount" } },
    { name = "Guard me", glyph = "guard", tone = "steel", text = "Defend me; fight anything that comes.",
        on = { "join", "aoe", "avoid_aoe", "potions", "food", "buffs" } },
}

local page
local target = "all" -- "all" or a bot's guid
local checks = {}
local before -- the switches when the page was opened or a set was used, for Undo: guid -> { id -> on }

local function AllBots()
    local bots = {}
    for _, bot in pairs(LA.Bots.Mine()) do
        if bot.commandable then
            bots[#bots + 1] = bot
        end
    end
    table.sort(bots, function(a, b) return a.name < b.name end)
    return bots
end

-- The bots the tab is changing, by name.
function Tactics.Targets()
    local bots = {}
    for _, bot in ipairs(AllBots()) do
        if target == "all" or target == bot.guid then
            bots[#bots + 1] = bot
        end
    end
    return bots
end
local Targets = Tactics.Targets

local function Sets()
    local sets = {}
    for _, set in ipairs(SETS) do
        sets[#sets + 1] = set
    end
    for _, set in ipairs(LivingAzerothDB.tactics.sets) do
        sets[#sets + 1] = set
    end
    return sets
end

local function Remember()
    before = {}
    for _, bot in ipairs(Targets()) do
        before[bot.guid] = {}
        for id, on in pairs(bot.switches or {}) do
            before[bot.guid][id] = on
        end
    end
end

-- Turns a switch on or off for every target bot whose class has it and that is not already that way.
local function SetSwitch(id, on, bots)
    for _, bot in ipairs(bots or Targets()) do
        local now = bot.switches and bot.switches[id]
        if now ~= nil and now ~= on and not LA.Orders.IsPending(bot.guid, id) then
            LA.Orders.Give(bot, id, on)
        end
    end
end

local function UseSet(set)
    Remember()
    local wanted = {}
    for _, id in ipairs(set.on) do
        wanted[id] = true
    end
    for id in pairs(checks) do
        SetSwitch(id, wanted[id] == true)
    end
end

local function Undo()
    for _, bot in ipairs(Targets()) do
        for id, on in pairs(before and before[bot.guid] or {}) do
            SetSwitch(id, on, { bot })
        end
    end
end

-- What a switch is across the targets: true (all on), false (all off), "some", or nil (no target has it).
local function State(id)
    local on, off = 0, 0
    for _, bot in ipairs(Targets()) do
        local value = bot.switches and bot.switches[id]
        if value == true then
            on = on + 1
        elseif value == false then
            off = off + 1
        end
    end
    if on + off == 0 then
        return nil
    end
    return off == 0 or (on > 0 and "some")
end

-- The set the targets' switches match, if any.
local function Matching()
    for _, set in ipairs(Sets()) do
        local wanted = {}
        for _, id in ipairs(set.on) do
            wanted[id] = true
        end
        local matches = true
        for id in pairs(checks) do
            local state = State(id)
            if state ~= nil and state ~= (wanted[id] == true) then
                matches = false
            end
        end
        if matches then
            return set
        end
    end
end

local function Changed()
    for _, bot in ipairs(Targets()) do
        for id, on in pairs(before and before[bot.guid] or {}) do
            if bot.switches and bot.switches[id] ~= on then
                return true
            end
        end
    end
    return false
end

local function Waiting()
    local count = 0
    for _, bot in ipairs(Targets()) do
        if LA.Orders.Pending(bot.guid) then
            count = count + 1
        end
    end
    return count
end


local UI, Style = LA.UI, LA.Style
local Refresh

-- The "Apply to" chips: all the player's bots, then each bot with its role. They wrap onto a second line.
local function ShowTargets(bots)
    local chips, row = page.targets, page.targetRow
    local width = row:GetWidth()
    local entries = { { key = "all", label = "All my bots", glyph = "bot" } }
    for _, bot in ipairs(bots) do
        entries[#entries + 1] = { key = bot.guid, label = bot.name, bot = bot }
    end
    local x, y = 0, 0
    for i, entry in ipairs(entries) do
        local chip = chips[i]
        if not chip then
            chip = UI.Chip(row, "", 70)
            chip.glyphIcon = UI.Icon(chip, 12, "medal")
            chip:SetScript("OnClick", function(self)
                target = self.key
                Remember()
                Refresh()
            end)
            chips[i] = chip
        end
        chip.key = entry.key
        local role = entry.bot and Style.ROLE_LOOK[entry.bot.role or "damage"] or nil
        chip.glyphIcon:Set(role and role[1] or "bot", role and role[2] or "gold")
        chip:SetText(entry.label)
        chip:FitText(16)
        chip:SetOn(target == entry.key)
        if entry.bot then
            local colour = UI.ClassColor(entry.bot.class)
            chip.text:SetTextColor(colour.r, colour.g, colour.b)
        end
        if x > 0 and x + chip:GetWidth() > width then
            x, y = 0, y + UI.BUTTON_HEIGHT + 4
        end
        chip:ClearAllPoints()
        chip:SetPoint("TOPLEFT", x, -y)
        chip:Show()
        x = x + chip:GetWidth() + 4
    end
    for i = #entries + 1, #chips do
        chips[i]:Hide()
    end
    LA.SetShown(page.noBots, #bots == 0)
    LA.SetShown(page.toRoster, #bots == 0)
    row:SetHeight(y + UI.BUTTON_HEIGHT)
    return y + UI.BUTTON_HEIGHT
end

-- The set tiles: the standard sets, then the player's own (right-click one of those to forget it).
local function ShowSets(current, usable)
    local tiles = page.sets
    local sets = Sets()
    local columns, gap, height = 8, 4, 44
    local each = math.floor((page.setRow:GetWidth() - (columns - 1) * gap) / columns)
    for i, set in ipairs(sets) do
        local tile = tiles[i]
        if not tile then
            tile = CreateFrame("Button", nil, page.setRow)
            tile:SetSize(each, height)
            tile:RegisterForClicks("LeftButtonUp", "RightButtonUp")
            UI.Surface(tile, "row")
            UI.HoverLight(tile)
            tile.icon = UI.Icon(tile, 22, "tile")
            tile.icon:SetPoint("TOP", 0, -4)
            tile.name = UI.Text(tile, "label", each - 4)
            tile.name:SetJustifyH("CENTER")
            tile.name:SetWordWrap(false)
            tile.name:SetPoint("BOTTOM", 0, 4)
            tile:SetScript("OnClick", function(self, mouse)
                if mouse == "RightButton" and self.own then
                    StaticPopup_Show("LIVINGAZEROTH_FORGET_SET", self.set.name, nil, self.set)
                elseif #AllBots() > 0 then
                    UseSet(self.set)
                    Refresh()
                end
            end)
            tile:SetScript("OnEnter", function(self)
                GameTooltip:SetOwner(self, "ANCHOR_TOP")
                GameTooltip:AddLine(self.set.name, 1, 0.82, 0)
                GameTooltip:AddLine(self.set.text or "Your own set.", 1, 1, 1, true)
                GameTooltip:AddLine(self.own and "Click to use it; right-click to forget it." or "Click to use it.",
                    0.6, 0.6, 0.6)
                GameTooltip:Show()
            end)
            tile:SetScript("OnLeave", GameTooltip_Hide)
            tiles[i] = tile
        end
        tile.set, tile.own = set, i > #SETS
        tile.icon:Set(set.glyph or "tactics", set.tone or "bronze")
        tile.name:SetText(set.name)
        local on = current == set
        tile.surface:Set(on and "rowOn" or "row")
        tile.icon:SetActive(on)
        tile.icon:SetDim(not usable)
        local colour = on and Style.gold or Style.T().text
        tile.name:SetTextColor(colour[1], colour[2], colour[3])
        tile:ClearAllPoints()
        tile:SetPoint("TOPLEFT", ((i - 1) % columns) * (each + gap), -math.floor((i - 1) / columns) * (height + gap))
        tile:Show()
    end
    for i = #sets + 1, #tiles do
        tiles[i]:Hide()
    end
    local rows = math.ceil(#sets / columns)
    page.setRow:SetHeight(rows * (height + gap) - gap)
    return rows * (height + gap) - gap
end

function Refresh()
    if not page or not LA.Manager.IsShown(page) then
        return
    end
    local all = AllBots()
    if target ~= "all" and not LA.Bots.Mine()[target] then
        target = "all"
    end
    local usable = #all > 0
    local y = ShowTargets(all) + 10
    page.setRow:SetPoint("TOPLEFT", 0, -y)
    local current = usable and Matching() or nil
    y = y + ShowSets(current, usable) + 8

    page.lineRow:SetPoint("TOPLEFT", 0, -y)
    local waiting = Waiting()
    if not usable then
        page.line:SetText("The switches show and change your bots' tactics once one is with you.")
    elseif current then
        page.line:SetText("|cffffd100" .. current.name .. ":|r " .. (current.text or "your own set."))
    else
        page.line:SetText("|cffffd100Your own mix:|r these switches match none of the sets.")
    end
    if waiting > 0 then
        page.line:SetText(page.line:GetText() .. "  |cffa89f8cWaiting for " .. waiting .. "...|r")
    end
    page.save:SetUsable(usable)
    page.undo:SetUsable(usable and Changed())
    page.formation:SetUsable(usable)
    y = y + UI.BUTTON_HEIGHT + 8

    page.switches:SetPoint("TOPLEFT", 0, -y)
    local _, height = LA.Manager.BodySize()
    page.switches:SetHeight(height - y)
    for id, check in pairs(checks) do
        local state -- false (all off) must stay false, not become "no bot can"
        if usable then
            state = State(id)
        end
        check:SetChecked(state == true or state == "some")
        check:SetMixed(state == "some")
        check:SetUsable(state ~= nil)
    end
end
Tactics.Refresh = Refresh

StaticPopupDialogs.LIVINGAZEROTH_SAVE_SET = {
    text = "Name this tactic set:",
    button1 = SAVE,
    button2 = CANCEL,
    hasEditBox = true,
    maxLetters = 24,
    OnAccept = function(self)
        local name = strtrim(self.editBox:GetText())
        if name == "" then
            return
        end
        local on = {}
        for id in pairs(checks) do
            if State(id) == true then
                on[#on + 1] = id
            end
        end
        local sets = LivingAzerothDB.tactics.sets
        for i = #sets, 1, -1 do
            if sets[i].name == name then
                table.remove(sets, i)
            end
        end
        table.insert(sets, { name = name, on = on })
        Refresh()
    end,
    EditBoxOnEnterPressed = function(self)
        StaticPopupDialogs.LIVINGAZEROTH_SAVE_SET.OnAccept(self:GetParent())
        self:GetParent():Hide()
    end,
    timeout = 0,
    whileDead = true,
    hideOnEscape = true,
}

StaticPopupDialogs.LIVINGAZEROTH_FORGET_SET = {
    text = "Forget the tactic set \"%s\"?",
    button1 = YES,
    button2 = NO,
    OnAccept = function(_, set)
        local sets = LivingAzerothDB.tactics.sets
        for i = #sets, 1, -1 do
            if sets[i] == set then
                table.remove(sets, i)
            end
        end
        Refresh()
    end,
    timeout = 0,
    whileDead = true,
    hideOnEscape = true,
}

local function Build(self)
    page = self
    local width = LA.Manager.BodySize()

    local applyTo = UI.Text(page.frame, "heading")
    applyTo:SetPoint("TOPLEFT", 0, -4)
    applyTo:SetText("Apply to")
    local indent = math.ceil(applyTo:GetStringWidth()) + 10
    page.targetRow = CreateFrame("Frame", nil, page.frame)
    page.targetRow:SetPoint("TOPLEFT", indent, 0)
    page.targetRow:SetWidth(width - indent)
    page.targets = {}
    page.noBots = UI.Text(page.targetRow, "small")
    page.noBots:SetPoint("LEFT", 100, 0)
    page.noBots:SetText("No bots with you yet.")
    page.toRoster = UI.Button(page.targetRow, "Bring one in", nil, "plain", "roster")
    page.toRoster:SetPoint("LEFT", page.noBots, "RIGHT", 10, 0)
    page.toRoster:SetScript("OnClick", function() LA.Manager.Show(1) end)

    page.setRow = CreateFrame("Frame", nil, page.frame)
    page.setRow:SetWidth(width)
    page.sets = {}

    page.lineRow = CreateFrame("Frame", nil, page.frame)
    page.lineRow:SetSize(width, UI.BUTTON_HEIGHT)
    page.formation = UI.Button(page.lineRow, "Formation", nil, "plain", "formation")
    page.formation:SetPoint("RIGHT")
    page.formation:SetScript("OnClick", function() LA.Formation.Toggle() end)
    page.undo = UI.Button(page.lineRow, "Undo", 56, "plain")
    page.undo:SetPoint("RIGHT", page.formation, "LEFT", -4, 0)
    page.undo:SetScript("OnClick", function()
        Undo()
        Refresh()
    end)
    page.save = UI.Button(page.lineRow, "Save as a set")
    page.save:SetPoint("RIGHT", page.undo, "LEFT", -4, 0)
    page.save:SetScript("OnClick", function() StaticPopup_Show("LIVINGAZEROTH_SAVE_SET") end)
    page.line = UI.Text(page.lineRow, "body")
    page.line:SetPoint("LEFT", 2, 0)
    page.line:SetPoint("RIGHT", page.save, "LEFT", -10, 0)
    page.line:SetWordWrap(false)

    -- The switches, one group to a column.
    page.switches = UI.Inset(page.frame)
    page.switches:SetWidth(width)
    local gap = 14
    local column = math.floor((width - 20 - (#GROUPS - 1) * gap) / #GROUPS)
    for g, group in ipairs(GROUPS) do
        local x = 10 + (g - 1) * (column + gap)
        local title = UI.Heading(page.switches, group.title)
        title:SetPoint("TOPLEFT", x, -8)
        title:SetWidth(column)
        local y = 32
        for _, item in ipairs(group.items) do
            local id = item[1]
            local check = UI.Check(page.switches, item[2], item[3], column)
            check:SetPoint("TOPLEFT", x, -y)
            y = y + check:GetHeight() + 8
            check.onToggle = function()
                SetSwitch(id, State(id) ~= true)
                Refresh() -- the box shows the bots' state, not the click
            end
            check:SetScript("OnEnter", function(c)
                local state = State(id)
                local why = state == nil and "None of these bots' classes can do this."
                    or state == "some" and "Some of these bots have this on. Click to turn it on for all of them."
                if #AllBots() > 0 and why then
                    GameTooltip:SetOwner(c, "ANCHOR_RIGHT")
                    GameTooltip:AddLine(item[2], 1, 1, 1)
                    GameTooltip:AddLine(why, nil, nil, nil, true)
                    GameTooltip:Show()
                end
            end)
            check:SetScript("OnLeave", GameTooltip_Hide)
            checks[id] = check
        end
    end
end

local function Show()
    Remember()
    Refresh()
end

LA.On("loaded", function()
    LivingAzerothDB.tactics = LivingAzerothDB.tactics or {}
    LivingAzerothDB.tactics.sets = LivingAzerothDB.tactics.sets or {}
    LA.Manager.AddPage("Tactics", "tactics", Build, Show)
end)
LA.On("bots:changed", function() Refresh() end)
LA.On("orders:changed", function() Refresh() end)
