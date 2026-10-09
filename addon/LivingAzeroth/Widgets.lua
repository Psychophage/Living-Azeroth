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

-- The party or raid unit for a low guid, if the character is in the player's group.
function UI.UnitFor(guid)
    if not guid then
        return nil
    end
    if LA.Bots.Guid("player") == guid then
        return "player"
    end
    local raid = GetNumRaidMembers()
    if raid > 0 then
        for i = 1, raid do
            if LA.Bots.Guid("raid" .. i) == guid then
                return "raid" .. i
            end
        end
    end
    for i = 1, GetNumPartyMembers() do
        if LA.Bots.Guid("party" .. i) == guid then
            return "party" .. i
        end
    end
    return nil
end

-- A round button like the Dungeon Finder's role buttons: a portrait in a gold ring, a label under it, and a
-- glow when chosen.
function UI.RoundButton(parent, size, label)
    local button = CreateFrame("Button", nil, parent)
    button:SetSize(size, size)
    button.portrait = button:CreateTexture(nil, "ARTWORK")
    button.portrait:SetPoint("CENTER")
    button.portrait:SetSize(size * 0.8, size * 0.8)
    button.ring = button:CreateTexture(nil, "OVERLAY")
    button.ring:SetTexture("Interface\\LFGFrame\\UI-LFG-ICON-REWARDRING")
    button.ring:SetTexCoord(0, 40 / 64, 0, 40 / 64) -- the ring fills only that corner of its texture
    button.ring:SetPoint("CENTER")
    button.ring:SetSize(size, size)
    button.glow = button:CreateTexture(nil, "BACKGROUND")
    button.glow:SetTexture("Interface\\Minimap\\UI-Minimap-ZoomButton-Highlight")
    button.glow:SetBlendMode("ADD")
    button.glow:SetPoint("CENTER")
    button.glow:SetSize(size * 1.25, size * 1.25)
    button.glow:Hide()
    button:SetHighlightTexture("Interface\\Minimap\\UI-Minimap-ZoomButton-Highlight", "ADD")
    button:GetHighlightTexture():ClearAllPoints()
    button:GetHighlightTexture():SetPoint("CENTER")
    button:GetHighlightTexture():SetSize(size * 1.1, size * 1.1)
    button.label = button:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    button.label:SetPoint("TOP", button, "BOTTOM", 0, -1)
    button.label:SetWidth(size + 26)
    button.label:SetHeight(12)
    button.label:SetText(label or "")
    function button:SetChosen(chosen)
        LA.SetShown(self.glow, chosen)
        self.label:SetFontObject(chosen and "GameFontNormalSmall" or "GameFontHighlightSmall")
    end
    -- The character's face when they are in the group, otherwise their class icon; an icon path otherwise.
    function button:SetCharacter(guid, class)
        local unit = UI.UnitFor(guid)
        if unit and UnitExists(unit) then
            SetPortraitTexture(self.portrait, unit)
            self.portrait:SetTexCoord(0, 1, 0, 1)
        else
            UI.SetClassIcon(self.portrait, class)
        end
    end
    function button:SetIcon(path)
        SetPortraitToTexture(self.portrait, path)
        self.portrait:SetTexCoord(0, 1, 0, 1)
    end
    return button
end

-- Small tabs along the bottom of a window's metal bar, for the pages within a page. onSelect(index) is called
-- when one is clicked; :Select(index) chooses one without calling it.
local tabCount = 0
function UI.SubTabs(bar, names, onSelect)
    local tabs = { buttons = {} }
    for i, name in ipairs(names) do
        tabCount = tabCount + 1
        local tab = CreateFrame("Button", "LivingAzerothSubTab" .. tabCount, bar, "TabButtonTemplate")
        tab:SetText(name)
        PanelTemplates_TabResize(tab, 0)
        if i == 1 then
            tab:SetPoint("BOTTOMLEFT", 8, -2)
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

