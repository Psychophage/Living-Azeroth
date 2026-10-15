-- SPDX-License-Identifier: GPL-2.0-or-later
-- Raid frames: up to eight groups of compact cells (health, a thin power bar, role icons), built on the game's secure
-- raid headers so that clicking a cell targets its player in combat too. Each bot the player commands has a round
-- order mark in its cell that opens the orders ring; someone else's bots show a small grey bot mark. A group's name
-- opens the ring for that group's bots, and shift-clicking order marks picks a selection ("All my bots" and the
-- selection's buttons are AllBots.lua). Layout, look and the size of each part are the player's choice (Settings).
-- The bars are drawn from plain colour at the cell's own size, so no texture is stretched out of shape.
local _, LA = ...

local Raid = {}
LA.Raid = Raid

local UI, Style, Draw = LA.UI, LA.Style, LA.Draw

local GROUPS = 8
-- The sizes the player sets (Settings, "Raid sizes") as { least, most, default }: the cell, the spaces between cells
-- and between groups, and the text and marks in it, in units of the fitted raid frames; the power bar in pixels.
Raid.SIZES = {
    width = { 56, 140, 72 },
    height = { 24, 60, 36 },
    gap = { 0, 10, 2 },
    groupGap = { 0, 24, 6 },
    powerHeight = { 2, 12, 4 },
    font = { 8, 18, 11 }, -- names
    status = { 7, 16, 9 }, -- missing health, "Dead", "Offline"
    label = { 7, 14, 9 }, -- group names
    role = { 8, 24, 12 },
    mark = { 10, 24, 14 }, -- the order mark of each of the player's bots
}
local SIZE_KEYS = { "width", "height", "gap", "groupGap", "powerHeight", "font", "status", "label", "role", "mark" }
-- The game's round role icons (the dungeon finder's), by role: cells of 67 texels in a 256 texture.
local ROLES = "Interface\\LFGFrame\\UI-LFG-ICON-ROLES"
local function Cell(column, row)
    return { (column - 1) * 67 / 256, column * 67 / 256, (row - 1) * 67 / 256, row * 67 / 256 }
end
local ROLE_COORDS = { tank = Cell(1, 2), healer = Cell(2, 1), damage = Cell(2, 2) }

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

