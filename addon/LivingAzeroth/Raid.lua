-- SPDX-License-Identifier: GPL-2.0-or-later
-- Raid frames: up to eight groups of compact cells (class-coloured health, a thin power bar, tank and healer
-- marks), built on the game's secure raid headers so that clicking a cell targets its player in combat too. Each
-- bot the player commands has a round order mark in its cell that opens the orders ring; someone else's bots show
-- a small grey bot mark. A group's name or "All my bots" opens the ring for several bots, and shift-clicking order
-- marks picks a selection. The cells' size, the groups' layout and the power bars are the player's choice (Settings).
local _, LA = ...

local Raid = {}
LA.Raid = Raid

local UI, Style, Draw = LA.UI, LA.Style, LA.Draw

local GROUPS = 8
local GAP = 2 -- between cells
local GROUP_GAP = 6 -- between groups
local TOP = 22 -- the title row
local LABEL = 12 -- a group's name above its column, or its number beside its row
Raid.MIN_WIDTH, Raid.MAX_WIDTH = 56, 120
Raid.MIN_HEIGHT, Raid.MAX_HEIGHT = 26, 52

local cells = {} -- every cell the headers made
local selected = {} -- guid -> true

local function Settings()
    return LivingAzerothDB.raid
end

-- Placement: the raid hangs from an anchor the player can drag (out of combat) while frames are unlocked.
local anchor = CreateFrame("Frame", "LivingAzerothRaidAnchor", UIParent)
Draw.Fit(anchor)
anchor:SetSize(1, 1)
anchor:SetMovable(true)
anchor:SetClampedToScreen(true)

local raid = CreateFrame("Frame", "LivingAzerothRaid", UIParent)
Draw.Fit(raid)
raid:SetSize(1, 1)
raid:SetPoint("TOPLEFT", anchor, "TOPLEFT")

local title = UI.Text(raid, "heading")
title:SetPoint("TOPLEFT", 1, -4)

local function SmallButton(text, glyph)
    local button = UI.Button(raid, text, nil, "plain", glyph)
    button:SetHeight(16)
    button.livingAzerothPip = true -- clicking it does not close the ring
    return button
end

local allButton = SmallButton("All my bots", "bot")
allButton:SetPoint("LEFT", title, "RIGHT", 8, 0)
local selectionButton = SmallButton("")
selectionButton:SetPoint("LEFT", allButton, "RIGHT", 4, 0)
local clearButton = SmallButton("Clear")
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

-- A bar filled from the left to a share (0 to 1), its end on a whole pixel.
local function Bar(parent)
    local bar = CreateFrame("Frame", nil, parent)
    bar.background = bar:CreateTexture(nil, "BACKGROUND")
    bar.background:SetAllPoints()
    bar.background:SetTexture(Style.WHITE)
    bar.fill = bar:CreateTexture(nil, "ARTWORK")
    bar.fill:SetTexture(Style.WHITE)
    bar.fill:SetPoint("TOPLEFT")
    bar.fill:SetPoint("BOTTOMLEFT")
    function bar:SetValue(share)
        self.share = share
        local width = Draw.Round(self:GetWidth() * math.max(0, math.min(1, share)))
        self.fill:SetWidth(math.max(width, Draw.Px(1)))
        LA.SetShown(self.fill, width > 0)
    end
    function bar:SetStatusBarColor(r, g, b)
        self.fill:SetVertexColor(r, g, b)
    end
    bar:SetScript("OnSizeChanged", function(self)
        self:SetValue(self.share or 0)
    end)
    return bar
end

local function OpenFor(target, anchorFrame)
    if #target.guids > 0 then
        LA.Ring.Toggle(target, anchorFrame)
    else
        UIErrorsFrame:AddMessage("None of your bots there.", 1, 0.25, 0.2)
    end
end

