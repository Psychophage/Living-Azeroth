-- SPDX-License-Identifier: GPL-2.0-or-later
-- Drawing surfaces from plain textures on whole screen pixels. The addon's windows are scaled so one unit is a
-- whole number of screen pixels (Draw.Fit); a box is then built from solid strips measured in pixels, with
-- rounded corners from a quarter-circle atlas shown at one texel per pixel, so its edges are sharp and meet exactly.
local _, LA = ...

local Draw = {}
LA.Draw = Draw

local Style = LA.Style
local WHITE = Style.WHITE
local CORNERS = Style.ART .. "corners"
local GRAIN = Style.ART .. "grain"
local ATLAS_W, ATLAS_H, CELL, MAX_RADIUS = 512, 128, 32, 16 -- a corner starts one texel into its cell

-- Screen pixels per unit inside a fitted frame, and one pixel in units.
Draw.ppu = 2
local px = 0.5

local function ScreenHeight()
    local resolution = ({ GetScreenResolutions() })[GetCurrentResolution() or 0] or GetCVar("gxResolution") or ""
    return tonumber(resolution:match("%d+x(%d+)")) or 768
end

local fitted = {}
local boxes = setmetatable({}, { __mode = "k" })

-- The scale, relative to UIParent, at which one unit is a whole number of pixels, close to the game's own size.
local function FitScale()
    local height = ScreenHeight()
    local natural = height * UIParent:GetEffectiveScale() / 768
    Draw.ppu = math.max(1, math.floor(natural + 0.6))
    px = 1 / Draw.ppu
    return Draw.ppu * 768 / height / UIParent:GetEffectiveScale()
end

-- Scales a frame (a window, the ring) so its units fall on whole pixels; it follows UI scale changes.
function Draw.Fit(frame)
    fitted[frame] = true
    frame:SetScale(FitScale())
end

-- One pixel (or n) in units, inside a fitted frame.
function Draw.Px(n)
    return (n or 1) * px
end

-- A length rounded to an even number of pixels, so it centres on whole pixels in an even-sized frame.
function Draw.Even(units)
    return math.floor(units / px / 2 + 0.5) * 2 * px
end

-- A length or offset rounded to whole pixels.
function Draw.Round(units)
    return math.floor(units / px + 0.5) * px
end

-- A length rounded up to whole pixels.
function Draw.Ceil(units)
    return math.ceil(units / px - 0.01) * px
end

-- Puts a moved frame back on whole pixels, anchored by its top left corner.
function Draw.Settle(frame)
    local left, top = frame:GetLeft(), frame:GetTop()
    if left and top then
        frame:ClearAllPoints()
        frame:SetPoint("TOPLEFT", UIParent, "BOTTOMLEFT", math.floor(left / px + 0.5) * px,
            math.floor(top / px + 0.5) * px)
    end
end

local function Refit()
    local scale = FitScale()
    for frame in pairs(fitted) do
        frame:SetScale(scale)
        Draw.Settle(frame)
    end
    for box in pairs(boxes) do
        box:Layout()
    end
end
local watcher = CreateFrame("Frame")
watcher:RegisterEvent("UI_SCALE_CHANGED")
watcher:RegisterEvent("DISPLAY_SIZE_CHANGED")
watcher:SetScript("OnEvent", Refit)
hooksecurefunc(UIParent, "SetScale", Refit)

-- Colours

local function Paint(texture, top, bottom)
    bottom = bottom or top
    texture:SetGradientAlpha("VERTICAL", bottom[1], bottom[2], bottom[3], bottom[4] or 1,
        top[1], top[2], top[3], top[4] or 1)
end
Draw.Paint = Paint

-- A colour pair { top, bottom } or a single colour, as top and bottom.
local function Pair(colour)
    if type(colour[1]) == "table" then
        return colour[1], colour[2] or colour[1]
    end
    return colour, colour
end

-- Boxes

local Box = {}
Box.__index = Box

local function Take(box, layer)
    layer = box.spec.layer or layer
    box.pool[layer] = box.pool[layer] or {}
    local pool = box.pool[layer]
    local n = (box.used[layer] or 0) + 1
    box.used[layer] = n
    local texture = pool[n]
    if not texture then
        texture = box.frame:CreateTexture(nil, layer)
        pool[n] = texture
    end
    texture:ClearAllPoints()
    texture:SetTexture(WHITE)
    texture:SetTexCoord(0, 1, 0, 1)
    texture:SetHorizTile(false)
    texture:SetVertTile(false)
    texture:SetBlendMode("BLEND")
    texture:SetAlpha(1)
    texture:Show()
    return texture
end

local SIDES = { "TOPLEFT", "TOPRIGHT", "BOTTOMLEFT", "BOTTOMRIGHT" }
local SIGN = { TOPLEFT = { 1, -1 }, TOPRIGHT = { -1, -1 }, BOTTOMLEFT = { 1, 1 }, BOTTOMRIGHT = { -1, 1 } }

