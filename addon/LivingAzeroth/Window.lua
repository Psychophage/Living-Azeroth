-- SPDX-License-Identifier: GPL-2.0-or-later
-- The addon's windows: a dark panel in the style's frame, a title band with the title in the middle and a close
-- button on the right, and a medal over the top left corner. window.body holds the contents. Drag the title band
-- to move a window; it opens where it was left, on whole pixels.
local _, LA = ...

local Window = {}
LA.Window = Window

local UI, Style, Draw = LA.UI, LA.Style, LA.Draw

local TITLE = 22 -- the title band's height
local MEDAL = 40 -- the medal's size
Window.PAD = 10 -- the space between the frame and the contents

-- The frame's thickness inside the window's edge, in units.
local function Rim()
    return Draw.Px(Style.Rim())
end

local function Top()
    return Rim() + TITLE + Window.PAD
end

local function Remember(window)
    Draw.Settle(window)
    LivingAzerothDB.places = LivingAzerothDB.places or {}
    LivingAzerothDB.places[window:GetName()] = { window:GetLeft(), window:GetTop() }
end

local function Restore(window)
    local saved = LivingAzerothDB.places and LivingAzerothDB.places[window:GetName()]
    window:ClearAllPoints()
    if saved then
        window:SetPoint("TOPLEFT", UIParent, "BOTTOMLEFT", saved[1], saved[2])
    else
        window:SetPoint("CENTER", UIParent, "CENTER", 0, 40)
    end
    Draw.Settle(window)
end

-- A window width x height in size. glyph is the medal's glyph (the bot mark by default).
function Window.Panel(name, width, height, title, glyph)
    local T = Style.T()
    local window = CreateFrame("Frame", name, UIParent)
    Draw.Fit(window)
    window:SetSize(width, height)
    window:SetFrameStrata("HIGH")
    window:SetToplevel(true)
    window:SetClampedToScreen(true)
    window:EnableMouse(true)
    window:SetMovable(true)
    window:Hide()
    table.insert(UISpecialFrames, name)
    UI.Surface(window, "window")

    -- The title band: drag it to move the window.
    local rim = Rim()
    local band = CreateFrame("Frame", nil, window)
    band:SetPoint("TOPLEFT", rim, -rim)
    band:SetPoint("TOPRIGHT", -rim, -rim)
    band:SetHeight(TITLE)
    UI.Surface(band, "band")
    -- Its lower edge: a gold rule over a dark line.
    local shade = band:CreateTexture(nil, "ARTWORK")
    shade:SetTexture(Style.WHITE)
    shade:SetPoint("BOTTOMLEFT")
    shade:SetPoint("BOTTOMRIGHT")
    shade:SetHeight(Draw.Px(1))
    shade:SetVertexColor(0, 0, 0, 0.6)
    local rule = band:CreateTexture(nil, "ARTWORK")
    rule:SetTexture(Style.WHITE)
    rule:SetPoint("BOTTOMLEFT", shade, "TOPLEFT")
    rule:SetPoint("BOTTOMRIGHT", shade, "TOPRIGHT")
    rule:SetHeight(Draw.Px(1))
    UI.Tint(rule, T.rule)
    window.title = UI.Text(band, "title")
    window.title:SetPoint("CENTER", 0, Draw.Px(1))
    window.title:SetJustifyH("CENTER")
    window.title:SetText(title)
    window.close = UI.CloseButton(band, function() window:Hide() end)
    local margin = (TITLE - window.close:GetHeight()) / 2
    window.close:SetPoint("TOPRIGHT", -margin, -margin)
    band:EnableMouse(true)
    band:RegisterForDrag("LeftButton")
    band:SetScript("OnDragStart", function() window:StartMoving() end)
    band:SetScript("OnDragStop", function()
        window:StopMovingOrSizing()
        Remember(window)
    end)
    window.bar = band

    -- The medal over the top left corner.
    local medal = UI.Icon(window, MEDAL, "medal")
    medal:SetPoint("CENTER", window, "TOPLEFT", 12, -10)
    medal:SetFrameLevel(band:GetFrameLevel() + 4)
    medal:Set(glyph or "bot", "gold")
    window.icon = medal

    window.body = CreateFrame("Frame", nil, window)
    window.body:SetPoint("TOPLEFT", Window.PAD, -Top())
    window.body:SetPoint("BOTTOMRIGHT", -Window.PAD, Window.PAD)

    window:SetScript("OnShow", function(self)
        -- In front of the window it was opened from. A window shown the moment it is made is only raised a frame
        -- later.
        self:Raise()
        self:SetScript("OnUpdate", function()
            self:SetScript("OnUpdate", nil)
            self:Raise()
        end)
        if not self.placed then
            self.placed = true
            Restore(self)
        end
        PlaySound("igCharacterInfoOpen")
    end)
    window:SetScript("OnHide", function()
        PlaySound("igCharacterInfoClose")
    end)
    return window
end

-- The body's size for a window of this size.
function Window.BodySize(width, height)
    return width - 2 * Window.PAD, height - Top() - Window.PAD
end

-- Window places from before windows were drawn on whole pixels are in other units: forget them.
LA.On("loaded", function()
    LivingAzerothDB.windows = nil
end)
