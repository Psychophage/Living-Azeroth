-- SPDX-License-Identifier: GPL-2.0-or-later
-- Raid frames: eight groups of compact cells (class-coloured health, power, role, a bot medal), built on the game's
-- secure raid headers so that clicking a cell targets its player in combat too. Each bot the player commands has
-- an order icon in its cell; a group's name or "All my bots" opens the orders ring for several bots, and
-- shift-clicking order icons picks a selection. Someone else's bots show a grey medal and no order icon.
local _, LA = ...

local Raid = {}
LA.Raid = Raid

local WIDTH, HEIGHT, GAP = 112, 40, 4
local GROUPS = 8
local BAR = "Interface\\TargetingFrame\\UI-StatusBar"
local ROLES = "Interface\\LFGFrame\\UI-LFG-ICON-PORTRAITROLES"

local cells = {} -- every cell the headers made
local selected = {} -- guid -> true

-- Placement: the raid hangs from an anchor the player can drag (out of combat) while frames are unlocked.
local anchor = CreateFrame("Frame", "LivingAzerothRaidAnchor", UIParent)
anchor:SetSize(1, 1)
anchor:SetMovable(true)
anchor:SetClampedToScreen(true)

local raid = CreateFrame("Frame", "LivingAzerothRaid", UIParent)
raid:SetSize(GROUPS * (WIDTH + GAP) + 8, 5 * (HEIGHT + GAP) + 44)
raid:SetPoint("TOPLEFT", anchor, "TOPLEFT")

local title = raid:CreateFontString(nil, "OVERLAY", "GameFontNormal")
title:SetPoint("TOPLEFT", 8, -10)

local function SmallButton(text, width)
    local button = CreateFrame("Button", nil, raid, "UIPanelButtonTemplate")
    button:SetSize(width, 20)
    button:SetText(text)
    button:GetFontString():SetFontObject("GameFontNormalSmall")
    return button
end

local allButton = SmallButton("All my bots", 96)
allButton:SetPoint("LEFT", title, "RIGHT", 10, 0)
allButton.livingAzerothPip = true
local selectionButton = SmallButton("", 150)
selectionButton:SetPoint("LEFT", allButton, "RIGHT", 6, 0)
selectionButton.livingAzerothPip = true
local clearButton = SmallButton("Clear", 50)
clearButton:SetPoint("LEFT", selectionButton, "RIGHT", 4, 0)

-- Cells

local dropdown = CreateFrame("Frame", "LivingAzerothRaidDropDown", UIParent, "UIDropDownMenuTemplate")
UIDropDownMenu_Initialize(dropdown, function(self)
    if self.unit then
        UnitPopup_ShowMenu(self, UnitIsUnit(self.unit, "player") and "SELF" or "RAID_PLAYER", self.unit, self.name,
            self.id)
    end
end, "MENU")

local function ShowMenu(cell)
    local unit = cell:GetAttribute("unit")
    if not unit then
        return
    end
    dropdown.unit, dropdown.name = unit, UnitName(unit)
    dropdown.id = tonumber(unit:match("^raid(%d+)$"))
    ToggleDropDownMenu(1, nil, dropdown, "cursor")
end

local function Bar(parent, height)
    local bar = CreateFrame("StatusBar", nil, parent)
    bar:SetStatusBarTexture(BAR)
    bar:SetMinMaxValues(0, 1)
    bar.background = bar:CreateTexture(nil, "BACKGROUND")
    bar.background:SetAllPoints()
    bar.background:SetTexture(BAR)
    bar.background:SetVertexColor(0.12, 0.12, 0.12, 0.9)
    bar:SetHeight(height)
    return bar
end

local function OpenFor(target, anchorFrame)
    if #target.guids > 0 then
        LA.Ring.Toggle(target, anchorFrame)
    else
        UIErrorsFrame:AddMessage("None of your bots there.", 1, 0.25, 0.2)
    end
end

