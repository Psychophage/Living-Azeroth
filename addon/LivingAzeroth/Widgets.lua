-- SPDX-License-Identifier: GPL-2.0-or-later
-- Building blocks in the game's own style, shared by the windows.
local _, LA = ...

local UI = {}
LA.UI = UI

local CLASS_ICONS = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-Classes"
local CLASSES = { "WARRIOR", "PALADIN", "HUNTER", "ROGUE", "PRIEST", "DEATHKNIGHT", "SHAMAN", "MAGE", "WARLOCK",
    nil, "DRUID" }
UI.CLASSES = CLASSES

-- The class token for a class id the server sends.
function UI.ClassToken(class)
    return CLASSES[class or 0]
end

function UI.ClassColor(class)
    local token = CLASSES[class or 0]
    return token and RAID_CLASS_COLORS[token] or NORMAL_FONT_COLOR
end

-- A name in its class colour.
function UI.Coloured(name, class)
    local color = UI.ClassColor(class)
    return string.format("|cff%02x%02x%02x%s|r", color.r * 255, color.g * 255, color.b * 255, name or "?")
end

-- Sets a texture to a class icon.
function UI.SetClassIcon(texture, class)
    local token = CLASSES[class or 0]
    texture:SetTexture(CLASS_ICONS)
    if token and CLASS_ICON_TCOORDS[token] then
        texture:SetTexCoord(unpack(CLASS_ICON_TCOORDS[token]))
    else
        texture:SetTexCoord(0, 0.25, 0, 0.25)
    end
end

function UI.ClassName(class)
    local token = CLASSES[class or 0]
    return token and LOCALIZED_CLASS_NAMES_MALE[token] or ""
end

-- The party or raid unit for a low guid, if the character is in the player's group.
function UI.UnitFor(guid)
    if not guid then
        return nil
    end
    if LA.Bots.Guid("player") == guid then
        return "player"
    end
    for i = 1, GetNumRaidMembers() do
        if LA.Bots.Guid("raid" .. i) == guid then
            return "raid" .. i
        end
    end
    for i = 1, GetNumPartyMembers() do
        if LA.Bots.Guid("party" .. i) == guid then
            return "party" .. i
        end
    end
    return nil
end

-- A character's face when they are in the group, otherwise their class icon, in a round portrait texture.
function UI.SetCharacterPortrait(texture, guid, class)
    local unit = UI.UnitFor(guid)
    if unit and UnitExists(unit) then
        SetPortraitTexture(texture, unit)
        texture:SetTexCoord(0, 1, 0, 1)
    else
        UI.SetClassIcon(texture, class)
    end
end

-- Text

-- Dark text for parchment, as the Quest Log's details use: "title", "body" or "small".
function UI.Paper(parent, kind, width)
    local font = kind == "title" and "QuestTitleFont" or kind == "small" and "QuestFontNormalSmall" or "QuestFont"
    local text = parent:CreateFontString(nil, "OVERLAY", font)
    text:SetJustifyH("LEFT")
    text:SetJustifyV("TOP")
    if width then
        text:SetWidth(width)
    end
    return text
end

-- Light text for the dark parts of a window.
function UI.Text(parent, width, template)
    local text = parent:CreateFontString(nil, "OVERLAY", template or "GameFontHighlight")
    text:SetJustifyH("LEFT")
    text:SetJustifyV("TOP")
    if width then
        text:SetWidth(width)
    end
    return text
end

function UI.Button(parent, text, width)
    local button = CreateFrame("Button", nil, parent, "UIPanelButtonTemplate")
    button:SetSize(width or 100, 21)
    button:SetText(text)
    return button
end

-- A radio button. The game's own texture holds its four states side by side, and at large interface scales the
-- neighbouring state bleeds in as a thin line; each state here is cut half a pixel inside its edges.
local RADIO = "Interface\\Buttons\\UI-RadioButton"
local HALF = 0.5 / 64
local function RadioState(left)
    return left + HALF, left + 0.25 - HALF, HALF * 4, 1 - HALF * 4
