-- SPDX-License-Identifier: GPL-2.0-or-later
-- The manager window: one movable window with tabs along the bottom, as in the game's own windows. Each tab is
-- a page another file adds (roster, tactics, dialogue, guild).
local _, LA = ...

local Manager = {}
LA.Manager = Manager

local WIDTH, HEIGHT = 720, 500

local window = CreateFrame("Frame", "LivingAzerothManager", UIParent)
window:SetSize(WIDTH, HEIGHT)
window:SetPoint("CENTER", 0, 40)
window:SetFrameStrata("HIGH")
window:SetToplevel(true)
window:SetClampedToScreen(true)
window:EnableMouse(true)
window:SetMovable(true)
window:RegisterForDrag("LeftButton")
window:SetScript("OnDragStart", window.StartMoving)
window:SetScript("OnDragStop", window.StopMovingOrSizing)
window:SetBackdrop({ bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
    edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border", tile = true, tileSize = 32, edgeSize = 32,
    insets = { left = 11, right = 12, top = 12, bottom = 11 } })
window:Hide()
table.insert(UISpecialFrames, "LivingAzerothManager")
-- The dialog background is see-through; a dark layer keeps frames behind it from showing.
local shade = window:CreateTexture(nil, "BACKGROUND", nil)
shade:SetTexture("Interface\\Buttons\\WHITE8X8")
shade:SetVertexColor(0.03, 0.025, 0.02, 0.92)
shade:SetPoint("TOPLEFT", 11, -12)
shade:SetPoint("BOTTOMRIGHT", -12, 11)

local header = window:CreateTexture(nil, "ARTWORK")
header:SetTexture("Interface\\DialogFrame\\UI-DialogBox-Header")
header:SetSize(300, 64)
header:SetPoint("TOP", 0, 12)
local title = window:CreateFontString(nil, "OVERLAY", "GameFontNormal")
title:SetPoint("TOP", header, "TOP", 0, -14)
local close = CreateFrame("Button", nil, window, "UIPanelCloseButton")
close:SetPoint("TOPRIGHT", -6, -6)

local pages = {}
local tabs = {}

-- The area a page draws in.
function Manager.Content()
    return window
end

-- Adds a tab; build(page) makes its contents once, show(page) refreshes it each time it is shown.
function Manager.AddPage(name, build, show)
    local index = #pages + 1
    local page = CreateFrame("Frame", nil, window)
    page:SetPoint("TOPLEFT", 20, -32)
    page:SetPoint("BOTTOMRIGHT", -20, 18)
    page:Hide()
    page.tabName = name -- pages keep their own fields on the page; this one is the manager's
    page.build, page.show = build, show
    pages[index] = page

    local tab = CreateFrame("Button", "LivingAzerothManagerTab" .. index, window, "CharacterFrameTabButtonTemplate")
    tab:SetID(index)
    tab:SetText(name)
    PanelTemplates_TabResize(tab, 0)
    if index == 1 then
        tab:SetPoint("TOPLEFT", window, "BOTTOMLEFT", 14, 8)
    else
        tab:SetPoint("LEFT", tabs[index - 1], "RIGHT", -15, 0)
    end
    tab:SetScript("OnClick", function(self)
        Manager.Show(self:GetID())
    end)
    tabs[index] = tab
    PanelTemplates_SetNumTabs(window, #pages)
    return page
end

function Manager.Show(index)
    index = index or LivingAzerothDB.managerTab or 1
    if not pages[index] then
        index = 1
    end
    LivingAzerothDB.managerTab = index
    PanelTemplates_SetTab(window, index)
    window:Show() -- before the page refreshes, which only work on a visible page
    for i, page in ipairs(pages) do
        if i == index then
            if not page.built then
                page.built = true
                page.build(page)
            end
            title:SetText("Living Azeroth - " .. page.tabName)
            page:Show()
            if page.show then
                page.show(page)
            end
        else
            page:Hide()
        end
    end
end

function Manager.Toggle()
    if window:IsShown() then
        window:Hide()
    else
        Manager.Show()
    end
end

function Manager.IsShown(page)
    return window:IsShown() and page:IsShown()
end

-- Small building blocks shared by the pages.

function Manager.Heading(parent, text, x, y)
    local heading = parent:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    heading:SetPoint("TOPLEFT", x, y)
    heading:SetText(text)
    return heading
end

function Manager.Button(parent, text, width)
    local button = CreateFrame("Button", nil, parent, "UIPanelButtonTemplate")
    button:SetSize(width or 120, 22)
    button:SetText(text)
    return button
end

-- A small checkbox with a label and a grey hint under it.
function Manager.Check(parent, label, hint)
    local check = CreateFrame("CheckButton", nil, parent, "UICheckButtonTemplate")
    check:SetSize(24, 24)
    check.label = check:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
    check.label:SetPoint("TOPLEFT", check, "TOPRIGHT", 2, -2)
    check.label:SetText(label)
    if hint then
        check.hint = check:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
        check.hint:SetPoint("TOPLEFT", check.label, "BOTTOMLEFT", 0, -1)
        check.hint:SetText(hint)
    end
    return check
end

-- A dark inset panel, like the game's list backgrounds.
function Manager.Inset(parent)
    local inset = CreateFrame("Frame", nil, parent)
    inset:SetBackdrop({ bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
        edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border", edgeSize = 12,
        insets = { left = 3, right = 3, top = 3, bottom = 3 } })
    inset:SetBackdropColor(0, 0, 0, 0.55)
    inset:SetBackdropBorderColor(0.45, 0.38, 0.22)
    return inset
end