-- The bars' looks, all drawn from plain colour on whole pixels at the bar's own size:
-- "gradient": dark at the left, the full colour where a full bar ends, with dark rims along its top and foot (the
--   look of DragonUI's party bars);
-- "smooth": light along the top, darker towards the foot, with a light top edge and a dark foot;
-- "glossy": one colour with a sheen over its top half and a shade over its foot;
-- "flat": one colour.
local LOOKS = { gradient = true, smooth = true, glossy = true, flat = true }

local function Lighter(f, r, g, b)
    return r + (1 - r) * f, g + (1 - g) * f, b + (1 - b) * f
end

-- Paints a texture in a colour scaled by f at its left and by f2 at its right.
local function Across(texture, colour, f, f2)
    local r, g, b = colour[1], colour[2], colour[3]
    f2 = f2 or f
    texture:SetGradient("HORIZONTAL", r * f, g * f, b * f, r * f2, g * f2, b * f2)
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
    local px = Draw.Px(1)
    local function Over()
        local texture = bar:CreateTexture(nil, "OVERLAY")
        texture:SetTexture(Style.WHITE)
        return texture
    end
    bar.top = Over() -- the rim along the top
    bar.top:SetPoint("TOPLEFT", bar.fill)
    bar.top:SetPoint("TOPRIGHT", bar.fill)
    bar.top:SetHeight(px)
    bar.under = Over() -- the line under it, halfway to the bar's colour
    bar.under:SetPoint("TOPLEFT", bar.top, "BOTTOMLEFT")
    bar.under:SetPoint("TOPRIGHT", bar.top, "BOTTOMRIGHT")
    bar.under:SetHeight(px)
    bar.foot = Over() -- the rim along the foot
    bar.foot:SetPoint("BOTTOMLEFT", bar.fill)
    bar.foot:SetPoint("BOTTOMRIGHT", bar.fill)
    bar.foot:SetHeight(px)
    bar.sheen = Over() -- glossy: over the top half
    bar.sheen:SetPoint("TOPLEFT", bar.fill)
    bar.sheen:SetPoint("BOTTOMRIGHT", bar.fill, "RIGHT")
    Draw.Paint(bar.sheen, Style.C("#ffffff", 0.2), Style.C("#ffffff", 0.03))
    bar.shade = Over() -- glossy: over the bottom half
    bar.shade:SetPoint("TOPLEFT", bar.fill, "LEFT")
    bar.shade:SetPoint("BOTTOMRIGHT", bar.fill)
    Draw.Paint(bar.shade, Style.C("#000000", 0), Style.C("#000000", 0.25))

    -- Colours the fill and its rims for the look, the colour and how full the bar is.
    function bar:Paint()
        local look, colour, share = self.look or "flat", self.colour or { 1, 1, 1 }, self.share or 0
        local pixels = self:GetHeight() / px
        local rims = (look == "gradient" or look == "smooth") and pixels >= 5
        if look == "gradient" then
            -- Where the fill ends it has the colour a full bar has there, so it darkens as it empties.
            local left, right = 0.5, 0.5 + 0.5 * share
            Across(self.fill, colour, left, right)
            Across(self.top, colour, 0.3 * left, 0.3 * right)
            Across(self.under, colour, 0.72 * left, 0.72 * right)
            Across(self.foot, colour, 0.4 * left, 0.4 * right)
        elseif look == "smooth" then
            local r, g, b = colour[1], colour[2], colour[3]
            self.fill:SetGradient("VERTICAL", r * 0.6, g * 0.6, b * 0.6, r, g, b)
            local lr, lg, lb = Lighter(0.4, r, g, b)
            self.top:SetGradient("HORIZONTAL", lr, lg, lb, lr, lg, lb)
            Across(self.foot, colour, 0.35)
        else
            Across(self.fill, colour, look == "glossy" and 0.9 or 1)
        end
        local shown = share > 0
        LA.SetShown(self.fill, shown)
        LA.SetShown(self.top, shown and rims)
        LA.SetShown(self.foot, shown and rims)
        LA.SetShown(self.under, shown and rims and look == "gradient" and pixels >= 8)
        LA.SetShown(self.sheen, shown and look == "glossy")
        LA.SetShown(self.shade, shown and look == "glossy")
    end
    function bar:SetValue(share)
        self.share = math.max(0, math.min(1, share))
        self.fill:SetWidth(math.max(Draw.Round(self:GetWidth() * self.share), px))
        self:Paint()
    end
    function bar:SetLook(look)
        if look ~= self.look then
            self.look = look
            self:Paint()
        end
    end
    function bar:SetStatusBarColor(r, g, b)
        local colour = self.colour
        if not colour or colour[1] ~= r or colour[2] ~= g or colour[3] ~= b then
            self.colour = { r, g, b }
            self:Paint()
        end
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
    cell.power.background:SetVertexColor(0, 0, 0, 0.9)
    cell.split = cell.power:CreateTexture(nil, "OVERLAY") -- a dark line between health and power
    cell.split:SetTexture(Style.WHITE)
    cell.split:SetVertexColor(0, 0, 0, 1)
    cell.split:SetPoint("BOTTOMLEFT", cell.power, "TOPLEFT")
    cell.split:SetPoint("BOTTOMRIGHT", cell.power, "TOPRIGHT")
    cell.split:SetHeight(px)
    local text = CreateFrame("Frame", nil, cell)
    text:SetAllPoints()
    text:SetFrameLevel(cell.health:GetFrameLevel() + 2)
    cell.over = text
    cell.ring = CreateFrame("Frame", nil, cell) -- gold around the player's target
    cell.ring:SetAllPoints()
    cell.ring:SetFrameLevel(text:GetFrameLevel() + 3)
    Draw.Box(cell.ring, { edges = { { 1, Style.gold }, { 1, Style.C("#000000", 0.6) } } })
    cell.role = text:CreateTexture(nil, "OVERLAY")
    cell.role:SetTexture(ROLES)
    cell.name = UI.Text(text, "cell")
    cell.name:SetWordWrap(false)
    cell.status = UI.Text(text, "cellSmall")
    cell.status:SetWordWrap(false)
    cell.other = UI.Icon(text, 10, "plain") -- someone else's bot
    cell.other:Set("bot", T.muted)
    -- The order mark of one of the player's bots: its standing order, or the bot mark.
    cell.pip = CreateFrame("Button", nil, text)
    cell.pip.livingAzerothPip = true
    cell.pip:SetFrameLevel(cell.ring:GetFrameLevel() + 1)
    cell.pip.mark = UI.Icon(cell.pip, 14, "medal")
    cell.pip.mark:SetPoint("CENTER")
    cell.pip.glow = UI.MedalGlow(cell.pip, 14, "OVERLAY")
    cell.pip.hover = UI.MedalGlow(cell.pip, 14, "HIGHLIGHT")
    cell.pip.hover:SetAlpha(0.45)
    cell.passive = UI.Icon(text, 9, "plain")
    cell.passive:Set("passive", Style.TONES.violet.light)
    cell.passive:SetPoint("RIGHT", cell.pip, "LEFT", -1, 0)
    cell.tick = UI.Icon(cell.pip, 9, "plain")
    cell.tick:Set("check", Style.green)
    cell.tick:SetPoint("CENTER", cell.pip, "TOPRIGHT", -2, -2)
    cell.tick:SetFrameLevel(cell.pip:GetFrameLevel() + 2)
end

-- Lays a cell's insides out for its size, the sizes of its parts and the power bar setting.
local function Arrange(cell)
    local settings = Settings()
    local power = settings.power
    local px = Draw.Px(1)
    LA.SetShown(cell.power, power)
    -- The power bar, the dark line over it, then the health bar.
    cell.power:SetHeight(Draw.Px(settings.powerHeight))
    cell.health:SetPoint("BOTTOMRIGHT", -px, power and Draw.Px(settings.powerHeight + 2) or px)
    local size, small = settings.font, settings.status
    UI.TextSize(cell.name, size)
    UI.TextSize(cell.status, small)
    cell.roleSize = settings.role
    cell.role:SetSize(cell.roleSize, cell.roleSize)
    local mark = settings.mark
    cell.pip:SetSize(mark, mark)
    cell.pip.mark:Resize(mark)
    cell.pip.glow:SetSize(mark * 2, mark * 2)
    cell.pip.hover:SetSize(mark * 2, mark * 2)
    local little = Draw.Round(math.max(7, mark * 0.65)) -- the passive and done marks beside the order mark
    cell.passive:Resize(little)
    cell.tick:Resize(little)
    cell.other:Resize(Draw.Round(math.max(8, mark * 0.7)))
    local height = cell:GetHeight()
    local lift = power and Draw.Round(settings.powerHeight * px / 2) or 0 -- centred over the health bar
    cell.pip:ClearAllPoints()
    cell.pip:SetPoint("RIGHT", -3, lift)
    cell.other:ClearAllPoints()
    cell.other:SetPoint("TOPRIGHT", -3, -3)
    -- Tall cells have the name at the top and the missing health under it; short ones put both on one line.
    local roomy = height >= size + small + 12 + (power and settings.powerHeight * px or 0)
    cell.name:ClearAllPoints()
    cell.status:ClearAllPoints()
    cell.role:ClearAllPoints()
    if roomy then
        cell.namePoint, cell.nameY = "TOPLEFT", -4
        cell.role:SetPoint("TOPLEFT", 3, -3)
        cell.status:SetPoint("BOTTOMLEFT", 3, (power and Draw.Px(settings.powerHeight + 2) or 0) + 3)
    else
        cell.namePoint, cell.nameY = "LEFT", lift
        cell.role:SetPoint("LEFT", 3, lift)
        cell.status:SetPoint("RIGHT", cell.pip, "LEFT", -2, 0)
    end
    cell.name:SetPoint(cell.namePoint, 3, cell.nameY)
end

-- The health bar's colour for a class: the player's choice of class colour, a darker one, or green.
local function HealthColour(class)
    local mode = Settings().health
    if mode == "green" then
        return 0.42, 0.84, 0.14
    end
    local colour = class and RAID_CLASS_COLORS[class] or NORMAL_FONT_COLOR
    local shade = mode == "dark" and 0.5 or (Settings().bars == "flat" and 0.82 or 1)
    return colour.r * shade, colour.g * shade, colour.b * shade
end

-- Shows what info says: { name, class, health, max, power, powerMax, powerType, status, target, role, other,
-- mark (an order's glyph and tone), passive, glow (true, or "pulse"), done }.
local function Fill(cell, info)
    local settings = Settings()
    cell.health:SetLook(settings.bars)
    if info.status == "Offline" then
        cell.health:SetStatusBarColor(0.35, 0.35, 0.35)
    else
        cell.health:SetStatusBarColor(HealthColour(info.class))
    end
    cell.health:SetValue(info.max > 0 and info.health / info.max or 0)
    cell.power:SetLook(settings.bars)
    local powerColour = PowerBarColor[info.powerType] or PowerBarColor.MANA
    cell.power:SetStatusBarColor(powerColour.r, powerColour.g, powerColour.b)
    cell.power:SetValue(info.powerMax > 0 and info.power / info.powerMax or 0)
    local roles = settings.roles
    local coords = info.role and ROLE_COORDS[info.role]
    if coords and (roles == "all" or (roles == "some" and info.role ~= "damage")) then
        cell.role:SetTexCoord(unpack(coords))
        cell.role:Show()
    else
        cell.role:Hide()
    end
    local left = cell.role:IsShown() and cell.roleSize + 4 or 3
    cell.name:SetPoint(cell.namePoint, left, cell.nameY)
    cell.name:SetText(info.name)
    local nameColour = settings.names == "class" and info.class and RAID_CLASS_COLORS[info.class]
    if nameColour then
        cell.name:SetTextColor(nameColour.r, nameColour.g, nameColour.b)
    else
        cell.name:SetTextColor(1, 1, 1)
    end
    cell.status:SetText(info.status or "")
    LA.SetShown(cell.ring, info.target)
    LA.SetShown(cell.other, info.other)
    local right = info.other and cell.other:GetWidth() + 5 or 4
    if info.mark then
        cell.pip.mark:Set(info.mark[1], info.mark[2])
        cell.pip:Show()
        right = settings.mark + 6
    else
        cell.pip:Hide()
    end
    LA.SetShown(cell.passive, info.mark and info.passive)
    if cell.passive:IsShown() then
        right = right + cell.passive:GetWidth() + 1
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
            LA.Fire("raid:selection")
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
        local tank, healer, damage = UnitGroupRolesAssigned(unit)
        role = tank and "tank" or healer and "healer" or damage and "damage" or nil
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

-- The bots picked with shift-click, for "Orders for N selected" (AllBots.lua).
function Raid.Selection()
    local guids = {}
    for guid in pairs(selected) do
        guids[#guids + 1] = guid
    end
    table.sort(guids)
    return guids
end

function Raid.ClearSelection()
    wipe(selected)
    LA.Fire("raid:selection")
    Raid.Update()
end

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

-- A group's name above its column, or its number beside its row, is a little taller than its text.
local function LabelSize()
    return Settings().label + 3
end

-- Where group g sits, and the space the groups take, for the current sizes, layout and group names.
local function Labelled()
    return Settings().labels and LabelSize() + 2 or 0
end

local function Spot(group)
    local settings = Settings()
    if settings.layout == "rows" then
        return Labelled(), -((group - 1) * (settings.height + settings.groupGap))
    end
    return (group - 1) * (settings.width + settings.groupGap), -Labelled()
end

local function Extent(groups)
    local settings = Settings()
    local w, h, gap, groupGap = settings.width, settings.height, settings.gap, settings.groupGap
    if settings.layout == "rows" then
        return Labelled() + 5 * w + 4 * gap, groups * h + (groups - 1) * groupGap
    end
    return groups * w + (groups - 1) * groupGap, Labelled() + 5 * h + 4 * gap
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
        header:SetAttribute("xOffset", rows and settings.gap or 0)
        header:SetAttribute("yOffset", rows and 0 or -settings.gap)
        header:ClearAllPoints()
        header:SetPoint("TOPLEFT", raid, "TOPLEFT", Spot(group))
        local label = labels[group]
        label:ClearAllPoints()
        UI.TextSize(label.text, settings.label)
        if rows then
            label:SetSize(LabelSize(), h)
            label:SetPoint("TOPRIGHT", header, "TOPLEFT", -2, 0)
            label.text:SetText(group)
        else
            label:SetSize(w, LabelSize())
            label:SetPoint("BOTTOMLEFT", header, "TOPLEFT", 0, 2)
            label.text:SetText("Group " .. group)
        end
    end
    local shape = { settings.layout, tostring(settings.power), settings.bars, tostring(settings.labels) }
    for _, key in ipairs(SIZE_KEYS) do
        shape[#shape + 1] = settings[key]
    end
    shape = table.concat(shape, " ")
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
    anchor:SetSize(Extent(math.max(1, count))) -- so the whole raid stays on screen, not just its corner
    if mover then
        mover:SetArea(Extent(math.max(1, count)))
    end
    Raid.Update()
end

function Raid.Update()
    if not raid:IsShown() then
        return
    end
    local used = GroupsInUse()
    for group = 1, GROUPS do
        LA.SetShown(labels[group], used[group] and Settings().labels)
    end
    for _, cell in ipairs(cells) do
        if cell:IsVisible() then
            UpdateCell(cell)
        end
    end
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
            target = true,
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
    for key, size in pairs(Raid.SIZES) do
        saved[key] = math.max(size[1], math.min(size[2], tonumber(saved[key]) or size[3]))
    end
    -- The bars were once DragonUI's textures, stretched to the cell; the gradient look is drawn like them.
    if saved.bars == "dragonui" then
        saved.bars = "gradient"
    end
    saved.bars = LOOKS[saved.bars] and saved.bars or "gradient"
    saved.health = saved.health or "class"
    saved.names = saved.names or "white"
    saved.roles = saved.roles or "some"
    if saved.labels == nil then
        saved.labels = true
    end
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
