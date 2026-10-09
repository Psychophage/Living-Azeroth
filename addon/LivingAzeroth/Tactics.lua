-- SPDX-License-Identifier: GPL-2.0-or-later
-- The Tactics tab: the tactic switches of all the player's bots or one of them, tactic sets that set many
-- switches at once (the player can save their own), and the formation. Every box shows the bots' real state:
-- a change shows once the server says the bots have made it.
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
    { title = "Staying alive", column = 1, items = {
        { "avoid_aoe", "Avoid area damage", "Step out of fire and void zones" },
        { "potions", "Use potions", "Healing and mana potions when low" },
        { "run", "Run when outmatched", "Fall back instead of dying" },
    } },
    { title = "Healing", column = 2, items = {
        { "save_mana", "Save mana", "Skip small heals; keep a reserve" },
    } },
    { title = "Out of combat", column = 2, items = {
        { "loot", "Pick up loot", "After fights" },
        { "gather", "Gather herbs and ore", "When their professions allow" },
        { "food", "Eat and drink", "Rest after fights" },
        { "mount", "Mount when I mount", "Ride with me" },
    } },
}

local SETS = {
    { name = "Questing", text = "Keep up, help out, pick things up.",
        on = { "join", "aoe", "behind", "avoid_aoe", "potions", "run", "loot", "gather", "food", "mount" } },
    { name = "Dungeon", text = "Let the tank lead; mind the threat.",
        on = { "aoe", "behind", "threat", "avoid_aoe", "potions", "save_mana", "loot", "food" } },
    { name = "Raid", text = "Threat and mana discipline.",
        on = { "behind", "threat", "avoid_aoe", "potions", "save_mana", "food" } },
    { name = "Grinding", text = "Fight everything nearby, loot it all.",
        on = { "join", "aoe", "avoid_aoe", "potions", "save_mana", "loot", "gather", "food" } },
    { name = "Quietly", text = "Follow and stay out of fights.",
        on = { "passive", "avoid_aoe", "potions", "run", "food", "mount" } },
    { name = "Guard me", text = "Defend me; fight anything that comes.",
        on = { "join", "aoe", "avoid_aoe", "potions", "food" } },
}

local FORMATIONS = {
    { id = "near", name = "Near", text = "Close around you at follow distance." },
    { id = "far", name = "Far", text = "Hang back at a distance and keep out of the way.",
        pos = { { 0, 86 }, { -44, 118 }, { 44, 118 }, { -90, 100 }, { 90, 100 } } },
    { id = "arrow", name = "Arrow", text = "Tanks in front, damage in the middle, healers behind.",
        pos = { { 0, -96 }, { 0, 58 }, { -50, -50 }, { 50, -50 }, { 0, -54 } } },
    { id = "queue", name = "Queue", text = "Single file behind you.",
        pos = { { 0, 30 }, { 0, 58 }, { 0, 86 }, { 0, 114 }, { 0, 142 } } },
    { id = "circle", name = "Circle", text = "A ring around your target, or around you when there is none." },
    { id = "line", name = "Line", text = "Side by side, with you in the middle.",
        pos = { { -116, 0 }, { -58, 0 }, { 58, 0 }, { 116, 0 }, { 150, 0 } } },
    { id = "shield", name = "Shield", text = "An arc in front of you." },
    { id = "melee", name = "Melee", text = "Stay tight on the group leader for melee fights." },
    { id = "chaos", name = "Chaos", text = "Loosely scattered around you.",
        pos = { { -104, -66 }, { 84, -82 }, { 126, 40 }, { -90, 78 }, { 28, 116 } } },
}