-- A check box with a label and a grey hint under it.
function UI.Check(parent, label, hint)
    local check = CreateFrame("CheckButton", nil, parent, "UICheckButtonTemplate")
    check:SetSize(24, 24)
    check.label = check:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
    check.label:SetPoint("TOPLEFT", check, "TOPRIGHT", 2, -3)
    check.label:SetText(label)
    if hint then
        check.hint = check:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
        check.hint:SetPoint("TOPLEFT", check.label, "BOTTOMLEFT", 0, -1)
        check.hint:SetText(hint)
        check.hint:SetTextColor(0.75, 0.7, 0.6)
    end
    return check
end

-- A gold heading in the Dungeon Finder's title font.
function UI.Heading(parent, text)
    local heading = parent:CreateFontString(nil, "OVERLAY", "QuestTitleFontBlackShadow")
    heading:SetJustifyH("LEFT")
    heading:SetText(text or "")
    return heading
end

-- White body text that wraps within width.
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
    button:SetSize(width or 112, 22)
    button:SetText(text)
    return button
end

-- A list of rows that scrolls: rows are made by makeRow(row) once; fill(row, item) shows an item. :SetItems(list)
-- replaces the items. Rows are buttons; onClick(item) is called when one is clicked.
local listCount = 0
function UI.List(parent, rowCount, rowHeight, makeRow, fill, onClick)
    listCount = listCount + 1
    local list = { items = {}, rows = {} }
    local frame = CreateFrame("Frame", nil, parent)
    list.frame = frame
    local scroll = CreateFrame("ScrollFrame", "LivingAzerothList" .. listCount, frame, "FauxScrollFrameTemplate")
    scroll:SetPoint("TOPLEFT", 0, 0)
    scroll:SetPoint("BOTTOMRIGHT", -24, 0)
    for i = 1, rowCount do
        local row = CreateFrame("Button", nil, frame)
        row:SetHeight(rowHeight)
        row:SetPoint("TOPLEFT", 0, -(i - 1) * rowHeight)
        row:SetPoint("RIGHT", scroll, "RIGHT", 0, 0)
        row:SetHighlightTexture("Interface\\QuestFrame\\UI-QuestTitleHighlight", "ADD")
        row.selected = row:CreateTexture(nil, "BACKGROUND")
        row.selected:SetTexture("Interface\\QuestFrame\\UI-QuestTitleHighlight")
        row.selected:SetBlendMode("ADD")
        row.selected:SetVertexColor(1, 0.82, 0, 0.6)
        row.selected:SetAllPoints()
        row.selected:Hide()
        makeRow(row)
        row:SetScript("OnClick", function(self)
            if self.item and onClick then
                onClick(self.item)
            end
        end)
        list.rows[i] = row
    end
    function list:Refresh()
        local offset = FauxScrollFrame_GetOffset(scroll)
        for i, row in ipairs(self.rows) do
            local item = self.items[i + offset]
            row.item = item
            if item then
                fill(row, item)
                LA.SetShown(row.selected, self.chosen ~= nil and self.chosen == item.key)
                row:Show()
            else
                row:Hide()
            end
        end
        FauxScrollFrame_Update(scroll, #self.items, rowCount, rowHeight)
    end
    scroll:SetScript("OnVerticalScroll", function(self, offset)
        FauxScrollFrame_OnVerticalScroll(self, offset, rowHeight, function() list:Refresh() end)
    end)
    function list:SetItems(items)
        self.items = items
        self:Refresh()
    end
    function list:Choose(key)
        self.chosen = key
        self:Refresh()
    end
    return list
end

-- A thin gold rule like the Dungeon Finder's separators.
function UI.Rule(parent)
    local rule = parent:CreateTexture(nil, "ARTWORK")
    rule:SetTexture("Interface\\LFGFrame\\UI-LFG-SEPARATOR")
    rule:SetTexCoord(0, 0.6640625, 0, 0.3125)
    rule:SetHeight(16)
    return rule
end