end
function UI.Radio(parent, label, paper)
    local radio = CreateFrame("CheckButton", nil, parent)
    radio:SetSize(16, 16)
    radio:SetNormalTexture(RADIO)
    radio:GetNormalTexture():SetTexCoord(RadioState(0))
    radio:SetCheckedTexture(RADIO)
    radio:GetCheckedTexture():SetTexCoord(RadioState(0.25))
    radio:SetHighlightTexture(RADIO, "ADD")
    radio:GetHighlightTexture():SetTexCoord(RadioState(0.5))
    if label then
        radio.label = radio:CreateFontString(nil, "OVERLAY", paper and "QuestFont" or "GameFontHighlight")
        radio.label:SetPoint("LEFT", radio, "RIGHT", 3, 0)
        radio.label:SetText(label)
        radio:SetHitRectInsets(0, -radio.label:GetStringWidth() - 6, 0, 0)
    end
    return radio
end

-- A check box with a label and a smaller hint under it: dark text for parchment, or light text (dark = true) for
-- the Dungeon Finder's dark paper.
function UI.Check(parent, label, hint, dark)
    local check = CreateFrame("CheckButton", nil, parent, "UICheckButtonTemplate")
    check:SetSize(24, 24)
    check.label = check:CreateFontString(nil, "OVERLAY", dark and "GameFontHighlight" or "QuestFont")
    check.label:SetPoint("TOPLEFT", check, "TOPRIGHT", 1, -4)
    check.label:SetText(label)
    if hint then
        check.hint = check:CreateFontString(nil, "OVERLAY", dark and "GameFontDisableSmall" or "QuestFontNormalSmall")
        check.hint:SetPoint("TOPLEFT", check.label, "BOTTOMLEFT", 0, -1)
        check.hint:SetText(hint)
    end
    check:SetHitRectInsets(0, -math.max(check.label:GetStringWidth(), 120), 0, 0)
    function check:SetUsable(usable)
        if usable then
            self:Enable()
            if dark then
                self.label:SetTextColor(1, 1, 1)
            else
                self.label:SetTextColor(0, 0, 0)
            end
        else
            self:Disable()
            self.label:SetTextColor(0.35, 0.3, 0.25)
        end
    end
    return check
end

-- A round portrait in a gold ring, like the Dungeon Finder's role buttons, with a glow when chosen.
function UI.RoundButton(parent, size)
    local button = CreateFrame("Button", nil, parent)
    button:SetSize(size, size)
    button.portrait = button:CreateTexture(nil, "ARTWORK")
    button.portrait:SetPoint("CENTER")
    button.portrait:SetSize(size * 0.8, size * 0.8)
    button.ring = button:CreateTexture(nil, "OVERLAY")
    button.ring:SetTexture("Interface\\LFGFrame\\UI-LFG-ICON-REWARDRING")
    button.ring:SetTexCoord(0, 40 / 64, 0, 40 / 64) -- the ring fills only that corner of its texture
    button.ring:SetAllPoints()
    function button:SetCharacter(guid, class)
        UI.SetCharacterPortrait(self.portrait, guid, class)
    end
    function button:SetIcon(path)
        SetPortraitToTexture(self.portrait, path)
        self.portrait:SetTexCoord(0, 1, 0, 1)
    end
    return button
end

