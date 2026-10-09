-- SPDX-License-Identifier: GPL-2.0-or-later
-- The Tactics tab: who the changes are for (all the player's bots or one), tactic sets that set many switches
-- at once (the player can save their own), the switches themselves, and Undo. Every box shows the bots' real
-- state: a change shows once the server says the bots have made it.
local _, LA = ...

local Tactics = {}
LA.Tactics = Tactics

local GROUPS = {
    { title = "Fighting", column = 1, items = {
        { "join", "Join in when I attack", "Before my first hit lands" },
        { "aoe", "Use area attacks", "When enemies bunch up" },
        { "behind", "Attack from behind", "Melee stays out of the front" },
        { "threat", "Wait for the tank", "Hold damage until it has threat" },
        { "passive", "Stay out of fights", "Follow, but never fight" },
    } },
    { title = "Staying alive", column = 2, items = {
        { "avoid_aoe", "Avoid area damage", "Step out of fire and void zones" },
        { "potions", "Use potions", "Healing and mana potions when low" },
        { "run", "Run when outmatched", "Fall back instead of dying" },
        { "save_mana", "Save mana", "Healers skip small heals" },
    } },
    { title = "Out of combat", column = 3, items = {
        { "loot", "Pick up loot", "After fights" },
        { "gather", "Gather herbs and ore", "When their professions allow" },
        { "food", "Eat and drink", "Rest after fights" },
        { "mount", "Mount when I mount", "Ride with me" },
    } },
}

local SETS = {
    { name = "Questing", icon = "Interface\\Icons\\INV_Misc_Map_01", text = "Keep up, help out, pick things up.",
        on = { "join", "aoe", "behind", "avoid_aoe", "potions", "run", "loot", "gather", "food", "mount" } },
    { name = "Dungeon", icon = "Interface\\Icons\\INV_Shield_06", text = "Let the tank lead; mind the threat.",
        on = { "aoe", "behind", "threat", "avoid_aoe", "potions", "save_mana", "loot", "food" } },
    { name = "Raid", icon = "Interface\\Icons\\INV_Misc_Head_Dragon_Black", text = "Threat and mana discipline.",
        on = { "behind", "threat", "avoid_aoe", "potions", "save_mana", "food" } },
    { name = "Grinding", icon = "Interface\\Icons\\Ability_Warrior_Rampage",
        text = "Fight everything nearby, loot it all.",
        on = { "join", "aoe", "avoid_aoe", "potions", "save_mana", "loot", "gather", "food" } },
    { name = "Quietly", icon = "Interface\\Icons\\Spell_Nature_Sleep", text = "Follow and stay out of fights.",
        on = { "passive", "avoid_aoe", "potions", "run", "food", "mount" } },
    { name = "Guard me", icon = "Interface\\Icons\\Ability_Defend", text = "Defend me; fight anything that comes.",
        on = { "join", "aoe", "avoid_aoe", "potions", "food" } },
}

local MAX_TARGETS = 8

local page
local target = "all" -- "all" or a bot's guid
local checks = {}
local targetButtons = {}
local before -- the switches when the page was opened or a set was used, for Undo: guid -> { id -> on }

-- The bots the tab is changing, by name.
function Tactics.Targets()
    local bots = {}
    for guid, bot in pairs(LA.Bots.Mine()) do
        if bot.commandable and (target == "all" or target == guid) then
            bots[#bots + 1] = bot
        end
    end
    table.sort(bots, function(a, b) return a.name < b.name end)
    return bots
end
local Targets = Tactics.Targets

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

local function RefreshTargets()
    local bots = AllBots()
    if target ~= "all" and not LA.Bots.Mine()[target] then
        target = "all"
    end
    targetButtons[1]:SetChosen(target == "all")
    for i = 2, MAX_TARGETS do
        local button, bot = targetButtons[i], bots[i - 1]
        if bot then
            button.guid = bot.guid
            button:SetCharacter(bot.guid, bot.class)
            button.label:SetText(bot.name)
            button:SetChosen(target == bot.guid)
            button:Show()
        else
            button.guid = nil
            button:Hide()
        end
    end
end

local function Refresh()
    if not page or not page.content:IsVisible() then
        return
    end
    RefreshTargets()
    local bots = Targets()
    for id, check in pairs(checks) do
        local state = State(id)
        check:SetChecked(state == true or state == "some")
        if state == nil then
            check:Disable()
            check.label:SetFontObject("GameFontDisable")
        else
            check:Enable()
            check.label:SetFontObject("GameFontHighlight")
        end
        LA.SetShown(check.some, state == "some")
    end
    local set = Matching()
    if #bots == 0 then
        page.heading:SetText("No bots to command")
        page.description:SetText("Bring one of your characters in from the Roster tab, or invite a bot to your " ..
            "party. Their tactics show here.")
    elseif set then
        page.heading:SetText(set.name)
        page.description:SetText(set.text or "Your own set.")
    else
        page.heading:SetText("Your own mix")
        page.description:SetText("These switches match none of the sets. Save them as a set to use them again.")
    end
    UIDropDownMenu_SetText(page.sets, set and set.name or "Choose a set")
    local waiting = Waiting()
    page.status:SetText(waiting > 0 and ("Waiting for " .. waiting .. (waiting == 1 and " bot..." or " bots...")) or "")
    if Changed() then
        page.undo:Enable()
    else
        page.undo:Disable()
    end
    if #bots > 0 then
        page.save:Enable()
    else
        page.save:Disable()
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

local function ChooseTarget(value)
    target = value
    Remember()
    Refresh()
end

local function Build(self)
    page = self
    local UI = LA.UI

    -- Header: who the changes are for.
    local applyTo = page.header:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    applyTo:SetPoint("TOPLEFT", 64, -6)
    applyTo:SetText("Apply to")
    for i = 1, MAX_TARGETS do
        local button = UI.RoundButton(page.header, 42, i == 1 and "All my bots" or "")
        button:SetPoint("TOPLEFT", 66 + (i - 1) * 70, -22)
        if i == 1 then
            button:SetIcon("Interface\\Icons\\INV_Misc_Gear_01")
            button:SetScript("OnClick", function() ChooseTarget("all") end)
        else
            button:SetScript("OnClick", function(b) ChooseTarget(b.guid) end)
        end
        targetButtons[i] = button
    end

    -- Bar: the tactic sets.
    local setLabel = page.bar:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    setLabel:SetPoint("LEFT", 64, 0)
    setLabel:SetText("Tactic set:")
    page.sets = CreateFrame("Frame", "LivingAzerothTacticSets", page.bar, "UIDropDownMenuTemplate")
    page.sets:SetPoint("LEFT", setLabel, "RIGHT", -6, -2)
    UIDropDownMenu_SetWidth(page.sets, 180)
    UIDropDownMenu_Initialize(page.sets, function(_, level, menu)
        if level == 2 then
            local info = UIDropDownMenu_CreateInfo()
            info.text, info.notCheckable = "Forget this set", true
            info.func = function()
                CloseDropDownMenus()
                StaticPopup_Show("LIVINGAZEROTH_FORGET_SET", menu.name, nil, menu)
            end
            UIDropDownMenu_AddButton(info, 2)
            return
        end
        local current = Matching()
        for i, set in ipairs(Sets()) do
            local info = UIDropDownMenu_CreateInfo()
            info.text, info.icon = set.name, set.icon or "Interface\\Icons\\INV_Misc_Note_01"
            info.checked = current == set
            info.tooltipTitle, info.tooltipText = set.name, set.text or "Your own set."
            info.func = function()
                UseSet(set)
                Refresh()
            end
            if i > #SETS then
                info.hasArrow, info.value = true, set
            end
            UIDropDownMenu_AddButton(info, 1)
        end
    end)
    page.status = page.bar:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    page.status:SetPoint("RIGHT", -16, 0)

    -- Paper: the set's name and what it does, then the switches in three columns.
    page.heading = UI.Heading(page.content)
    page.heading:SetPoint("TOPLEFT", 10, -8)
    page.description = UI.Text(page.content, 620)
    page.description:SetPoint("TOPLEFT", page.heading, "BOTTOMLEFT", 0, -4)

    local columns = { { x = 10, y = -66 }, { x = 220, y = -66 }, { x = 430, y = -66 } }
    for _, group in ipairs(GROUPS) do
        local column = columns[group.column]
        local title = page.content:CreateFontString(nil, "OVERLAY", "GameFontNormal")
        title:SetPoint("TOPLEFT", column.x, column.y)
        title:SetText(group.title)
        column.y = column.y - 18
        for _, item in ipairs(group.items) do
            local id = item[1]
            local check = UI.Check(page.content, item[2], item[3])
            check:SetPoint("TOPLEFT", column.x - 4, column.y)
            check.some = check:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
            check.some:SetPoint("LEFT", check.label, "RIGHT", 4, 0)
            check.some:SetText("(some)")
            check:SetScript("OnClick", function()
                SetSwitch(id, State(id) ~= true)
                Refresh() -- the box shows the bots' state, not the click
            end)
            check:SetScript("OnEnter", function(c)
                GameTooltip:SetOwner(c, "ANCHOR_RIGHT")
                GameTooltip:AddLine(item[2], 1, 1, 1)
                GameTooltip:AddLine(item[3], nil, nil, nil, true)
                if State(id) == nil then
                    GameTooltip:AddLine("None of these bots' classes can do this.", 0.6, 0.6, 0.6, true)
                end
                GameTooltip:Show()
            end)
            check:SetScript("OnLeave", GameTooltip_Hide)
            checks[id] = check
            column.y = column.y - 34
        end
        column.y = column.y - 8
    end

    -- Bottom bar.
    local formation = UI.Button(page.footer, "Formation...", 120)
    formation:SetPoint("LEFT", 0, 0)
    formation:SetScript("OnClick", function() LA.Formation.Toggle() end)
    page.save = UI.Button(page.footer, "Save as a set...", 120)
    page.save:SetPoint("LEFT", formation, "RIGHT", 4, 0)
    page.save:SetScript("OnClick", function() StaticPopup_Show("LIVINGAZEROTH_SAVE_SET") end)
    page.undo = UI.Button(page.footer, "Undo changes", 120)
    page.undo:SetPoint("LEFT", page.save, "RIGHT", 4, 0)
    page.undo:SetScript("OnClick", function()
        Undo()
        Refresh()
    end)
    LA.Manager.CloseButton(page)
end

local function Show()
    Remember()
    Refresh()
end

LA.On("loaded", function()
    LivingAzerothDB.tactics = LivingAzerothDB.tactics or {}
    LivingAzerothDB.tactics.sets = LivingAzerothDB.tactics.sets or {}
    LA.Manager.AddPage("Tactics", Build, Show)
end)
LA.On("bots:changed", Refresh)
LA.On("orders:changed", Refresh)