-- Pieces whose colour runs from top to bottom remember the rows they cover, as a + b * height (b = 1 measures
-- from the bottom edge), and the layer's own rows, so each is painted with the colours at its own rows.
local function Shade(box, texture, top, bottom, o, y0, y0b, y1, y1b)
    if top == bottom then
        Paint(texture, top)
    else
        box.shades[#box.shades + 1] = { texture, top, bottom, o, y0, y0b, y1, y1b }
    end
end

local function Lerp(a, b, f)
    return { a[1] + (b[1] - a[1]) * f, a[2] + (b[2] - a[2]) * f, a[3] + (b[3] - a[3]) * f,
        (a[4] or 1) + ((b[4] or 1) - (a[4] or 1)) * f }
end

-- A corner piece size x size px at offset px in from the corner: a filled corner (ring 0) or its edge band
-- (ring = its width) of radius px, or a plain square when radius is 0.
local function Corner(box, layer, corner, offset, size, radius, ring, top, bottom)
    local texture = Take(box, layer)
    local sx, sy = SIGN[corner][1], SIGN[corner][2]
    texture:SetPoint(corner, box.frame, corner, sx * offset * px, sy * offset * px)
    texture:SetSize(size * px, size * px)
    if radius > 0 then
        texture:SetTexture(CORNERS)
        local u0, v0 = ((radius - 1) * CELL + 1) / ATLAS_W, (ring * CELL + 1) / ATLAS_H
        local u1, v1 = u0 + radius / ATLAS_W, v0 + radius / ATLAS_H
        if sx < 0 then
            u0, u1 = u1, u0
        end
        if sy > 0 then
            v0, v1 = v1, v0
        end
        texture:SetTexCoord(u0, u1, v0, v1)
    end
    if sy < 0 then
        Shade(box, texture, top, bottom, offset, offset, 0, offset + size, 0)
    else
        Shade(box, texture, top, bottom, offset, -(offset + size), 1, -offset, 1)
    end
end

-- A strip between two points, inset from the frame's edges in px.
local function Strip(box, layer, left, top, right, bottom)
    local texture = Take(box, layer)
    texture:SetPoint("TOPLEFT", box.frame, "TOPLEFT", left * px, -top * px)
    texture:SetPoint("BOTTOMRIGHT", box.frame, "BOTTOMRIGHT", -right * px, bottom * px)
    return texture
end

local function Radius(box, corner, offset)
    local spec = box.spec
    if spec.square and spec.square[corner] then
        return 0
    end
    local radius = spec.radius or 0
    if radius == "round" then
        radius = math.floor(math.min(box.frame:GetWidth(), box.frame:GetHeight()) / px / 2 + 0.5)
    end
    return math.min(MAX_RADIUS, math.max(0, radius - offset))
end

-- Paints the pieces that shade from top to bottom for the frame's current height.
function Box:Repaint()
    local height = self.frame:GetHeight() / px
    for _, shade in ipairs(self.shades) do
        local texture, top, bottom, o = shade[1], shade[2], shade[3], shade[4]
        local span = height - 2 * o
        local function At(y)
            return Lerp(top, bottom, span > 0 and math.min(1, math.max(0, (y - o) / span)) or 0)
        end
        Paint(texture, At(shade[5] + shade[6] * height), At(shade[7] + shade[8] * height))
    end
end

-- Rebuilds the box's textures from its spec.
function Box:Layout()
    local spec, frame = self.spec, self.frame
    for layer in pairs(self.used) do
        self.used[layer] = 0
    end
    self.shades = {}
    local offset = -(spec.outset or 0)
    for _, edge in ipairs(spec.edges or {}) do
        local width, top, bottom = edge[1], Pair(edge[2])
        local layer = edge.layer or "BORDER"
        local size = {}
        for _, corner in ipairs(SIDES) do
            local radius = Radius(self, corner, offset)
            if radius < width then
                radius = 0
            end
            size[corner] = radius > 0 and radius or width
            Corner(self, layer, corner, offset, size[corner], radius, width, top, bottom)
        end
        local w, o = width, offset
        local strip = Take(self, layer)
        strip:SetPoint("TOPLEFT", frame, "TOPLEFT", (o + size.TOPLEFT) * px, -o * px)
        strip:SetPoint("TOPRIGHT", frame, "TOPRIGHT", -(o + size.TOPRIGHT) * px, -o * px)
        strip:SetHeight(w * px)
        Shade(self, strip, top, bottom, o, o, 0, o + w, 0)
        strip = Take(self, layer)
        strip:SetPoint("BOTTOMLEFT", frame, "BOTTOMLEFT", (o + size.BOTTOMLEFT) * px, o * px)
        strip:SetPoint("BOTTOMRIGHT", frame, "BOTTOMRIGHT", -(o + size.BOTTOMRIGHT) * px, o * px)
        strip:SetHeight(w * px)
        Shade(self, strip, top, bottom, o, -(o + w), 1, -o, 1)
        strip = Take(self, layer)
        strip:SetPoint("TOPLEFT", frame, "TOPLEFT", o * px, -(o + size.TOPLEFT) * px)
        strip:SetPoint("BOTTOMLEFT", frame, "BOTTOMLEFT", o * px, (o + size.BOTTOMLEFT) * px)
        strip:SetWidth(w * px)
        Shade(self, strip, top, bottom, o, o + size.TOPLEFT, 0, -(o + size.BOTTOMLEFT), 1)
        strip = Take(self, layer)
        strip:SetPoint("TOPRIGHT", frame, "TOPRIGHT", -o * px, -(o + size.TOPRIGHT) * px)
        strip:SetPoint("BOTTOMRIGHT", frame, "BOTTOMRIGHT", -o * px, (o + size.BOTTOMRIGHT) * px)
        strip:SetWidth(w * px)
        Shade(self, strip, top, bottom, o, o + size.TOPRIGHT, 0, -(o + size.BOTTOMRIGHT), 1)
        offset = offset + width
    end
    if spec.fill then
        local top, bottom = Pair(spec.fill)
        local r = {}
        for _, corner in ipairs(SIDES) do
            r[corner] = Radius(self, corner, offset)
        end
        local upper = math.max(r.TOPLEFT, r.TOPRIGHT)
        local lower = math.max(r.BOTTOMLEFT, r.BOTTOMRIGHT)
        local o = offset
        local middle = Strip(self, "BACKGROUND", o, o + upper, o, o + lower)
        Shade(self, middle, top, bottom, o, o + upper, 0, -(o + lower), 1)
        if upper > 0 then
            local band = Take(self, "BACKGROUND")
            band:SetPoint("TOPLEFT", frame, "TOPLEFT", (o + r.TOPLEFT) * px, -o * px)
            band:SetPoint("TOPRIGHT", frame, "TOPRIGHT", -(o + r.TOPRIGHT) * px, -o * px)
            band:SetHeight(upper * px)
            Shade(self, band, top, bottom, o, o, 0, o + upper, 0)
        end
        if lower > 0 then
            local band = Take(self, "BACKGROUND")
            band:SetPoint("BOTTOMLEFT", frame, "BOTTOMLEFT", (o + r.BOTTOMLEFT) * px, o * px)
            band:SetPoint("BOTTOMRIGHT", frame, "BOTTOMRIGHT", -(o + r.BOTTOMRIGHT) * px, o * px)
            band:SetHeight(lower * px)
            Shade(self, band, top, bottom, o, -(o + lower), 1, -o, 1)
        end
        for _, corner in ipairs(SIDES) do
            if r[corner] > 0 then
                Corner(self, "BACKGROUND", corner, o, r[corner], r[corner], 0, top, bottom)
            end
        end
        if spec.grain then
            local grain = Strip(self, "BORDER", o, o + upper, o, o + lower)
            Paint(grain, { 1, 1, 1, spec.grain })
            grain:SetTexture(GRAIN, true)
            grain:SetHorizTile(true)
            grain:SetVertTile(true)
        end
    end
    for layer, pool in pairs(self.pool) do
        for i = (self.used[layer] or 0) + 1, #pool do
            pool[i]:Hide()
        end
    end
    self:Repaint()
end

-- Changes the box's look (a spec from Style.Surface) and redraws it.
function Box:Set(spec)
    self.spec = spec
    self:Layout()
end

function Box:SetShown(shown)
    for layer, pool in pairs(self.pool) do
        for i, texture in ipairs(pool) do
            LA.SetShown(texture, shown and i <= (self.used[layer] or 0))
        end
    end
end

-- Draws a box over a frame's whole area. spec:
--   radius   the outer corners' radius in px, or "round" for a circle (size the frame first);
--            square = { TOPLEFT = true, ... } keeps some corners square
--   outset   px the outermost edge starts outside the frame (for shadows)
--   edges    { { width px (1-3), colour or { top, bottom } }, ... } from the outside in
--   fill     colour or { top, bottom } inside the edges; grain = alpha of the grain laid over it
--   layer    a draw layer for all of it (HIGHLIGHT for a hover light); otherwise fill below, edges above
function Draw.Box(frame, spec)
    local box = setmetatable({ frame = frame, spec = spec, pool = {}, used = {} }, Box)
    boxes[box] = true
    box:Layout()
    frame:HookScript("OnSizeChanged", function()
        box:Layout()
    end)
    return box
end

-- A line one pixel (or width px) thick, in a colour or a { left/top, right/bottom } pair fading along it.
function Draw.Line(parent, colour, width, layer)
    local line = parent:CreateTexture(nil, layer or "ARTWORK")
    line:SetTexture(WHITE)
    line.thickness = width or 1
    function line:SetColour(c, fade)
        if fade then
            self:SetGradientAlpha("HORIZONTAL", c[1], c[2], c[3], c[4] or 1, fade[1], fade[2], fade[3], fade[4] or 1)
        else
            Paint(self, c)
        end
    end
    line:SetColour(type(colour[1]) == "table" and colour[1] or colour, type(colour[1]) == "table" and colour[2])
    -- Lay it between two points, across (horizontal) or down.
    function line:Across(anchor, x, y, toX)
        self:ClearAllPoints()
        self:SetPoint("TOPLEFT", anchor, "TOPLEFT", x, y)
        self:SetPoint("TOPRIGHT", anchor, "TOPRIGHT", toX or 0, y)
        self:SetHeight(self.thickness * px)
    end
    return line
end
