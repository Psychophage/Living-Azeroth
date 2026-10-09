-- SPDX-License-Identifier: GPL-2.0-or-later
-- Windows in the style of the game's Dungeon Finder, at any size: its frame art cut into pieces (corners kept,
-- edges stretched), with the round portrait, title bar, close box, a dark header band, a metal bar for controls,
-- the brown paper for content, and a bottom bar for buttons.
local _, LA = ...

local Window = {}
LA.Window = Window

local FRAME = "Interface\\LFGFrame\\UI-LFG-FRAME" -- 512x512, laid out in its top-left 355x440
local PAPER = "Interface\\LFGFrame\\UI-LFG-BACKGROUND-QUESTPAPER"

-- Where the art's bands are, in texture pixels.
local TOP = 163 -- portrait, title bar, header band and the metal bar
local BOTTOM = 32 -- the button bar
local LEFT, RIGHT = 25, 14 -- side borders
local TOP_LEFT, TOP_RIGHT = 80, 35 -- the portrait corner and the close-box corner
local ART_W, ART_H = 355, 440

-- Heights inside the top band, from the window's top edge.
Window.HEADER_TOP, Window.HEADER_BOTTOM = 38, 120 -- the dark blue header
Window.BAR_TOP, Window.BAR_BOTTOM = 122, 156 -- the metal bar under it
Window.CONTENT_TOP = TOP + 4

local function Piece(frame, layer, left, top, right, bottom)
    local texture = frame:CreateTexture(nil, layer)
    texture:SetTexture(FRAME)
    texture:SetTexCoord(left / 512, right / 512, top / 512, bottom / 512)
    return texture
end

-- A window of width x height with a title and a portrait texture; it is movable by its title bar and closes
-- with Escape. window.content is the paper area; window.header and window.bar are the bands above it;
-- window:Buttons(...) fills the bottom bar.
function Window.Create(name, width, height, title, portrait)
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

    -- The paper first, under the frame, which is see-through where the paper shows.
    local paper = window:CreateTexture(nil, "BACKGROUND", nil, -1)
    paper:SetTexture(PAPER)
    paper:SetTexCoord(0, 325 / 512, 0, 1)
    paper:SetPoint("TOPLEFT", LEFT - 4, -TOP + 6)
    paper:SetPoint("BOTTOMRIGHT", -RIGHT + 4, BOTTOM - 6)

    local middleW = width - TOP_LEFT - TOP_RIGHT
    local middleH = height - TOP - BOTTOM
    -- { art left, top, right, bottom; anchor point, x, y; width, height }
    local pieces = {
        -- top band: the portrait corner, a plain stretch of the bands (clear of the art's one streak), the close box
        { 0, 0, TOP_LEFT, TOP, "TOPLEFT", 0, 0, TOP_LEFT, TOP },
        { 90, 0, 260, TOP, "TOPLEFT", TOP_LEFT, 0, middleW, TOP },
        { ART_W - TOP_RIGHT, 0, ART_W, TOP, "TOPRIGHT", 0, 0, TOP_RIGHT, TOP },
        -- the sides
        { 0, TOP, LEFT, 408, "TOPLEFT", 0, -TOP, LEFT, middleH },
        { ART_W - RIGHT, TOP, ART_W, 408, "TOPRIGHT", 0, -TOP, RIGHT, middleH },
        -- the bottom bar: its two ends (each with a whole slot end), and a plain slot middle stretched between
        { 0, 408, 30, ART_H, "BOTTOMLEFT", 0, 0, 30, BOTTOM },
        { 30, 408, 125, ART_H, "BOTTOMLEFT", 30, 0, width - 30 - 20, BOTTOM },
        { ART_W - 20, 408, ART_W, ART_H, "BOTTOMRIGHT", 0, 0, 20, BOTTOM },
    }
    for _, p in ipairs(pieces) do
        local texture = Piece(window, "BORDER", p[1], p[2], p[3], p[4])
        texture:SetPoint(p[5], p[6], p[7])
        texture:SetSize(p[8], p[9])
    end

    window.portrait = window:CreateTexture(nil, "BACKGROUND")
    window.portrait:SetSize(60, 60)
    window.portrait:SetPoint("TOPLEFT", 10, -5)
    if portrait then
        SetPortraitToTexture(window.portrait, portrait)
    end

    window.title = window:CreateFontString(nil, "ARTWORK", "GameFontNormal")
    window.title:SetPoint("TOP", TOP_LEFT / 2 - 4, -17)
    window.title:SetText(title)

    local close = CreateFrame("Button", nil, window, "UIPanelCloseButton")
    close:SetPoint("TOPRIGHT", 2, -8)

    -- Dragging by the title bar.
    local drag = CreateFrame("Frame", nil, window)
    drag:SetPoint("TOPLEFT", TOP_LEFT, -10)
    drag:SetPoint("TOPRIGHT", -TOP_RIGHT, -10)
    drag:SetHeight(26)
    drag:EnableMouse(true)
    drag:RegisterForDrag("LeftButton")
    drag:SetScript("OnDragStart", function() window:StartMoving() end)
    drag:SetScript("OnDragStop", function() window:StopMovingOrSizing() end)

    window.header = CreateFrame("Frame", nil, window)
    window.header:SetPoint("TOPLEFT", LEFT, -Window.HEADER_TOP)
    window.header:SetPoint("BOTTOMRIGHT", window, "TOPRIGHT", -RIGHT, -Window.HEADER_BOTTOM)
    window.bar = CreateFrame("Frame", nil, window)
    window.bar:SetPoint("TOPLEFT", LEFT, -Window.BAR_TOP)
    window.bar:SetPoint("BOTTOMRIGHT", window, "TOPRIGHT", -RIGHT, -Window.BAR_BOTTOM)
    window.content = CreateFrame("Frame", nil, window)
    window.content:SetPoint("TOPLEFT", LEFT + 6, -Window.CONTENT_TOP)
    window.content:SetPoint("BOTTOMRIGHT", -RIGHT - 6, BOTTOM + 4)
    window.footer = CreateFrame("Frame", nil, window)
    window.footer:SetPoint("BOTTOMLEFT", 16, 8)
    window.footer:SetPoint("TOPRIGHT", window, "BOTTOMRIGHT", -8, BOTTOM - 6)
    return window
end

-- A button that sits in a bottom bar, sized like the Dungeon Finder's.
function Window.FooterButton(parent, text, width)
    local button = CreateFrame("Button", nil, parent, "UIPanelButtonTemplate")
    button:SetSize(width or 112, 22)
    button:SetText(text)
    return button
end

-- A heading in the Dungeon Finder's gold title font.
function Window.Heading(parent, text)
    local heading = parent:CreateFontString(nil, "OVERLAY", "QuestTitleFontBlackShadow")
    heading:SetText(text)
    heading:SetJustifyH("LEFT")
    return heading
end