-- A list like the Quest Log's: gold headers that fold with a + or -, entries under them with an icon, a name and a
-- small tag on the right, the chosen one highlighted. It scrolls in the track the frame art draws beside it.
-- Items: { header = true, key, text } or { key, text, icon (path or function(texture)), tag, under = header key }.
-- onClick(item, mouseButton) for entries; headers fold themselves.
local listCount = 0
local ROW = 18
function UI.List(parent, onClick)
    listCount = listCount + 1
    local list = { items = {}, rows = {}, folded = {} }
    local count = math.floor(parent:GetHeight() / ROW)
    local scroll = CreateFrame("ScrollFrame", "LivingAzerothList" .. listCount, parent, "FauxScrollFrameTemplate")
    scroll:SetAllPoints()
    local bar = _G[scroll:GetName() .. "ScrollBar"]
    bar:ClearAllPoints()
    bar:SetPoint("TOPLEFT", scroll, "TOPRIGHT", 5, -13)
    bar:SetPoint("BOTTOMLEFT", scroll, "BOTTOMRIGHT", 5, 14)
    list.scroll = scroll

    list.empty = parent:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
    list.empty:SetPoint("CENTER", -6, 16)
    list.empty:SetWidth(240)

    for i = 1, count do
        local row = CreateFrame("Button", nil, parent)
        row:SetHeight(ROW)
        row:SetPoint("TOPLEFT", 0, -(i - 1) * ROW)
        row:SetPoint("RIGHT", parent, "RIGHT", 0, 0)
        row:RegisterForClicks("LeftButtonUp", "RightButtonUp")
        row.fold = row:CreateTexture(nil, "ARTWORK")
        row.fold:SetSize(16, 16)
        row.fold:SetPoint("LEFT", 3, 0)
        row.icon = row:CreateTexture(nil, "ARTWORK")
        row.icon:SetSize(15, 15)
        row.icon:SetPoint("LEFT", 20, 0)
        row.text = row:CreateFontString(nil, "OVERLAY", "GameFontNormal")
        row.text:SetJustifyH("LEFT")
        row.tag = row:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
        row.tag:SetPoint("RIGHT", -4, 0)
        row.tag:SetJustifyH("RIGHT")
        row.tag:SetTextColor(0.7, 0.7, 0.7)
        row.chosen = row:CreateTexture(nil, "BACKGROUND")
        row.chosen:SetTexture("Interface\\QuestFrame\\UI-QuestLogTitleHighlight")
        row.chosen:SetBlendMode("ADD")
        row.chosen:SetVertexColor(1, 0.82, 0, 0.75)
        row.chosen:SetAllPoints()
        row:SetHighlightTexture("Interface\\QuestFrame\\UI-QuestLogTitleHighlight", "ADD")
        row:GetHighlightTexture():SetVertexColor(0.8, 0.8, 0.8, 0.5)
        row:SetScript("OnClick", function(self, mouse)
            local item = self.item
            if not item then
                return
            end
            if item.header then
                list.folded[item.key] = not list.folded[item.key] or nil
                list:Refresh()
            elseif onClick then
                onClick(item, mouse)
            end
        end)
        row:SetScript("OnEnter", function(self)
            if self.item and self.item.tooltip then
                GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
                GameTooltip:AddLine(self.item.tooltip[1], 1, 1, 1)
                for i = 2, #self.item.tooltip do
                    GameTooltip:AddLine(self.item.tooltip[i], nil, nil, nil, true)
                end
                GameTooltip:Show()
            end
        end)
        row:SetScript("OnLeave", GameTooltip_Hide)
        list.rows[i] = row
    end

    local function Visible()
        local shown = {}
        for _, item in ipairs(list.items) do
            if item.header or not (item.under and list.folded[item.under]) then
                shown[#shown + 1] = item
            end
        end
        return shown
    end

    function list:Refresh()
        local shown = Visible()
        local offset = FauxScrollFrame_GetOffset(scroll)
        for i, row in ipairs(self.rows) do
            local item = shown[i + offset]
            row.item = item
            if not item then
                row:Hide()
            else
                row:Show()
                row.text:ClearAllPoints()
                if item.header then
                    row.fold:SetTexture(self.folded[item.key] and "Interface\\Buttons\\UI-PlusButton-Up" or
                        "Interface\\Buttons\\UI-MinusButton-Up")
                    row.fold:Show()
                    row.icon:Hide()
                    row.text:SetFontObject("GameFontNormal")
                    row.text:SetPoint("LEFT", row.fold, "RIGHT", 2, 0)
                    row.text:SetText(item.text)
                    row.tag:SetText(item.tag or "")
                    row.chosen:Hide()
                else
                    row.fold:Hide()
                    if item.icon then
                        if type(item.icon) == "function" then
                            item.icon(row.icon)
                        else
                            row.icon:SetTexture(item.icon)
                            row.icon:SetTexCoord(0.08, 0.92, 0.08, 0.92)
                        end
                        row.icon:Show()
                        row.text:SetPoint("LEFT", row.icon, "RIGHT", 5, 0)
                    else
                        row.icon:Hide()
                        row.text:SetPoint("LEFT", 22, 0)
                    end
                    row.text:SetFontObject("GameFontHighlight")
                    row.text:SetText(item.text)
                    row.tag:SetText(item.tag or "")
                    LA.SetShown(row.chosen, self.chosen ~= nil and item.key == self.chosen)
                end
                row.text:SetPoint("RIGHT", row.tag, "LEFT", -4, 0)
            end
        end
        FauxScrollFrame_Update(scroll, #shown, count, ROW)
        bar:Show() -- the track is in the art; the bar stays, as the Quest Log's does
        self.empty:SetText(#self.items == 0 and (self.emptyText or "") or "")
    end
    scroll:SetScript("OnVerticalScroll", function(self, offset)
        FauxScrollFrame_OnVerticalScroll(self, offset, ROW, function() list:Refresh() end)
    end)
    function list:SetItems(items, emptyText)
        self.items = items
        self.emptyText = emptyText
        self:Refresh()
    end
    function list:Choose(key)
        self.chosen = key
        self:Refresh()
    end
    return list
end

-- Stacks regions down a parchment page: stack:Add(region, gap, indent) places it under the last one;
-- stack:Height() is the space used, for the scroll child.
function UI.Stack(parent)
    local stack = { parent = parent, y = -6 }
    function stack:Add(region, gap, indent, height)
        self.y = self.y - (gap or 0)
        region:ClearAllPoints()
        region:SetPoint("TOPLEFT", self.parent, "TOPLEFT", 6 + (indent or 0), self.y)
        region:Show()
        -- A wrapped font string reports its text's height, not the frame's.
        local measured = region.GetStringHeight and math.max(region:GetStringHeight(), region:GetHeight()) or
            region:GetHeight()
        self.y = self.y - (height or measured)
        return region
    end
    function stack:Reset()
        self.y = -6
    end
    function stack:Height()
        return -self.y + 10
    end
    return stack
end

-- Small tabs along the bottom of a Dungeon Finder style window's metal bar, for the pages within it.
-- onSelect(index) is called when one is clicked; :Select(index) chooses one without calling it.
local tabCount = 0
function UI.SubTabs(bar, names, onSelect)
    local tabs = { buttons = {} }
    for i, name in ipairs(names) do
        tabCount = tabCount + 1
        local tab = CreateFrame("Button", "LivingAzerothSubTab" .. tabCount, bar, "TabButtonTemplate")
        tab:SetText(name)
        PanelTemplates_TabResize(tab, 0)
        if i == 1 then
            tab:SetPoint("BOTTOMLEFT", 6, -2)
        else
            tab:SetPoint("LEFT", tabs.buttons[i - 1], "RIGHT", 0, 0)
        end
        tab:SetScript("OnClick", function()
            tabs:Select(i)
            onSelect(i)
        end)
        tabs.buttons[i] = tab
    end
    function tabs:Select(index)
        self.selected = index
        for i, tab in ipairs(self.buttons) do
            if i == index then
                PanelTemplates_SelectTab(tab)
            else
                PanelTemplates_DeselectTab(tab)
            end
        end
    end
    return tabs
end

-- What "Move frames" shows over something the player can drag: a box over its whole area in the game's gold
-- edging, named, that moves it. Every mover looks the same. width and height are the area; onMoved() saves.
function UI.Mover(target, label, width, height, onMoved)
    local mover = CreateFrame("Frame", nil, UIParent)
    mover:SetFrameStrata("DIALOG")
    mover:SetPoint("TOPLEFT", target, "TOPLEFT", -4, 4)
    mover:SetSize(width + 8, height + 8)
    mover:SetBackdrop({ bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
        edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border", edgeSize = 16,
        insets = { left = 4, right = 4, top = 4, bottom = 4 } })
    mover:SetBackdropColor(0.2, 0.15, 0.05, 0.55)
    mover:SetBackdropBorderColor(1, 0.82, 0)
    local text = mover:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    text:SetPoint("CENTER")
    text:SetText(label)
    local hint = mover:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    hint:SetPoint("TOP", text, "BOTTOM", 0, -3)
    hint:SetText("Drag to move")
    mover:EnableMouse(true)
    mover:RegisterForDrag("LeftButton")
    mover:SetScript("OnDragStart", function()
        if not InCombatLockdown() then
            target:SetMovable(true)
            target:StartMoving()
        end
    end)
    mover:SetScript("OnDragStop", function()
        target:StopMovingOrSizing()
        if onMoved then
            onMoved()
        end
    end)
    mover:Hide()
    return mover
end

-- Text for the Dungeon Finder's dark paper: "title" (gold), "body" (white) or "small" (grey).
function UI.Dark(parent, kind, width)
    local font = kind == "title" and "GameFontNormalLarge" or kind == "small" and "GameFontHighlightSmall" or
        "GameFontHighlight"
    local text = parent:CreateFontString(nil, "OVERLAY", font)
    if kind == "small" then
        text:SetTextColor(0.72, 0.72, 0.72) -- the disabled grey is too dark to read on this paper
    end
    text:SetJustifyH("LEFT")
    text:SetJustifyV("TOP")
    if width then
        text:SetWidth(width)
    end
    return text
end