function Raid.SetUpCell(cell)
    cell:RegisterForClicks("AnyUp")
    cell.menu = ShowMenu
    cell:SetBackdrop({ bgFile = "Interface\\Buttons\\WHITE8X8", edgeFile = "Interface\\Buttons\\WHITE8X8",
        edgeSize = 1 })
    cell:SetBackdropColor(0, 0, 0, 0.85)
    cell:SetBackdropBorderColor(0, 0, 0, 1)
    cell.health = Bar(cell, HEIGHT - 7)
    cell.health:SetPoint("TOPLEFT", 1, -1)
    cell.health:SetPoint("TOPRIGHT", -1, -1)
    cell.power = Bar(cell, 4)
    cell.power:SetPoint("BOTTOMLEFT", 1, 1)
    cell.power:SetPoint("BOTTOMRIGHT", -1, 1)
    local text = CreateFrame("Frame", nil, cell)
    text:SetAllPoints()
    text:SetFrameLevel(cell.health:GetFrameLevel() + 2)
    cell.name = text:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    cell.name:SetPoint("TOPLEFT", 18, -5)
    cell.name:SetPoint("TOPRIGHT", -18, -5)
    cell.name:SetJustifyH("LEFT")
    cell.sub = text:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    cell.sub:SetPoint("BOTTOMLEFT", 5, 8)
    cell.sub:SetTextColor(0.85, 0.82, 0.75)
    cell.role = text:CreateTexture(nil, "OVERLAY")
    cell.role:SetTexture(ROLES)
    cell.role:SetSize(14, 14)
    cell.role:SetPoint("TOPLEFT", 3, -4)
    cell.medal = LA.Markers.Medal(text, 13)
    cell.medal:SetPoint("BOTTOMRIGHT", -3, 6)
    cell.pip = CreateFrame("Button", nil, text)
    cell.pip.livingAzerothPip = true
    cell.pip:SetSize(15, 15)
    cell.pip:SetPoint("TOPRIGHT", -3, -3)
    cell.pip.icon = cell.pip:CreateTexture(nil, "ARTWORK")
    cell.pip.icon:SetAllPoints()
    cell.pip.icon:SetTexCoord(0.08, 0.92, 0.08, 0.92)
    cell.pip.glow = cell.pip:CreateTexture(nil, "OVERLAY")
    cell.pip.glow:SetTexture("Interface\\Buttons\\UI-ActionButton-Border")
    cell.pip.glow:SetBlendMode("ADD")
    cell.pip.glow:SetPoint("CENTER")
    cell.pip.glow:SetSize(30, 30)
    cell.pip.glow:SetVertexColor(1, 0.82, 0)
    cell.pip:SetHighlightTexture("Interface\\Buttons\\ButtonHilight-Square", "ADD")
    cell.flags = LA.Markers.Flags(text, 11)
    cell.flags:SetPoint("RIGHT", cell.pip, "LEFT", -2, 0)
    cell.pip.tick = cell.pip:CreateTexture(nil, "OVERLAY")
    cell.pip.tick:SetTexture("Interface\\RaidFrame\\ReadyCheck-Ready")
    cell.pip.tick:SetSize(12, 12)
    cell.pip.tick:SetPoint("CENTER", cell.pip, "LEFT", -5, 0)
    cell.pip:RegisterForClicks("AnyUp")
    cell.pip:SetScript("OnClick", function(self)
        local guid = cell.guid
        if not guid then
            return
        end
        if IsShiftKeyDown() then
            selected[guid] = not selected[guid] or nil
            Raid.Update()
            return
        end
        LA.Ring.Toggle({ key = "bot:" .. guid, guids = { guid } }, self)
    end)
    cell.pip:SetScript("OnEnter", function(self)
        local bot = cell.guid and LA.Bots.Mine()[cell.guid]
        if bot then
            GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
            GameTooltip:AddLine(bot.name, 1, 0.82, 0)
            GameTooltip:AddLine(LA.Orders.Standing(bot).state, 1, 1, 1)
            GameTooltip:AddLine("Click for orders, shift-click to pick several", 0.6, 0.6, 0.6)
            GameTooltip:Show()
        end
    end)
    cell.pip:SetScript("OnLeave", function()
        GameTooltip:Hide()
    end)
    cell:HookScript("OnEnter", function(self)
        local unit = self:GetAttribute("unit")
        if unit then
            GameTooltip_SetDefaultAnchor(GameTooltip, self)
            GameTooltip:SetUnit(unit)
        end
    end)
    cell:HookScript("OnLeave", function()
        GameTooltip:Hide()
    end)
    cells[#cells + 1] = cell
end

local function RoleOf(unit, bot)
    local role = bot and bot.role
    if not role then
        local tank, healer = UnitGroupRolesAssigned(unit)
        role = tank and "tank" or healer and "healer" or nil
    end
    return role == "tank" and "TANK" or role == "healer" and "HEALER" or role == "damage" and "DAMAGER" or nil
end

local function UpdateCell(cell)
    local unit = cell:GetAttribute("unit")
    if not unit or not UnitExists(unit) then
        cell.guid = nil
        return
    end
    local _, class = UnitClass(unit)
    local color = class and RAID_CLASS_COLORS[class] or NORMAL_FONT_COLOR
    local max = UnitHealthMax(unit)
    local health = UnitHealth(unit)
    cell.health:SetValue(max > 0 and health / max or 0)
    cell.health:SetStatusBarColor(color.r, color.g, color.b)
    local powerMax = UnitPowerMax(unit)
    cell.power:SetValue(powerMax > 0 and UnitPower(unit) / powerMax or 0)
    local powerColor = PowerBarColor[UnitPowerType(unit)] or PowerBarColor.MANA
    cell.power:SetStatusBarColor(powerColor.r, powerColor.g, powerColor.b)
    cell.name:SetText(UnitName(unit))
    if not UnitIsConnected(unit) then
        cell.sub:SetText("Offline")
        cell.health:SetStatusBarColor(0.4, 0.4, 0.4)
    elseif UnitIsDeadOrGhost(unit) then
        cell.sub:SetText("Dead")
    elseif health < max then
        local missing = max - health
        cell.sub:SetText("-" .. (missing >= 1000 and string.format("%.1fk", missing / 1000) or missing))
    else
        cell.sub:SetText("")
    end
    if UnitIsUnit(unit, "target") then
        cell:SetBackdropBorderColor(1, 0.82, 0, 1)
    else
        cell:SetBackdropBorderColor(0, 0, 0, 1)
    end

    local guid = LA.Bots.Guid(unit)
    local bot = LA.Bots.ForUnit(unit)
    local mine = guid and LA.Bots.Mine()[guid]
    local role = RoleOf(unit, mine)
    if role then
        cell.role:SetTexCoord(GetTexCoordsForRoleSmallCircle(role))
        cell.role:Show()
    else
        cell.role:Hide()
    end
    if bot then
        cell.medal:SetCommandable(bot.commandable)
        cell.medal:Show()
    else
        cell.medal:Hide()
    end
    if mine and mine.commandable then
        cell.guid = guid
        cell.pip.icon:SetTexture(LA.Orders.Standing(mine).icon)
        cell.pip:Show()
        cell.flags:Update(mine)
        cell.flags:Show()
        local marked = mine.switches and (mine.switches.passive == true or mine.switches.loot == false)
        cell.name:SetPoint("TOPRIGHT", marked and -32 or -18, -5)
        local pending = LA.Orders.Pending(guid)
        if pending then
            cell.pip.glow:SetAlpha(0.45 + 0.45 * math.sin(GetTime() * 9))
        end
        LA.SetShown(cell.pip.glow, pending or selected[guid] or LA.Ring.OpenFor() == "bot:" .. guid)
        LA.SetShown(cell.pip.tick, LA.Orders.RecentlyDone(guid))
    else
        cell.guid = nil
        selected[guid or 0] = nil
        cell.pip:Hide()
        cell.flags:Hide()
        cell.name:SetPoint("TOPRIGHT", -18, -5)
    end
end

-- The commandable bots in the raid, all of them or one group's.
local function BotsIn(group)
    local guids = {}
    for i = 1, GetNumRaidMembers() do
        local _, _, subgroup = GetRaidRosterInfo(i)
        local guid = LA.Bots.Guid("raid" .. i)
        local bot = guid and LA.Bots.Mine()[guid]
        if bot and bot.commandable and (not group or subgroup == group) then
            guids[#guids + 1] = guid
        end
    end
    return guids
end

local headers, labels = {}, {}
for group = 1, GROUPS do
    local header = CreateFrame("Frame", "LivingAzerothRaidGroup" .. group, raid, "SecureRaidGroupHeaderTemplate")
    header:SetAttribute("template", "LivingAzerothRaidCellTemplate")
    header:SetAttribute("groupFilter", tostring(group))
    header:SetAttribute("showRaid", true)
    header:SetAttribute("point", "TOP")
    header:SetAttribute("yOffset", -GAP)
    header:SetPoint("TOPLEFT", raid, "TOPLEFT", 6 + (group - 1) * (WIDTH + GAP), -48)
    headers[group] = header

    local label = CreateFrame("Button", nil, raid)
    label.livingAzerothPip = true
    label:SetSize(WIDTH, 14)
    label:SetPoint("BOTTOMLEFT", header, "TOPLEFT", 0, 2)
    label.text = label:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    label.text:SetPoint("LEFT", 2, 0)
    label.text:SetText("Group " .. group)
    label:SetHighlightTexture("Interface\\QuestFrame\\UI-QuestTitleHighlight", "ADD")
    label:RegisterForClicks("AnyUp")
    label:SetScript("OnClick", function(self)
        OpenFor({ key = "group:" .. group, label = "Group " .. group, guids = BotsIn(group) }, self)
    end)
    labels[group] = label
end

allButton:SetScript("OnClick", function(self)
    OpenFor({ key = "all", label = "All my bots", guids = BotsIn(nil) }, self)
end)

local function Selection()
    local guids = {}
    for guid in pairs(selected) do
        guids[#guids + 1] = guid
    end
    table.sort(guids)
    return guids
end

selectionButton:SetScript("OnClick", function(self)
    local guids = Selection()
    OpenFor({ key = "selection:" .. table.concat(guids, ","), label = #guids .. " selected", guids = guids }, self)
end)
clearButton:SetScript("OnClick", function()
    wipe(selected)
    Raid.Update()
end)

function Raid.Update()
    if not raid:IsShown() then
        return
    end
    title:SetText("Raid - " .. GetNumRaidMembers())
    local groups = {}
    for i = 1, GetNumRaidMembers() do
        local _, _, subgroup = GetRaidRosterInfo(i)
        groups[subgroup or 0] = true
    end
    for group = 1, GROUPS do
        LA.SetShown(labels[group], groups[group])
    end
    for _, cell in ipairs(cells) do
        if cell:IsVisible() then
            UpdateCell(cell)
        end
    end
    local count = #Selection()
    LA.SetShown(selectionButton, count > 0)
    LA.SetShown(clearButton, count > 0)
    selectionButton:SetText("Orders for " .. count .. " selected")
    LA.SetShown(allButton, #BotsIn(nil) > 0)
end

local elapsed = 0
raid:SetScript("OnUpdate", function(_, delta)
    elapsed = elapsed + delta
    if elapsed >= 0.1 then
        elapsed = 0
        Raid.Update()
    end
end)

-- Shown in a raid only; the state driver shows and hides it even in combat.
RegisterStateDriver(raid, "visibility", "[group:raid] show; hide")

-- The CompactRaidFrame addon draws its own raid frames; with ours in use, its are hidden (a setting keeps them).
local function HideCompactRaidFrames()
    if not LivingAzerothDB.raid.hideCompact or InCombatLockdown() then
        return
    end
    for _, name in ipairs({ "CompactRaidFrameManager", "CompactRaidFrameContainer" }) do
        local frame = _G[name]
        if frame and not frame.livingAzerothHidden then
            frame.livingAzerothHidden = true
            frame:Hide()
            hooksecurefunc(frame, "Show", function(self)
                if LivingAzerothDB.raid.hideCompact and not InCombatLockdown() then
                    self:Hide()
                end
            end)
        end
    end
end

local function Place()
    local saved = LivingAzerothDB.raid
    anchor:ClearAllPoints()
    anchor:SetPoint("TOPLEFT", UIParent, "BOTTOMLEFT", saved.x, saved.y)
end

-- Moving (out of combat): a bar above the raid frames while frames are unlocked.
local mover = CreateFrame("Frame", nil, UIParent)
mover:SetFrameStrata("DIALOG")
mover:SetSize(200, 20)
mover:SetPoint("BOTTOMLEFT", anchor, "TOPLEFT", 0, 4)
mover:SetBackdrop({ bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
    edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border", edgeSize = 10,
    insets = { left = 2, right = 2, top = 2, bottom = 2 } })
mover:SetBackdropColor(0.16, 0.12, 0.04, 0.92)
mover:SetBackdropBorderColor(0.79, 0.64, 0.29)
local moverText = mover:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
moverText:SetPoint("CENTER")
moverText:SetText("Raid frames - drag to move")
mover:EnableMouse(true)
mover:RegisterForDrag("LeftButton")
mover:SetScript("OnDragStart", function()
    if not InCombatLockdown() then
        anchor:StartMoving()
    end
end)
mover:SetScript("OnDragStop", function()
    anchor:StopMovingOrSizing()
    LivingAzerothDB.raid.x, LivingAzerothDB.raid.y = anchor:GetLeft(), anchor:GetTop()
end)
mover:Hide()

function Raid.Unlock(unlocked)
    if unlocked and InCombatLockdown() then
        return
    end
    LA.SetShown(mover, unlocked)
end

LA.On("loaded", function()
    local saved = LivingAzerothDB.raid or {}
    if saved.hideCompact == nil then
        saved.hideCompact = true
    end
    saved.x = saved.x or 20
    saved.y = saved.y or UIParent:GetHeight() - 180
    LivingAzerothDB.raid = saved
    Place()
    for _, header in ipairs(headers) do
        header:Show() -- only once the cell template exists
    end
end)

local events = CreateFrame("Frame")
events:RegisterEvent("PLAYER_ENTERING_WORLD")
events:RegisterEvent("RAID_ROSTER_UPDATE")
events:RegisterEvent("PLAYER_REGEN_ENABLED")
events:SetScript("OnEvent", HideCompactRaidFrames)