-- Five places on a circle or an arc around the player (x right, y towards the back), as in the mockups.
local function Arc(radius, start, span)
    local places = {}
    for i = 0, 4 do
        local angle = math.rad(start + span * i / (span >= 360 and 5 or 4))
        places[#places + 1] = { radius * math.cos(angle), radius * math.sin(angle) }
    end
    return places
end
FORMATIONS[1].pos = Arc(48, -90, 360)
FORMATIONS[5].pos = Arc(84, -90, 360)
FORMATIONS[7].pos = Arc(84, -160, 140)
FORMATIONS[8].pos = Arc(28, -90, 360)

local page
local target = "all" -- "all" or a bot's guid
local checks = {}
local setButtons = {}
local formationButtons = {}

-- The bots the tab is changing.
local function Targets()
    local bots = {}
    for guid, bot in pairs(LA.Bots.Mine()) do
        if bot.commandable and (target == "all" or target == guid) then
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

-- Turns a switch on or off for every target bot whose class has it and that is not already that way.
local function SetSwitch(id, on)
    for _, bot in ipairs(Targets()) do
        local now = bot.switches and bot.switches[id]
        if now ~= nil and now ~= on and not LA.Orders.IsPending(bot.guid, id) then
            LA.Orders.Give(bot, id, on)
        end
    end
end

local function UseSet(set)
    local wanted = {}
    for _, id in ipairs(set.on) do
        wanted[id] = true
    end
    for _, group in ipairs(GROUPS) do
        for _, item in ipairs(group.items) do
            SetSwitch(item[1], wanted[item[1]] == true)
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

local function Waiting()
    local count = 0
    for _, bot in ipairs(Targets()) do
        if LA.Orders.Pending(bot.guid) then
            count = count + 1
        end
    end
    return count
end

local function Refresh()
    if not page or not page:IsVisible() then
        return
    end
    local bots = Targets()
    UIDropDownMenu_SetText(page.target, target == "all" and "All my bots" or (bots[1] and bots[1].name or "?"))
    for id, check in pairs(checks) do
        local state = State(id)
        check:SetChecked(state == true or state == "some")
        if state == nil then
            check:Disable()
            check.label:SetFontObject("GameFontDisable")
        else
            check:Enable()
            check.label:SetFontObject(state == "some" and "GameFontNormal" or "GameFontHighlight")
        end
        LA.SetShown(check.some, state == "some")
    end
    for _, button in ipairs(setButtons) do
        local set = button.set
        local matches = set ~= nil and #bots > 0
        if set then
            local wanted = {}
            for _, id in ipairs(set.on) do
                wanted[id] = true
            end
            for id in pairs(checks) do
                local state = State(id)
                if state ~= nil and state ~= (wanted[id] == true) then
                    matches = false
                end
            end
        end
        if matches then
            button:LockHighlight()
        else
            button:UnlockHighlight()
        end
    end
    local formation = bots[1] and bots[1].formation
    for _, bot in ipairs(bots) do
        if bot.formation ~= formation then
            formation = nil
        end
    end
    for _, button in ipairs(formationButtons) do
        LA.SetShown(button.chosen, button.formation.id == formation)
    end
    local waiting = Waiting()
    if #bots == 0 then
        page.status:SetText("You have no bots here to command.")
    elseif waiting > 0 then
        page.status:SetText("Waiting for " .. waiting .. (waiting == 1 and " bot..." or " bots..."))
    else
        page.status:SetText("")
    end
end

local function LayoutSets()
    local sets = Sets()
    for i, set in ipairs(sets) do
        local button = setButtons[i]
        if not button then
            button = LA.Manager.Button(page, "", 180)
            button:SetPoint("TOPLEFT", 0, -62 - (i - 1) * 26)
            button:RegisterForClicks("LeftButtonUp", "RightButtonUp")
            button:SetScript("OnClick", function(self, mouse)
                if mouse == "RightButton" and self.custom then
                    StaticPopup_Show("LIVINGAZEROTH_FORGET_SET", self.set.name, nil, self.set)
                    return
                end
                UseSet(self.set)
                Refresh()
            end)
            button:SetScript("OnEnter", function(self)
                GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
                GameTooltip:AddLine(self.set.name, 1, 0.82, 0)
                GameTooltip:AddLine(self.set.text or "Your own set.", 1, 1, 1, true)
                GameTooltip:AddLine(self.custom and "Click to use; right-click to forget." or "Click to use.",
                    0.6, 0.6, 0.6)
                GameTooltip:Show()
            end)
            button:SetScript("OnLeave", function()
                GameTooltip:Hide()
            end)
            setButtons[i] = button
        end
        button.set = set
        button.custom = i > #SETS
        button:SetText(set.name)
        button:Show()
    end
    for i = #sets + 1, #setButtons do
        setButtons[i].set = nil
        setButtons[i]:Hide()
    end
    page.save:ClearAllPoints()
    page.save:SetPoint("TOPLEFT", 0, -62 - #sets * 26 - 8)
end

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
        LayoutSets()
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
        LayoutSets()
        Refresh()
    end,
    timeout = 0,
    whileDead = true,
    hideOnEscape = true,
}

local function FormationButton(parent, formation)
    local button = CreateFrame("Button", nil, parent)
    button:SetSize(54, 54)
    local background = button:CreateTexture(nil, "BACKGROUND")
    background:SetAllPoints()
    background:SetTexture("Interface\\Buttons\\WHITE8X8")
    background:SetVertexColor(0.08, 0.07, 0.05, 0.9)
    local edge = button:CreateTexture(nil, "BORDER")
    edge:SetPoint("TOPLEFT", -1, 1)
    edge:SetPoint("BOTTOMRIGHT", 1, -1)
    edge:SetTexture("Interface\\Buttons\\WHITE8X8")
    edge:SetVertexColor(0.35, 0.29, 0.17)
    local scale = 22 / 150
    local you = button:CreateTexture(nil, "ARTWORK")
    you:SetTexture("Interface\\CharacterFrame\\TempPortraitAlphaMask")
    you:SetVertexColor(1, 0.82, 0)
    you:SetSize(8, 8)
    you:SetPoint("CENTER")
    for _, place in ipairs(formation.pos) do
        local dot = button:CreateTexture(nil, "ARTWORK")
        dot:SetTexture("Interface\\CharacterFrame\\TempPortraitAlphaMask")
        dot:SetVertexColor(0.85, 0.85, 0.85)
        dot:SetSize(6, 6)
        dot:SetPoint("CENTER", place[1] * scale, -place[2] * scale)
    end
    button.chosen = button:CreateTexture(nil, "OVERLAY")
    button.chosen:SetTexture("Interface\\Buttons\\CheckButtonHilight")
    button.chosen:SetBlendMode("ADD")
    button.chosen:SetAllPoints()
    button:SetHighlightTexture("Interface\\Buttons\\ButtonHilight-Square", "ADD")
    local label = button:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    label:SetPoint("TOP", button, "BOTTOM", 0, -3)
    label:SetText(formation.name)
    button.formation = formation
    button:SetScript("OnClick", function()
        for _, bot in ipairs(Targets()) do
            if bot.formation ~= formation.id then
                LA.Orders.Give(bot, "formation", nil, { formation = formation.id })
            end
        end
        Refresh()
    end)
    button:SetScript("OnEnter", function(self)
        GameTooltip:SetOwner(self, "ANCHOR_TOP")
        GameTooltip:AddLine(formation.name, 1, 0.82, 0)
        GameTooltip:AddLine(formation.text, 1, 1, 1, true)
        GameTooltip:AddLine("You are the gold dot, facing up.", 0.6, 0.6, 0.6)
        GameTooltip:Show()
    end)
    button:SetScript("OnLeave", function()
        GameTooltip:Hide()
    end)
    return button
end

local function Build(self)
    page = self
    local label = page:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    label:SetPoint("TOPLEFT", 0, -6)
    label:SetText("Apply to")
    page.target = CreateFrame("Frame", "LivingAzerothTacticsTarget", page, "UIDropDownMenuTemplate")
    page.target:SetPoint("LEFT", label, "RIGHT", -6, -2)
    UIDropDownMenu_SetWidth(page.target, 150)
    UIDropDownMenu_Initialize(page.target, function()
        local function Add(text, value)
            local info = UIDropDownMenu_CreateInfo()
            info.text, info.checked = text, target == value
            info.func = function()
                target = value
                Refresh()
            end
            UIDropDownMenu_AddButton(info)
        end
        Add("All my bots", "all")
        local bots = {}
        for guid, bot in pairs(LA.Bots.Mine()) do
            if bot.commandable then
                bots[#bots + 1] = bot
            end
        end
        table.sort(bots, function(a, b) return a.name < b.name end)
        for _, bot in ipairs(bots) do
            Add(bot.name, bot.guid)
        end
    end)
    page.status = page:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    page.status:SetPoint("LEFT", page.target, "RIGHT", 4, 2)

    LA.Manager.Heading(page, "Tactic sets", 0, -42)
    page.save = LA.Manager.Button(page, "Save as a set...", 180)
    page.save:SetScript("OnClick", function()
        StaticPopup_Show("LIVINGAZEROTH_SAVE_SET")
    end)
    LayoutSets()

    local columns = { { x = 210, y = -42 }, { x = 450, y = -42 } }
    for _, group in ipairs(GROUPS) do
        local column = columns[group.column]
        LA.Manager.Heading(page, group.title, column.x, column.y)
        column.y = column.y - 20
        for _, item in ipairs(group.items) do
            local id = item[1]
            local check = LA.Manager.Check(page, item[2], item[3])
            check:SetPoint("TOPLEFT", column.x, column.y)
            check.some = check:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
            check.some:SetPoint("LEFT", check.label, "RIGHT", 4, 0)
            check.some:SetText("(some)")
            check:SetScript("OnClick", function(self)
                SetSwitch(id, State(id) ~= true)
                Refresh() -- the box shows the bots' state, not the click
            end)
            checks[id] = check
            column.y = column.y - 32
        end
        column.y = column.y - 6
    end

    LA.Manager.Heading(page, "Formation", 0, -352)
    for i, formation in ipairs(FORMATIONS) do
        local button = FormationButton(page, formation)
        button:SetPoint("TOPLEFT", (i - 1) * 74, -374)
        formationButtons[i] = button
    end
end

LA.On("loaded", function()
    LivingAzerothDB.tactics = LivingAzerothDB.tactics or {}
    LivingAzerothDB.tactics.sets = LivingAzerothDB.tactics.sets or {}
    LA.Manager.AddPage("Tactics", Build, Refresh)
end)
LA.On("bots:changed", Refresh)
LA.On("orders:changed", Refresh)
