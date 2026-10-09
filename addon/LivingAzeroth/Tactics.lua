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
    { name = "Questing", icon = "Interface\\Icons\\INV_Misc_Map_01", text = "Keep up, help out, pick things up.",
        on = { "join", "aoe", "behind", "avoid_aoe", "potions", "run", "loot", "gather", "food", "buffs", "mount" } },
    { name = "Dungeon", icon = "Interface\\Icons\\INV_Shield_06", text = "Let the tank lead; mind the threat.",
        on = { "aoe", "behind", "threat", "avoid_aoe", "potions", "save_mana", "loot", "food", "buffs" } },
    { name = "Raid", icon = "Interface\\Icons\\INV_Misc_Head_Dragon_Black", text = "Threat and mana discipline.",
        on = { "behind", "threat", "avoid_aoe", "potions", "save_mana", "food", "buffs" } },
    { name = "Grinding", icon = "Interface\\Icons\\Ability_Warrior_Rampage",
        text = "Fight everything nearby, loot it all.",
        on = { "join", "aoe", "avoid_aoe", "potions", "save_mana", "loot", "gather", "food", "buffs" } },
    { name = "Quietly", icon = "Interface\\Icons\\Spell_Nature_Sleep", text = "Follow and stay out of fights.",
        on = { "passive", "avoid_aoe", "potions", "run", "food", "mount" } },
    { name = "Guard me", icon = "Interface\\Icons\\Ability_Defend", text = "Defend me; fight anything that comes.",
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

local function ShowList()
    local bots = AllBots()
    if target ~= "all" and not LA.Bots.Mine()[target] then
        target = "all"
    end
    local items = { { header = true, key = "who", text = "Apply to" } }
    items[#items + 1] = { key = "all", under = "who", text = "All my bots", tag = #bots > 0 and tostring(#bots) or "",
        icon = "Interface\\Icons\\INV_Misc_Gear_01" }
    for _, bot in ipairs(bots) do
        items[#items + 1] = { key = bot.guid, under = "who", text = LA.UI.Coloured(bot.name, bot.class),
            tag = LA.Orders.Pending(bot.guid) and "..." or "",
            icon = function(texture) LA.UI.SetClassIcon(texture, bot.class) end }
    end
    items[#items + 1] = { header = true, key = "sets", text = "Tactic sets" }
    local current = #bots > 0 and Matching()
    for i, set in ipairs(Sets()) do
        items[#items + 1] = { key = "set:" .. i, set = set, own = i > #SETS, under = "sets", text = set.name,
            tag = current == set and "In use" or "", icon = set.icon or "Interface\\Icons\\INV_Misc_Note_01",
            tooltip = { set.name, set.text or "Your own set.",
                i > #SETS and "|cff999999Click to use it; right-click to forget it.|r" or
                    "|cff999999Click to use it.|r" } }
    end
    page.list:SetItems(items)
    page.list:Choose(target)
end

local function Refresh()
    if not page or not page.left:IsVisible() then
        return
    end
    ShowList()
    local bots = Targets()
    local d, stack = page.parts, page.stack
    stack:Reset()
    for _, region in pairs(d) do
        region:Hide()
    end
    for _, check in pairs(checks) do
        check:Hide()
    end

    if #AllBots() == 0 then
        d.heading:SetText("No bots with you")
        stack:Add(d.heading)
        d.description:SetText("Bring one of your characters in from the Roster tab, or invite a bot to your " ..
            "party. Their tactics are set here.")
        stack:Add(d.description, 6)
        page:SetDetailHeight(stack:Height())
        page.save:Disable()
        page.undo:Disable()
        page.formation:Disable()
        page.who:SetText("")
        return
    end

    local set = Matching()
    d.heading:SetText(set and set.name or "Your own mix")
    stack:Add(d.heading)
    d.description:SetText(set and (set.text or "Your own set.") or
        "These switches match none of the sets. Save them as a set to use them again.")
    stack:Add(d.description, 4)
    for _, group in ipairs(GROUPS) do
        local title = d["group:" .. group.title]
        stack:Add(title, 12)
        for _, item in ipairs(group.items) do
            local id = item[1]
            local check = checks[id]
            local state = State(id)
            check:SetChecked(state == true or state == "some")
            check:SetUsable(state ~= nil)
            LA.SetShown(check.some, state == "some")
            stack:Add(check, 2, -4, 32)
        end
    end
    page:SetDetailHeight(stack:Height())

    local names = target == "all" and "all your bots" or bots[1] and bots[1].name or "?"
    local waiting = Waiting()
    page.who:SetText("For " .. names .. (waiting > 0 and (" - waiting for " .. waiting .. "...") or ""))
    page.save:Enable()
    page.formation:Enable()
    if Changed() then
        page.undo:Enable()
    else
        page.undo:Disable()
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
    local UI = LA.UI

    page.who = page.top:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    page.who:SetPoint("LEFT", 4, 0)

    page.list = UI.List(page.left, function(item, mouse)
        if item.set then
            if mouse == "RightButton" and item.own then
                StaticPopup_Show("LIVINGAZEROTH_FORGET_SET", item.set.name, nil, item.set)
            elseif #AllBots() > 0 then
                UseSet(item.set)
            end
        else
            target = item.key
            Remember()
            LA.Manager.ScrollToTop()
        end
        Refresh()
    end)

    local d = {}
    d.heading = UI.Paper(page.detail, "title", 280)
    d.description = UI.Paper(page.detail, "body", 280)
    for _, group in ipairs(GROUPS) do
        local title = UI.Paper(page.detail, "title", 280)
        title:SetText(group.title)
        d["group:" .. group.title] = title
        for _, item in ipairs(group.items) do
            local id = item[1]
            local check = UI.Check(page.detail, item[2], item[3])
            check.some = check:CreateFontString(nil, "OVERLAY", "QuestFontNormalSmall")
            check.some:SetPoint("LEFT", check.label, "RIGHT", 4, 0)
            check.some:SetText("(some)")
            check:SetScript("OnClick", function()
                SetSwitch(id, State(id) ~= true)
                Refresh() -- the box shows the bots' state, not the click
            end)
            check:SetScript("OnEnter", function(c)
                if State(id) == nil then
                    GameTooltip:SetOwner(c, "ANCHOR_RIGHT")
                    GameTooltip:AddLine(item[2], 1, 1, 1)
                    GameTooltip:AddLine("None of these bots' classes can do this.", nil, nil, nil, true)
                    GameTooltip:Show()
                end
            end)
            check:SetScript("OnLeave", GameTooltip_Hide)
            checks[id] = check
        end
    end
    page.parts = d
    page.stack = UI.Stack(page.detail)

    page.save = UI.Button(page.controls, "Save set", 100)
    page.save:SetPoint("LEFT", 0, 1)
    page.save:SetScript("OnClick", function() StaticPopup_Show("LIVINGAZEROTH_SAVE_SET") end)
    page.undo = UI.Button(page.controls, "Undo", 100)
    page.undo:SetPoint("LEFT", page.save, "RIGHT", 0, 0)
    page.undo:SetScript("OnClick", function()
        Undo()
        Refresh()
    end)
    page.formation = UI.Button(page.controls, "Formation", 100)
    page.formation:SetPoint("RIGHT", -3, 1)
    page.formation:SetScript("OnClick", function() LA.Formation.Toggle() end)
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
