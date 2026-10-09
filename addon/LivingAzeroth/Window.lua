-- SPDX-License-Identifier: GPL-2.0-or-later
-- Windows built from the game's own frame art at its native size, never stretched: the Quest Log's two-pane frame
-- for the manager (a list on the left, parchment on the right), and the Dungeon Finder's frame for small windows.
local _, LA = ...

local Window = {}
LA.Window = Window

local function Base(name, width, height, title, portrait)
    local window = CreateFrame("Frame", name, UIParent)
    window:SetSize(width, height)
    window:SetPoint("CENTER", 0, 40)
    window:SetFrameStrata("HIGH")
    window:SetToplevel(true)
    window:SetClampedToScreen(true)
    window:EnableMouse(true)
    window:SetMovable(true)
    window:Hide()
    table.insert(UISpecialFrames, name)

    window.portrait = window:CreateTexture(nil, "BACKGROUND")
    window.portrait:SetSize(60, 60)
    if portrait then
        SetPortraitToTexture(window.portrait, portrait)
    end
    window.title = window:CreateFontString(nil, "ARTWORK", "GameFontNormal")
    window.title:SetText(title)
    local close = CreateFrame("Button", nil, window, "UIPanelCloseButton")
    close:SetPoint("TOPRIGHT", 2, -8)

    local drag = CreateFrame("Frame", nil, window)
    drag:SetPoint("TOPLEFT", 70, -10)
    drag:SetPoint("TOPRIGHT", -30, -10)
    drag:SetHeight(24)
    drag:EnableMouse(true)
    drag:RegisterForDrag("LeftButton")
    drag:SetScript("OnDragStart", function() window:StartMoving() end)
    drag:SetScript("OnDragStop", function() window:StopMovingOrSizing() end)
    return window
end

-- The Quest Log's two-pane window, 682 x 447: window.top is the strip under the title, window.list the left pane
-- (305 x 335, its scroll track in the art beside it), window.detail a scroll frame over the parchment (298 x 333),
-- window.controls the three slots under the list and window.actions the bar under the parchment.
function Window.DualPane(name, title, portrait)
    local window = Base(name, 682, 447, title, portrait)
    window.portrait:SetPoint("TOPLEFT", 5, -6)
    window.title:SetPoint("TOP", 0, -15)

    local left = window:CreateTexture(nil, "BORDER")
    left:SetTexture("Interface\\QuestFrame\\UI-QuestLogDualPane-Left")
    left:SetSize(512, 445)
    left:SetPoint("TOPLEFT")
    left:SetTexCoord(0, 1, 0, 0.86914)
    local right = window:CreateTexture(nil, "BORDER")
    right:SetTexture("Interface\\QuestFrame\\UI-QuestLogDualPane-RIGHT")
    right:SetSize(170, 445)
    right:SetPoint("TOPRIGHT")
    right:SetTexCoord(0, 0.6640625, 0, 0.86914)

    window.top = CreateFrame("Frame", nil, window)
    window.top:SetPoint("TOPLEFT", 76, -38)
    window.top:SetPoint("TOPRIGHT", -32, -38)
    window.top:SetHeight(30)

    window.list = CreateFrame("Frame", nil, window)
    window.list:SetPoint("TOPLEFT", 19, -75)
    window.list:SetSize(305, 335)

    window.detail = CreateFrame("ScrollFrame", name .. "Detail", window, "UIPanelScrollFrameTemplate")
    window.detail:SetPoint("TOPRIGHT", -32, -77)
    window.detail:SetSize(298, 333)
    local bar = _G[name .. "DetailScrollBar"]
    bar:ClearAllPoints()
    bar:SetPoint("TOPLEFT", window.detail, "TOPRIGHT", 6, -13)
    bar:SetPoint("BOTTOMLEFT", window.detail, "BOTTOMRIGHT", 6, 14)

    window.controls = CreateFrame("Frame", nil, window)
    window.controls:SetPoint("BOTTOMLEFT", 18, 9)
    window.controls:SetSize(304, 26)
    window.actions = CreateFrame("Frame", nil, window)
    window.actions:SetPoint("BOTTOMRIGHT", -7, 9)
    window.actions:SetSize(340, 26)
    return window
end

-- The Dungeon Finder's window, 355 x 440: window.header is the dark band under the title (316 x 82), window.bar
-- the metal strip under it, window.content the paper (316 x 245) and window.footer the bottom bar.
function Window.Finder(name, title, portrait)
    local window = Base(name, 355, 440, title, portrait)
    window.portrait:SetPoint("TOPLEFT", 12, -7)
    window.title:SetPoint("TOP", 12, -17)

    local layout = window:CreateTexture(nil, "BACKGROUND", nil, 1)
    layout:SetTexture("Interface\\LFGFrame\\UI-LFG-FRAME")
    layout:SetSize(512, 512)
    layout:SetPoint("TOPLEFT")
    local paper = window:CreateTexture(nil, "BORDER")
    paper:SetTexture("Interface\\LFGFrame\\UI-LFG-BACKGROUND-QUESTPAPER")
    paper:SetSize(512, 256)
    paper:SetPoint("LEFT", 21, -64)

    window.header = CreateFrame("Frame", nil, window)
    window.header:SetPoint("TOPLEFT", 25, -38)
    window.header:SetSize(316, 82)
    window.bar = CreateFrame("Frame", nil, window)
    window.bar:SetPoint("TOPLEFT", 25, -122)
    window.bar:SetSize(316, 34)
    window.content = CreateFrame("Frame", nil, window)
    window.content:SetPoint("TOPLEFT", 25, -163)
    window.content:SetSize(316, 245)
    window.footer = CreateFrame("Frame", nil, window)
    window.footer:SetPoint("BOTTOMLEFT", 19, 9)
    window.footer:SetSize(326, 26)
    return window
end