-- Builds a cell's insides: on the secure cells, and on the preview in the settings.
local function Build(cell)
    local T = Style.T()
    UI.Surface(cell, "cell")
    Draw.Box(cell, { fill = Style.C("#ffffff", 0.08), layer = "HIGHLIGHT" })
    local px = Draw.Px(1)
    cell.health = Bar(cell)
    cell.health:SetPoint("TOPLEFT", px, -px)
    cell.health.background:SetVertexColor(T.cellEmpty[1], T.cellEmpty[2], T.cellEmpty[3], 1)
    cell.power = Bar(cell)
    cell.power:SetPoint("BOTTOMLEFT", px, px)
    cell.power:SetPoint("BOTTOMRIGHT", -px, px)
    cell.power:SetHeight(Draw.Px(4))
    cell.power.background:SetVertexColor(0, 0, 0, 0.9)
    cell.split = cell.power:CreateTexture(nil, "OVERLAY") -- a dark line between health and power
    cell.split:SetTexture(Style.WHITE)
    cell.split:SetVertexColor(0, 0, 0, 1)
    cell.split:SetPoint("BOTTOMLEFT", cell.power, "TOPLEFT")
    cell.split:SetPoint("BOTTOMRIGHT", cell.power, "TOPRIGHT")
    cell.split:SetHeight(px)
    -- A soft sheen over the top of the bar, darker towards its foot.
    local text = CreateFrame("Frame", nil, cell)
    text:SetAllPoints()
    text:SetFrameLevel(cell.health:GetFrameLevel() + 2)
    cell.over = text
    local sheen = text:CreateTexture(nil, "BACKGROUND")
    sheen:SetTexture(Style.WHITE)
    sheen:SetPoint("TOPLEFT", cell.health)
    sheen:SetPoint("TOPRIGHT", cell.health)
    sheen:SetPoint("BOTTOM", cell.health, "CENTER")
    Draw.Paint(sheen, Style.C("#ffffff", 0.16), Style.C("#ffffff", 0.02))
    local foot = text:CreateTexture(nil, "BACKGROUND")
    foot:SetTexture(Style.WHITE)
    foot:SetPoint("BOTTOMLEFT", cell.health)
    foot:SetPoint("BOTTOMRIGHT", cell.health)
    foot:SetPoint("TOP", cell.health, "CENTER")
    Draw.Paint(foot, Style.C("#000000", 0), Style.C("#000000", 0.22))
    cell.ring = CreateFrame("Frame", nil, cell) -- gold around the player's target
    cell.ring:SetAllPoints()
    cell.ring:SetFrameLevel(text:GetFrameLevel() + 3)
    Draw.Box(cell.ring, { edges = { { 1, Style.gold }, { 1, Style.C("#000000", 0.6) } } })
    cell.role = UI.Icon(text, 9, "plain")
    cell.role:SetPoint("TOPLEFT", 3, -3)
    cell.name = UI.Text(text, "cell")
    cell.name:SetWordWrap(false)
    cell.status = UI.Text(text, "cellSmall")
    cell.status:SetWordWrap(false)
    cell.other = UI.Icon(text, 10, "plain") -- someone else's bot
    cell.other:SetPoint("TOPRIGHT", -3, -3)
    cell.other:Set("bot", T.muted)
    -- The order mark of one of the player's bots: its standing order, or the bot mark.
    cell.pip = CreateFrame("Button", nil, text)
    cell.pip.livingAzerothPip = true
    cell.pip:SetSize(14, 14)
    cell.pip:SetFrameLevel(cell.ring:GetFrameLevel() + 1)
    cell.pip.mark = UI.Icon(cell.pip, 14, "medal")
    cell.pip.mark:SetPoint("CENTER")
    cell.pip.glow = cell.pip:CreateTexture(nil, "OVERLAY")
    cell.pip.glow:SetTexture(Style.ART .. "tile-glow")
    cell.pip.glow:SetBlendMode("ADD")
    cell.pip.glow:SetPoint("CENTER")
    cell.pip.glow:SetSize(26, 26)
    local hover = cell.pip:CreateTexture(nil, "HIGHLIGHT")
    hover:SetTexture(Style.ART .. "tile-glow")
    hover:SetBlendMode("ADD")
    hover:SetPoint("CENTER")
    hover:SetSize(26, 26)
    hover:SetAlpha(0.35)
    cell.passive = UI.Icon(text, 9, "plain")
    cell.passive:Set("passive", Style.TONES.violet.light)
    cell.passive:SetPoint("RIGHT", cell.pip, "LEFT", -1, 0)
    cell.tick = UI.Icon(cell.pip, 9, "plain")
    cell.tick:Set("check", Style.green)
    cell.tick:SetPoint("CENTER", cell.pip, "TOPRIGHT", -2, -2)
    cell.tick:SetFrameLevel(cell.pip:GetFrameLevel() + 2)
end

-- Lays a cell's insides out for its size and the power bar setting.
local function Arrange(cell)
    local power = Settings().power
    LA.SetShown(cell.power, power)
    cell.health:SetPoint("BOTTOMRIGHT", -Draw.Px(1), power and Draw.Px(7) or Draw.Px(1))
    local height = cell:GetHeight()
    cell.pip:ClearAllPoints()
    cell.pip:SetPoint("RIGHT", -3, power and 1 or 0)
    -- Tall cells have the name at the top and the missing health under it; short ones put both on one line.
    local roomy = height >= 32
    cell.name:ClearAllPoints()
    cell.status:ClearAllPoints()
    cell.role:ClearAllPoints()
    if roomy then
        cell.namePoint, cell.nameY = "TOPLEFT", -4
        cell.role:SetPoint("TOPLEFT", 3, -4)
        cell.status:SetPoint("BOTTOMLEFT", 3, power and 6 or 3)
    else
        cell.namePoint, cell.nameY = "LEFT", power and 1 or 0
        cell.role:SetPoint("LEFT", 3, cell.nameY)
        cell.status:SetPoint("RIGHT", cell.pip, "LEFT", -2, 0)
    end
    cell.name:SetPoint(cell.namePoint, 3, cell.nameY)
end

-- Shows what info says: { name, class, health, max, power, powerMax, powerType, status, target, role, other,
-- mark (an order's glyph and tone), passive, glow (true, or "pulse"), done }.
local function Fill(cell, info)
    local colour = info.class and RAID_CLASS_COLORS[info.class] or NORMAL_FONT_COLOR
    cell.health:SetValue(info.max > 0 and info.health / info.max or 0)
    if info.status == "Offline" then
        cell.health:SetStatusBarColor(0.35, 0.35, 0.35)
    else
        cell.health:SetStatusBarColor(colour.r * 0.82, colour.g * 0.82, colour.b * 0.82)
    end
    cell.power:SetValue(info.powerMax > 0 and info.power / info.powerMax or 0)
    local powerColour = PowerBarColor[info.powerType] or PowerBarColor.MANA
    cell.power:SetStatusBarColor(powerColour.r, powerColour.g, powerColour.b)
    local look = info.role and Style.ROLE_LOOK[info.role]
    if look and info.role ~= "damage" then
        cell.role:Set(look[1], look[3])
        cell.role:Show()
    else
        cell.role:Hide()
    end
    local left = cell.role:IsShown() and 13 or 3
    cell.name:SetPoint(cell.namePoint, left, cell.nameY)
    cell.name:SetText(info.name)
    cell.status:SetText(info.status or "")
    LA.SetShown(cell.ring, info.target)
    LA.SetShown(cell.other, info.other)
    local right = info.other and 15 or 4
    if info.mark then
        cell.pip.mark:Set(info.mark[1], info.mark[2])
        cell.pip:Show()
        right = 19
    else
        cell.pip:Hide()
    end
    LA.SetShown(cell.passive, info.mark and info.passive)
    if cell.passive:IsShown() then
        right = right + 10
    end
    cell.name:SetWidth(math.max(8, cell:GetWidth() - left - right))
    if info.glow == "pulse" then
        cell.pip.glow:SetAlpha(0.45 + 0.45 * math.sin(GetTime() * 9))
    else
        cell.pip.glow:SetAlpha(1)
    end
    LA.SetShown(cell.pip.glow, info.mark and info.glow)
    LA.SetShown(cell.tick, info.mark and info.done)
end

function Raid.SetUpCell(cell)
    cell:RegisterForClicks("AnyUp")
    cell.menu = ShowMenu
    if not InCombatLockdown() then
        cell:SetSize(Settings().width, Settings().height)
    end
    Build(cell)
    Arrange(cell)
    cell:HookScript("OnSizeChanged", Arrange)
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
        LA.Ring.Toggle({ key = "bot:" .. guid, guids = { guid } }, cell)
    end)
    cell.pip:SetScript("OnEnter", function(self)
        local bot = cell.guid and LA.Bots.Mine()[cell.guid]
        if bot then
            GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
            GameTooltip:AddLine(bot.name, 1, 0.82, 0)
            GameTooltip:AddLine(LA.Orders.Standing(bot).state, 1, 1, 1)
            if bot.switches and bot.switches.passive then
                GameTooltip:AddLine("Staying out of fights", 0.82, 0.66, 1)
            end
            if bot.switches and bot.switches.loot == false then
                GameTooltip:AddLine("Not picking up loot", 0.7, 0.7, 0.7)
            end
            GameTooltip:AddLine("Click for orders, shift-click to pick several", 0.6, 0.6, 0.6)
            GameTooltip:Show()
        end
    end)
    cell.pip:SetScript("OnLeave", GameTooltip_Hide)
    cell:HookScript("OnEnter", function(self)
        local unit = self:GetAttribute("unit")
        if unit then
            GameTooltip_SetDefaultAnchor(GameTooltip, self)
            GameTooltip:SetUnit(unit)
        end
    end)
    cell:HookScript("OnLeave", GameTooltip_Hide)
    cells[#cells + 1] = cell
end

local function RoleOf(unit, bot)
    local role = bot and bot.role
    if not role then
        local tank, healer = UnitGroupRolesAssigned(unit)
        role = tank and "tank" or healer and "healer" or nil
    end
    return role
end

local function Read(unit)
    local _, class = UnitClass(unit)
    local info = {
        name = UnitName(unit), class = class, health = UnitHealth(unit), max = UnitHealthMax(unit),
        power = UnitPower(unit), powerMax = UnitPowerMax(unit), powerType = UnitPowerType(unit),
        target = UnitIsUnit(unit, "target"),
    }
    if not UnitIsConnected(unit) then
        info.status = "Offline"
    elseif UnitIsDeadOrGhost(unit) then
        info.status = "Dead"
    elseif info.health < info.max then
        local missing = info.max - info.health
        info.status = "-" .. (missing >= 1000 and string.format("%.1fk", missing / 1000) or missing)
    end
    return info
end

local function UpdateCell(cell)
    local unit = cell:GetAttribute("unit")
    if not unit or not UnitExists(unit) then
        cell.guid = nil
        return
    end
    local info = Read(unit)
    local guid = LA.Bots.Guid(unit)
    local bot = LA.Bots.ForUnit(unit)
    local mine = guid and LA.Bots.Mine()[guid]
    info.role = RoleOf(unit, mine)
    if mine and mine.commandable then
        cell.guid = guid
        local order = LA.Orders.Standing(mine)
        if LA.Orders.IconShown(mine) then
            info.mark = { order.glyph, order.tone }
        else
            info.mark = { "bot", "gold" }
        end
        info.passive = mine.switches and mine.switches.passive == true
        if LA.Orders.Pending(guid) then
            info.glow = "pulse"
        else
            info.glow = selected[guid] or LA.Ring.OpenFor() == "bot:" .. guid
        end
        info.done = LA.Orders.RecentlyDone(guid)
    else
        cell.guid = nil
        selected[guid or 0] = nil
        info.other = bot ~= nil
    end
    Fill(cell, info)
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
    headers[group] = header

    local label = CreateFrame("Button", nil, raid)
    label.livingAzerothPip = true
    label:SetHeight(LABEL)
    label.text = UI.Text(label, "cellSmall")
    label.text:SetPoint("LEFT", 1, 0)
    Draw.Box(label, { radius = 2, fill = Style.C("#ffe8b0", 0.08), layer = "HIGHLIGHT" })
    label:RegisterForClicks("AnyUp")
    label:SetScript("OnClick", function(self)
        OpenFor({ key = "group:" .. group, label = "Group " .. group, guids = BotsIn(group) }, self)
    end)
    UI.Tooltip(label, "Group " .. group, "Click for orders to your bots in this group.")
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

-- The groups in use, in a raid (or one, to show the frames' size while moving them out of a raid).
local function GroupsInUse()
    local used, count = {}, 0
    for i = 1, GetNumRaidMembers() do
        local _, _, subgroup = GetRaidRosterInfo(i)
        if subgroup and not used[subgroup] then
            used[subgroup] = true
            count = count + 1
        end
    end
    return used, count
end

-- Where group g sits, and the space all eight take, for the current size and layout.
local function Spot(group)
    local settings = Settings()
    if settings.layout == "rows" then
        return LABEL + 2, -(TOP + (group - 1) * (settings.height + GROUP_GAP))
    end
    return (group - 1) * (settings.width + GROUP_GAP), -(TOP + LABEL + 2)
end

local function Extent(groups)
    local settings = Settings()
    local w, h = settings.width, settings.height
    if settings.layout == "rows" then
        return LABEL + 2 + 5 * w + 4 * GAP, TOP + groups * h + (groups - 1) * GROUP_GAP
    end
    return groups * w + (groups - 1) * GROUP_GAP, TOP + LABEL + 2 + 5 * h + 4 * GAP
end

local mover

-- Applies the size and layout: the headers' arrangement and every cell's size. Out of combat only; in combat it
-- waits until the fight is over.
local pendingLayout = false
local applied -- the size and layout the cells have
function Raid.Apply()
    if InCombatLockdown() then
        pendingLayout = true
        return
    end
    pendingLayout = false
    local settings = Settings()
    local w, h = settings.width, settings.height
    local rows = settings.layout == "rows"
    for group, header in ipairs(headers) do
        header:SetAttribute("initialConfigFunction", string.format("self:SetWidth(%d) self:SetHeight(%d)", w, h))
        header:SetAttribute("point", rows and "LEFT" or "TOP")
        header:SetAttribute("xOffset", rows and GAP or 0)
        header:SetAttribute("yOffset", rows and 0 or -GAP)
        header:ClearAllPoints()
        header:SetPoint("TOPLEFT", raid, "TOPLEFT", Spot(group))
        local label = labels[group]
        label:ClearAllPoints()
        if rows then
            label:SetSize(LABEL, h)
            label:SetPoint("TOPRIGHT", header, "TOPLEFT", -2, 0)
            label.text:SetText(group)
        else
            label:SetSize(w, LABEL)
            label:SetPoint("BOTTOMLEFT", header, "TOPLEFT", 0, 2)
            label.text:SetText("Group " .. group)
        end
    end
    local shape = w .. "x" .. h .. settings.layout .. tostring(settings.power)
    if shape ~= applied then
        applied = shape
        for _, cell in ipairs(cells) do
            cell:SetSize(w, h)
            Arrange(cell)
        end
        -- Headers place their cells when their attributes change; a size change alone needs a nudge.
        for _, header in ipairs(headers) do
            if header:IsVisible() then
                header:Hide()
                header:Show()
            end
        end
    end
    local _, count = GroupsInUse()
    raid:SetSize(Extent(math.max(1, count)))
    if mover then
        mover:SetArea(Extent(math.max(1, count)))
    end
    Raid.Update()
end

function Raid.Update()
    if not raid:IsShown() then
        return
    end
    title:SetText("Raid · " .. GetNumRaidMembers())
    local used = GroupsInUse()
    for group = 1, GROUPS do
        LA.SetShown(labels[group], used[group])
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

-- A cell drawn from made-up values, for the settings: one of the player's bots, following.
function Raid.Preview(parent)
    local cell = CreateFrame("Frame", nil, parent)
    Build(cell)
    function cell:Refresh()
        local settings = Settings()
        self:SetSize(settings.width, settings.height)
        Arrange(self)
        Fill(self, {
            name = "Silannise", class = "SHAMAN", health = 7400, max = 9100, power = 6200, powerMax = 8800,
            powerType = 0, status = "-1.7k", role = "healer", mark = { "follow", "gold" },
        })
    end
    return cell
end

-- The CompactRaidFrame addon draws its own raid frames; with ours in use, its are hidden (a setting keeps them).
local function HideCompactRaidFrames()
    if not Settings().hideCompact or InCombatLockdown() then
        return
    end
    for _, name in ipairs({ "CompactRaidFrameManager", "CompactRaidFrameContainer" }) do
        local frame = _G[name]
        if frame and not frame.livingAzerothHidden then
            frame.livingAzerothHidden = true
            frame:Hide()
            hooksecurefunc(frame, "Show", function(self)
                if Settings().hideCompact and not InCombatLockdown() then
                    self:Hide()
                end
            end)
        end
    end
end

local function Place()
    local saved = Settings()
    anchor:ClearAllPoints()
    anchor:SetPoint("TOPLEFT", UIParent, "BOTTOMLEFT", saved.x, saved.y)
    Draw.Settle(anchor)
end

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
    if saved.power == nil then
        saved.power = true
    end
    saved.layout = saved.layout or "columns"
    saved.width = math.max(Raid.MIN_WIDTH, math.min(Raid.MAX_WIDTH, tonumber(saved.width) or 72))
    saved.height = math.max(Raid.MIN_HEIGHT, math.min(Raid.MAX_HEIGHT, tonumber(saved.height) or 36))
    -- Positions from before the raid frames were drawn on whole pixels are in other units.
    if not saved.fitted then
        saved.x, saved.y, saved.fitted = nil, nil, true
    end
    saved.x = saved.x or 20
    saved.y = saved.y or anchor:GetParent():GetHeight() * UIParent:GetEffectiveScale() / anchor:GetEffectiveScale()
        - 180
    LivingAzerothDB.raid = saved
    Place()
    -- Moving (out of combat), even when not in a raid: the mover covers where the groups in use are.
    mover = UI.Mover(anchor, "Raid frames", 1, 1, function()
        Draw.Settle(anchor)
        Settings().x, Settings().y = anchor:GetLeft(), anchor:GetTop()
    end)
    Raid.Apply()
    for _, header in ipairs(headers) do
        header:Show() -- only once the cell template exists
    end
end)

local events = CreateFrame("Frame")
events:RegisterEvent("PLAYER_ENTERING_WORLD")
events:RegisterEvent("RAID_ROSTER_UPDATE")
events:RegisterEvent("PLAYER_REGEN_ENABLED")
events:SetScript("OnEvent", function(_, event)
    HideCompactRaidFrames()
    if event == "RAID_ROSTER_UPDATE" or pendingLayout then
        Raid.Apply()
    end
end)
