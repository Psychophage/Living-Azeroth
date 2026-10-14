-- SPDX-License-Identifier: GPL-2.0-or-later
-- Building blocks for the addon's windows: surfaces drawn by Draw.lua in Style.lua's recipes, text, buttons,
-- chips, switches, sliders, icons, portraits and scrolling lists. Sizes are units of a fitted window.
local _, LA = ...

local UI = {}
LA.UI = UI

local Style, Draw = LA.Style, LA.Draw
local ART, WHITE = Style.ART, Style.WHITE

UI.BUTTON_HEIGHT = 18

-- Classes

local CLASS_CIRCLES = "Interface\\TargetingFrame\\UI-Classes-Circles"
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

function UI.ClassName(class)
    local token = CLASSES[class or 0]
    return token and LOCALIZED_CLASS_NAMES_MALE[token] or ""
end

-- Sets a texture to a round class icon.
function UI.SetClassIcon(texture, class)
    local token = CLASSES[class or 0]
    texture:SetTexture(CLASS_CIRCLES)
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

-- Surfaces

local function Tint(texture, colour)
    texture:SetVertexColor(colour[1], colour[2], colour[3], colour[4] or 1)
end
UI.Tint = Tint

local function TextColour(text, colour)
    text:SetTextColor(colour[1], colour[2], colour[3])
end

-- Older names for surface kinds.
local ALIAS = { inset = "well", chip = "plain", chipOn = "chosen", box = "field" }

-- A surface drawn over a frame's whole area in one of Style's recipes ("well", "row", "rowOn", "plain",
-- "chosen", "field", "tab", "tabOn", ...). surface:Set(kind) changes it; surface.kind is the current one.
function UI.Surface(frame, kind)
    kind = kind or "well"
    local box = Draw.Box(frame, Style.Surface(ALIAS[kind] or kind))
    box.kind = kind
    local set = box.Set
    function box:Set(name)
        if name ~= self.kind then
            self.kind = name
            set(self, Style.Surface(ALIAS[name] or name))
        end
    end
    frame.surface = box
    return box
end

-- A soft light over a button while the mouse is on it.
local function HoverLight(button, radius)
    return Draw.Box(button, { radius = radius or 3, fill = Style.C("#ffe8b0", 0.07), layer = "HIGHLIGHT" })
end
UI.HoverLight = HoverLight

-- A frame with a sunken well: the darker panels inside a window.
function UI.Inset(parent)
    local inset = CreateFrame("Frame", nil, parent)
    UI.Surface(inset, "well")
    return inset
end

-- A one-pixel line in the divider colour; anchor its ends.
function UI.Divider(parent)
    local line = parent:CreateTexture(nil, "ARTWORK")
    line:SetTexture(WHITE)
    Tint(line, Style.T().divider)
    line:SetHeight(Draw.Px(1))
    return line
end

-- Text

-- A text in one of the style's kinds ("title", "heading", "name", "label", "value", "body", "small", "button").
function UI.Text(parent, kind, width)
    local text = parent:CreateFontString(nil, "OVERLAY")
    text:SetFontObject(Style.Font(kind or "body"))
    text:SetJustifyH("LEFT")
    text:SetJustifyV("TOP")
    if width then
        text:SetWidth(width)
    end
    return text
end

-- A section heading: gold text over a rule that fades out to the right. Anchor its left and right; it is 16 tall.
function UI.Heading(parent, label)
    local heading = CreateFrame("Frame", nil, parent)
    heading:SetHeight(16)
    heading.text = UI.Text(heading, "heading")
    heading.text:SetPoint("BOTTOMLEFT", 0, 4)
    heading.text:SetText(label or "")
    heading.note = UI.Text(heading, "small")
    heading.note:SetPoint("BOTTOMRIGHT", 0, 4)
    heading.note:SetJustifyH("RIGHT")
    local rule = heading:CreateTexture(nil, "ARTWORK")
    rule:SetTexture(WHITE)
    rule:SetPoint("BOTTOMLEFT")
    rule:SetPoint("BOTTOMRIGHT")
    rule:SetHeight(Draw.Px(1))
    local colour = Style.T().rule
    rule:SetGradientAlpha("HORIZONTAL", colour[1], colour[2], colour[3], 0.9, colour[1], colour[2], colour[3], 0.08)
    heading.rule = rule
    function heading:SetText(value)
        self.text:SetText(value)
    end
    function heading:SetNote(value)
        self.note:SetText(value or "")
    end
    return heading
end

-- Icons

local CREAM = Style.C("#f7e8c2")

-- An icon from the addon's glyphs: "tile" (a coloured tile with a cream glyph, for orders and sets), "medal"
-- (a gold-rimmed coin, for roles and the bot mark) or "plain" (the glyph alone, in a colour).
function UI.Icon(parent, size, look)
    local icon = CreateFrame("Frame", nil, parent)
    icon:SetSize(size, size)
    icon.look = look or "tile"
    if icon.look ~= "plain" then
        icon.base = icon:CreateTexture(nil, "ARTWORK")
        icon.base:SetAllPoints()
        if icon.look == "medal" then
            icon.base:SetTexture(ART .. "medal")
        end
    end
    icon.glyph = icon:CreateTexture(nil, "OVERLAY")
    local share = icon.look == "tile" and 0.7 or icon.look == "medal" and 0.62 or 1
    local glyph = icon.look == "plain" and size or Draw.Even(size * share)
    icon.glyph:SetSize(glyph, glyph)
    icon.glyph:SetPoint("CENTER")
    function icon:Set(glyph, tone)
        self.glyph:SetTexture(ART .. "glyph-" .. glyph)
        if self.look == "tile" then
            self.base:SetTexture(ART .. "tile-" .. (tone or "gold"))
            Tint(self.glyph, CREAM)
        elseif self.look == "medal" then
            Tint(self.glyph, (Style.TONES[tone] or Style.TONES.gold).light)
        else
            Tint(self.glyph, type(tone) == "table" and tone or Style.gold)
        end
    end
    -- The current order's tile: a gold edge with a soft glow around it.
    function icon:SetActive(active)
        if active and not self.glow then
            self.glow = self:CreateTexture(nil, "OVERLAY")
            self.glow:SetTexture(ART .. "tile-glow")
            self.glow:SetBlendMode("ADD")
            self.glow:SetPoint("CENTER")
            self.glow:SetSize(self:GetWidth() * 2, self:GetHeight() * 2)
        end
        if self.glow then
            LA.SetShown(self.glow, active)
        end
    end
    -- Greyed, for an order or set that cannot be used.
    function icon:SetDim(dim)
        if self.base then
            self.base:SetDesaturated(dim)
        end
        self.glyph:SetDesaturated(dim)
        self:SetAlpha(dim and 0.5 or 1)
    end
    return icon
end

-- A round portrait in a gold ring: the character's face when they are in the group, otherwise their class.
function UI.Portrait(parent, size)
    local portrait = CreateFrame("Frame", nil, parent)
    portrait:SetSize(size, size)
    portrait.face = portrait:CreateTexture(nil, "ARTWORK")
    portrait.face:SetPoint("CENTER")
    local face = Draw.Even(size * 104 / 128)
    portrait.face:SetSize(face, face)
    portrait.ring = portrait:CreateTexture(nil, "OVERLAY")
    portrait.ring:SetTexture(ART .. "portrait-ring")
    portrait.ring:SetAllPoints()
    function portrait:SetCharacter(guid, class)
        local unit = UI.UnitFor(guid)
        if unit and UnitExists(unit) and UnitIsVisible(unit) then
            SetPortraitTexture(self.face, unit)
            self.face:SetTexCoord(0, 1, 0, 1)
        else
            UI.SetClassIcon(self.face, class)
        end
    end
    function portrait:SetClass(class)
        UI.SetClassIcon(self.face, class)
    end
    return portrait
end

-- Buttons

local PAD = 9 -- space each side of a button's label
local GLYPH = 10 -- a glyph beside a label

-- Lays out a button's label (and glyph) in the middle on whole pixels, nudged down a pixel while pressed.
local function Press(button, down)
    local x = down and Draw.Px(1) or 0
    local glyphWidth = button.glyphIcon and button.glyphIcon:GetWidth() + 4 or 0
    local left = Draw.Even(button:GetWidth() - glyphWidth - button.text:GetStringWidth()) / 2
    button.text:ClearAllPoints()
    button.text:SetPoint("LEFT", left + glyphWidth + x, -x)
    if button.glyphIcon then
        button.glyphIcon:ClearAllPoints()
        button.glyphIcon:SetPoint("LEFT", left + x, -x)
    end
end

-- The width a button needs for its label.
local function Needed(button, padding)
    local glyphWidth = button.glyphIcon and button.glyphIcon:GetWidth() + 4 or 0
    return math.ceil(button.text:GetStringWidth() + glyphWidth + 2 * (padding or PAD))
end

local function ButtonText(button, label, glyph)
    button.text = button:CreateFontString(nil, "OVERLAY")
    button.text:SetFontObject(Style.Font("button"))
    button.text:SetText(label)
    if glyph then
        button.glyphIcon = UI.Icon(button, GLYPH, "plain")
        button.glyphIcon:Set(glyph, Style.gold)
    end
    Press(button, false)
    button:HookScript("OnMouseDown", function(self)
        if self:IsEnabled() == 1 then
            Press(self, true)
            if self.downKind then
                self.surface:Set(self.downKind)
            end
        end
    end)
    local function Up(self)
        Press(self, false)
        if self.downKind and self:IsEnabled() == 1 then
            self.surface:Set(self.upKind)
        end
    end
    button:HookScript("OnMouseUp", Up)
    button:HookScript("OnHide", Up)
    -- A button is at least as wide as its label needs, and at least its given width.
    function button:Fit()
        self:SetWidth(math.max(self.minWidth or 0, Needed(self, self.padding)))
    end
    button:HookScript("OnSizeChanged", function(self) Press(self, false) end)
    function button:SetText(value)
        self.text:SetText(value)
        self:Fit()
        Press(self, false)
    end
    function button:GetText()
        return self.text:GetText()
    end
    function button:FitText(padding)
        self.padding = padding and padding / 2 or nil
        self.minWidth = nil
        self:Fit()
    end
end

-- Enabling and disabling greys the button, as the game's buttons do. paint(self, usable) does the rest.
local function Usable(button, paint)
    local enable, disable = button.Enable, button.Disable
    function button:Enable()
        enable(self)
        paint(self, true)
    end
    function button:Disable()
        disable(self)
        paint(self, false)
    end
    function button:SetUsable(usable)
        if usable then
            self:Enable()
        else
            self:Disable()
        end
    end
end

-- A button: red with a gold rim for main actions, or style "plain" for the quieter dark one. It grows to fit its
-- label; width is the least it is.
function UI.Button(parent, label, width, style, glyph)
    local button = CreateFrame("Button", nil, parent)
    button:SetHeight(UI.BUTTON_HEIGHT)
    button.upKind = style == "plain" and "plain" or "button"
    button.downKind = style == "plain" and "plainDown" or "buttonDown"
    UI.Surface(button, button.upKind)
    HoverLight(button)
    button.minWidth = width
    ButtonText(button, label, glyph)
    local textColour = style == "plain" and Style.T().text or Style.gold
    TextColour(button.text, textColour)
    button:Fit()
    Usable(button, function(self, usable)
        self.surface:Set(usable and self.upKind or "off")
        TextColour(self.text, usable and textColour or Style.T().faint)
        if self.glyphIcon then
            self.glyphIcon:SetDim(not usable)
        end
    end)
    return button
end

-- The square red close button with a gold cross, for title bars.
function UI.CloseButton(parent, onClick)
    local button = CreateFrame("Button", nil, parent)
    button:SetSize(16, 16)
    UI.Surface(button, "close")
    HoverLight(button)
    local cross = UI.Icon(button, 10, "plain")
    cross:SetPoint("CENTER")
    cross:Set("close", Style.gold)
    button:HookScript("OnMouseDown", function(self)
        self.surface:Set("closeDown")
        cross:SetPoint("CENTER", Draw.Px(1), -Draw.Px(1))
    end)
    button:HookScript("OnMouseUp", function(self)
        self.surface:Set("close")
        cross:SetPoint("CENTER")
    end)
    button:SetScript("OnClick", onClick)
    return button
end

-- A chip: a button that shows whether it is chosen (gold rim and text), for choices and targets.
function UI.Chip(parent, label, width, glyph)
    local chip = CreateFrame("Button", nil, parent)
    chip:SetHeight(UI.BUTTON_HEIGHT)
    UI.Surface(chip, "plain")
    HoverLight(chip)
    chip.minWidth = width
    chip.padding = 7
    ButtonText(chip, label, glyph)
    chip:Fit()
    function chip:SetOn(on)
        self.on = on
        self.surface:Set(on and "chosen" or "plain")
        TextColour(self.text, on and Style.gold or Style.T().text)
    end
    Usable(chip, function(self, usable)
        self:SetAlpha(usable and 1 or 0.45)
    end)
    chip:SetOn(false)
    return chip
end

-- A row of chips of which one is chosen. options: { { id = ..., label = ... } }; onPick(id) when one is clicked.
-- Each chip is as wide as its label needs; what is left of the width is shared out evenly.
function UI.Segment(parent, options, width, onPick)
    local segment = CreateFrame("Frame", nil, parent)
    segment:SetSize(width, UI.BUTTON_HEIGHT)
    segment.chips = {}
    local gap = 3
    local spare = width - (#options - 1) * gap
    for i, option in ipairs(options) do
        segment.chips[i] = UI.Chip(segment, option.label)
        spare = spare - segment.chips[i]:GetWidth()
    end
    local extra = math.max(0, math.floor(spare / #options))
    local x = 0
    for i, option in ipairs(options) do
        local chip = segment.chips[i]
        chip:SetWidth(chip:GetWidth() + extra)
        chip:SetPoint("LEFT", x, 0)
        x = x + chip:GetWidth() + gap
        chip.id = option.id
        chip:SetScript("OnClick", function()
            segment:Select(option.id)
            onPick(option.id)
        end)
    end
    function segment:Select(id)
        for _, chip in ipairs(self.chips) do
            chip:SetOn(chip.id == id)
        end
    end
    function segment:SetUsable(usable)
        for _, chip in ipairs(self.chips) do
            chip:SetUsable(usable)
        end
    end
    return segment
end

-- Switches

local BOX = 11 -- a check box's side

-- A check box with a label and a grey hint under it, width wide; its height follows the text.
-- SetMixed shows a dash in the box when the bots it covers differ. onToggle(self, checked) after a click.
function UI.Check(parent, label, hint, width)
    width = width or 240
    local check = CreateFrame("Button", nil, parent)
    check:SetWidth(width)
    local box = CreateFrame("Frame", nil, check)
    box:SetSize(BOX, BOX)
    box:SetPoint("TOPLEFT", 0, -1)
    UI.Surface(box, "field")
    check.box = box
    local light = check:CreateTexture(nil, "HIGHLIGHT")
    light:SetTexture(WHITE)
    light:SetPoint("TOPLEFT", box, "TOPLEFT", Draw.Px(1), -Draw.Px(1))
    light:SetPoint("BOTTOMRIGHT", box, "BOTTOMRIGHT", -Draw.Px(1), Draw.Px(1))
    light:SetVertexColor(1, 0.85, 0.4, 0.18)
    check.mark = UI.Icon(box, BOX, "plain")
    check.mark:SetPoint("CENTER")
    check.mark:Set("check", Style.gold)
    local textWidth = width - BOX - 6
    check.label = UI.Text(check, "label", textWidth)
    check.label:SetPoint("TOPLEFT", box, "TOPRIGHT", 6, 0)
    check.label:SetText(label)
    if hint then
        check.hint = UI.Text(check, "small", textWidth)
        check.hint:SetPoint("TOPLEFT", check.label, "BOTTOMLEFT", 0, -2)
        check.hint:SetText(hint)
    end
    -- A dash in the box when the things it covers differ.
    check.dash = box:CreateTexture(nil, "OVERLAY")
    check.dash:SetTexture(WHITE)
    check.dash:SetSize(Draw.Px(10), Draw.Px(2))
    check.dash:SetPoint("CENTER")
    Tint(check.dash, Style.gold)
    local height = 1 + check.label:GetStringHeight() + (hint and check.hint:GetStringHeight() + 2 or 0)
    check:SetHeight(Draw.Ceil(math.max(BOX + 2, height)))
    local function Mark(self)
        LA.SetShown(self.mark, self.checked and not self.mixed)
        LA.SetShown(self.dash, self.mixed)
    end
    function check:SetChecked(on)
        self.checked = on and true or false
        Mark(self)
    end
    function check:GetChecked()
        return self.checked
    end
    function check:SetMixed(mixed)
        self.mixed = mixed and true or false
        Mark(self)
    end
    Usable(check, function(self, usable)
        self:SetAlpha(usable and 1 or 0.45)
    end)
    check:SetScript("OnClick", function(self)
        self:SetChecked(not self.checked)
        PlaySound(self.checked and "igMainMenuOptionCheckBoxOn" or "igMainMenuOptionCheckBoxOff")
        if self.onToggle then
            self.onToggle(self, self.checked)
        end
    end)
    check:SetChecked(false)
    return check
end

-- A round radio button. With a label (and hint) it is width wide and the label sits to its right; without, it is
-- a small click area round the dot.
function UI.Radio(parent, label, hint, width)
    width = width or 200
    local radio = CreateFrame("Button", nil, parent)
    local RING = BOX + 1
    local ring = CreateFrame("Frame", nil, radio)
    ring:SetSize(RING, RING)
    UI.Surface(ring, "radio")
    radio.ring = ring
    local dot = CreateFrame("Frame", nil, ring)
    dot:SetSize(6, 6)
    dot:SetPoint("CENTER")
    UI.Surface(dot, "dot")
    radio.dot = dot
    local light = radio:CreateTexture(nil, "HIGHLIGHT")
    light:SetTexture(ART .. "dot")
    light:SetPoint("CENTER", ring)
    light:SetSize(BOX - 2, BOX - 2)
    light:SetVertexColor(1, 0.85, 0.4, 0.25)
    if label then
        radio:SetWidth(width)
        ring:SetPoint("TOPLEFT", 0, -1)
        radio.label = UI.Text(radio, "label", width - RING - 6)
        radio.label:SetPoint("TOPLEFT", ring, "TOPRIGHT", 6, 0)
        radio.label:SetText(label)
        local height = 1 + radio.label:GetStringHeight()
        if hint then
            radio.hint = UI.Text(radio, "small", width - RING - 6)
            radio.hint:SetPoint("TOPLEFT", radio.label, "BOTTOMLEFT", 0, -2)
            radio.hint:SetText(hint)
            height = height + radio.hint:GetStringHeight() + 2
        end
        radio:SetHeight(Draw.Ceil(math.max(BOX + 2, height)))
    else
        radio:SetSize(BOX + 10, BOX + 8)
        ring:SetPoint("CENTER")
    end
    function radio:SetChecked(on)
        LA.SetShown(self.dot, on)
    end
    radio:SetChecked(false)
    return radio
end

-- A slider: a sunken track with a gold handle. onChange(value) as it moves.
function UI.Slider(parent, low, high, step, width, onChange)
    local slider = CreateFrame("Slider", nil, parent)
    slider:SetOrientation("HORIZONTAL")
    slider:SetSize(width, 14)
    slider:SetMinMaxValues(low, high)
    slider:SetValueStep(step)
    slider:EnableMouseWheel(true)
    local track = CreateFrame("Frame", nil, slider)
    track:SetPoint("LEFT")
    track:SetPoint("RIGHT")
    track:SetHeight(5)
    UI.Surface(track, "field")
    slider:SetThumbTexture(WHITE)
    local thumbTexture = slider:GetThumbTexture()
    thumbTexture:SetSize(7, 14)
    thumbTexture:SetAlpha(0)
    -- The handle drawn over the game's (invisible) thumb, on whole pixels.
    local thumb = CreateFrame("Frame", nil, slider)
    thumb:SetSize(7, 14)
    thumb:SetFrameLevel(track:GetFrameLevel() + 2)
    UI.Surface(thumb, "thumb")
    local function Place(value)
        local share = high > low and (value - low) / (high - low) or 0
        thumb:SetPoint("LEFT", Draw.Round(share * (width - 7)), 0)
    end
    Place(low)
    slider:SetScript("OnValueChanged", function(self, value)
        Place(value)
        value = math.floor(value / step + 0.5) * step
        if value ~= self.last then
            self.last = value
            if not self.quiet then
                onChange(value)
            end
        end
    end)
    slider:SetScript("OnMouseWheel", function(self, delta)
        self:SetValue(self:GetValue() + delta * step)
    end)
    -- Sets the value without telling onChange.
    function slider:SetQuietly(value)
        self.quiet = true
        self:SetValue(value)
        self.last = value
        self.quiet = false
    end
    return slider
end

-- Text entry: a sunken field, one line or several (which scroll).
function UI.EditBox(parent, width, height, multiLine)
    local holder = CreateFrame("Frame", nil, parent)
    holder:SetSize(width, height)
    UI.Surface(holder, "field")
    local box
    if multiLine then
        local scroll = CreateFrame("ScrollFrame", nil, holder)
        scroll:SetPoint("TOPLEFT", 5, -4)
        scroll:SetPoint("BOTTOMRIGHT", -5, 4)
        box = CreateFrame("EditBox", nil, scroll)
        box:SetMultiLine(true)
        box:SetWidth(width - 10)
        box:SetHeight(height - 8)
        scroll:SetScrollChild(box)
        scroll:EnableMouseWheel(true)
        scroll:SetScript("OnMouseWheel", function(self, delta)
            local range = self:GetVerticalScrollRange()
            self:SetVerticalScroll(math.max(0, math.min(range, self:GetVerticalScroll() - delta * 14)))
        end)
        box:SetScript("OnCursorChanged", function(_, _, y, _, lineHeight)
            local top = scroll:GetVerticalScroll()
            y = -y
            if y < top then
                scroll:SetVerticalScroll(y)
            elseif y + lineHeight > top + scroll:GetHeight() then
                scroll:SetVerticalScroll(y + lineHeight - scroll:GetHeight())
            end
        end)
        holder:EnableMouse(true)
        holder:SetScript("OnMouseDown", function() box:SetFocus() end)
    else
        box = CreateFrame("EditBox", nil, holder)
        box:SetPoint("TOPLEFT", 5, 0)
        box:SetPoint("BOTTOMRIGHT", -5, 0)
    end
    box:SetAutoFocus(false)
    box:SetFontObject(Style.Font("body"))
    box:SetTextInsets(0, 0, 0, 0)
    box:SetScript("OnEscapePressed", function(self) self:ClearFocus() end)
    box:SetScript("OnEditFocusGained", function() holder.surface:Set("fieldFocus") end)
    box:SetScript("OnEditFocusLost", function() holder.surface:Set("field") end)
    holder.box = box
    function holder:SetText(text)
        self.box:SetText(text or "")
        self.box:SetCursorPosition(0)
    end
    function holder:GetText()
        return self.box:GetText()
    end
    return holder
end

-- Scrolling

local BAR = 4 -- the scroll bar's width
local GUTTER = 8 -- the room kept for it on the right

-- A scrolling area with a slim gold bar on its right that only shows when there is more than fits; the mouse
-- wheel scrolls it. Put contents in scroll.child and call scroll:SetContentHeight(height) after laying them out.
function UI.Scroll(parent)
    local holder = CreateFrame("Frame", nil, parent)
    local scroll = CreateFrame("ScrollFrame", nil, holder)
    scroll:SetPoint("TOPLEFT")
    scroll:SetPoint("BOTTOMRIGHT", -GUTTER, 0)
    local child = CreateFrame("Frame", nil, scroll)
    child:SetSize(1, 1)
    scroll:SetScrollChild(child)
    holder.child = child
    local bar = CreateFrame("Slider", nil, holder)
    bar:SetPoint("TOPRIGHT", 0, 0)
    bar:SetPoint("BOTTOMRIGHT", 0, 0)
    bar:SetWidth(BAR)
    bar:SetOrientation("VERTICAL")
    bar:SetValueStep(1)
    local track = bar:CreateTexture(nil, "BACKGROUND")
    track:SetTexture(WHITE)
    track:SetAllPoints()
    track:SetVertexColor(0, 0, 0, 0.35)
    bar:SetThumbTexture(WHITE)
    local thumbTexture = bar:GetThumbTexture()
    thumbTexture:SetWidth(BAR)
    thumbTexture:SetAlpha(0)
    local thumb = CreateFrame("Frame", nil, bar)
    thumb:SetAllPoints(thumbTexture)
    UI.Surface(thumb, "bar")
    bar:SetScript("OnValueChanged", function(_, value)
        scroll:SetVerticalScroll(value)
    end)
    local function Wheel(_, delta)
        bar:SetValue(bar:GetValue() - delta * 30)
    end
    holder:EnableMouseWheel(true)
    holder:SetScript("OnMouseWheel", Wheel)
    holder:SetScript("OnSizeChanged", function(self)
        child:SetWidth(math.max(1, self:GetWidth() - GUTTER))
        self:SetContentHeight(child:GetHeight())
    end)
    function holder:SetContentHeight(height)
        child:SetHeight(math.max(1, height))
        scroll:UpdateScrollChildRect()
        local view = self:GetHeight()
        local range = math.max(0, height - view)
        bar:SetMinMaxValues(0, range)
        thumbTexture:SetHeight(math.max(16, view > 0 and view * view / math.max(height, 1) or 16))
        LA.SetShown(bar, range > 0)
        if bar:GetValue() > range then
            bar:SetValue(range)
        end
        scroll:SetVerticalScroll(math.min(bar:GetValue(), range))
    end
    function holder:ToTop()
        bar:SetValue(0)
        scroll:SetVerticalScroll(0)
    end
    function holder:ContentWidth()
        return math.max(1, self:GetWidth() - GUTTER)
    end
    holder.Wheel = Wheel
    return holder
end

-- A scrolling list of rows. Items are { header = true, text = ..., note = ... } or entries:
-- { key, text, sub (a second line), icon = function(row) (sets row.portrait or row.tile), tag, tagColour,
--   action = { label, onClick }, dim, tooltip = { lines } }. onClick(item, mouseButton) when a row is clicked.
-- rowHeight is 20 for one line, 30 with a second line and a portrait.
function UI.List(parent, onClick, rowHeight)
    rowHeight = rowHeight or 20
    local list = UI.Scroll(parent)
    list.rows, list.headers = {}, {}
    local function Header(i)
        if not list.headers[i] then
            list.headers[i] = UI.Heading(list.child)
        end
        return list.headers[i]
    end
    local function Row(i)
        if not list.rows[i] then
            local row = CreateFrame("Button", nil, list.child)
            row:SetHeight(rowHeight)
            row:RegisterForClicks("LeftButtonUp", "RightButtonUp")
            UI.Surface(row, "row")
            HoverLight(row)
            local tall = rowHeight >= 26
            row.portrait = UI.Portrait(row, tall and 24 or 15)
            row.portrait:SetPoint("LEFT", 4, 0)
            row.tile = UI.Icon(row, tall and 20 or 14, "tile")
            row.tile:SetPoint("LEFT", 5, 0)
            row.text = UI.Text(row, "label")
            row.text:SetWordWrap(false)
            row.sub = UI.Text(row, "small")
            row.sub:SetWordWrap(false)
            row.tag = UI.Text(row, "small")
            row.tag:SetJustifyH("RIGHT")
            row.action = UI.Button(row, "", 56, "plain")
            row.action:SetHeight(16)
            row.action:SetPoint("RIGHT", -5, 0)
            row.action:SetScript("OnClick", function(self)
                if self.run then
                    self.run(row.item)
                end
            end)
            row:SetScript("OnClick", function(self, button)
                if self.item and onClick then
                    onClick(self.item, button)
                end
            end)
            row:SetScript("OnEnter", function(self)
                local tip = self.item and self.item.tooltip
                if tip then
                    GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
                    for n, line in ipairs(tip) do
                        if n == 1 then
                            GameTooltip:AddLine(line, 1, 0.82, 0)
                        else
                            GameTooltip:AddLine(line, 1, 1, 1, true)
                        end
                    end
                    GameTooltip:Show()
                end
            end)
            row:SetScript("OnLeave", GameTooltip_Hide)
            row:SetScript("OnMouseWheel", list.Wheel)
            list.rows[i] = row
        end
        return list.rows[i]
    end

    -- Shows the items; emptyText when there are none.
    function list:SetItems(items, emptyText)
        self.items = items
        local y, rowIndex, headerIndex = 0, 0, 0
        local width = self:ContentWidth()
        for _, item in ipairs(items) do
            if item.header then
                headerIndex = headerIndex + 1
                local header = Header(headerIndex)
                header:ClearAllPoints()
                y = y + (headerIndex > 1 and 8 or 0)
                header:SetPoint("TOPLEFT", 0, -y)
                header:SetWidth(width)
                header:SetText(item.text)
                header:SetNote(item.note)
                header:Show()
                y = y + header:GetHeight() + 4
            else
                rowIndex = rowIndex + 1
                local row = Row(rowIndex)
                row.item = item
                row:ClearAllPoints()
                row:SetPoint("TOPLEFT", 0, -y)
                row:SetWidth(width)
                row.portrait:Hide()
                row.tile:Hide()
                local left = 6
                if item.icon then
                    item.icon(row)
                    left = (row.portrait:IsShown() and row.portrait:GetWidth() + 4 or row.tile:GetWidth() + 5) + 6
                end
                row.text:ClearAllPoints()
                row.sub:ClearAllPoints()
                row.text:SetText(item.text)
                if item.sub then
                    row.text:SetPoint("BOTTOMLEFT", row, "LEFT", left, 1)
                    row.sub:SetPoint("TOPLEFT", row, "LEFT", left, -1)
                    row.sub:SetText(item.sub)
                    row.sub:Show()
                else
                    row.text:SetPoint("LEFT", left, 0)
                    row.sub:Hide()
                end
                local right = 6
                if item.action then
                    row.action:SetText(item.action.label)
                    row.action.run = item.action.onClick
                    row.action:Show()
                    right = row.action:GetWidth() + 10
                else
                    row.action:Hide()
                end
                row.tag:ClearAllPoints()
                row.tag:SetPoint("RIGHT", -right, 0)
                row.tag:SetText(item.tag or "")
                TextColour(row.tag, item.tagColour or Style.T().muted)
                local textRight = right + (item.tag and row.tag:GetStringWidth() + 8 or 0)
                row.text:SetWidth(width - left - textRight)
                row.sub:SetWidth(width - left - textRight)
                row:SetAlpha(item.dim and 0.6 or 1)
                row.surface:Set(item.key ~= nil and item.key == self.chosen and "rowOn" or "row")
                row:Show()
                y = y + rowHeight + 2
            end
        end
        for i = rowIndex + 1, #self.rows do
            self.rows[i]:Hide()
        end
        for i = headerIndex + 1, #self.headers do
            self.headers[i]:Hide()
        end
        if not self.empty then
            self.empty = UI.Text(self.child, "small", width - 8)
            self.empty:SetPoint("TOPLEFT", 4, -4)
        end
        self.empty:SetText(rowIndex == 0 and emptyText or "")
        if rowIndex == 0 and emptyText then
            y = y + Draw.Ceil(self.empty:GetStringHeight()) + 8
        end
        self:SetContentHeight(y)
    end

    -- Marks the chosen row.
    function list:Choose(key)
        self.chosen = key
        for _, row in ipairs(self.rows) do
            if row:IsShown() then
                row.surface:Set(key ~= nil and row.item.key == key and "rowOn" or "row")
            end
        end
    end
    return list
end

-- Lays regions out top to bottom: stack:Add(region, gap, indent, height) places the region under the last one;
-- a font string's height is measured from its text. stack:Reset() starts again; stack:Height() is the total.
function UI.Stack(parent)
    local stack = { parent = parent, y = 0 }
    function stack:Reset()
        self.y = 0
    end
    function stack:Add(region, gap, indent, height)
        self.y = self.y + (gap or 0)
        region:ClearAllPoints()
        region:SetPoint("TOPLEFT", self.parent, "TOPLEFT", indent or 0, -self.y)
        region:Show()
        local h = height
        if not h then
            h = region.GetStringHeight and region:GetStringHeight() or region:GetHeight()
        end
        self.y = self.y + Draw.Ceil(h)
        return region
    end
    function stack:Height()
        return self.y
    end
    return stack
end

-- Tabs along the top of an area, underlined in gold when chosen (the Dialogue and Guild parts).
function UI.TopTabs(parent, names, onSelect)
    local tabs = CreateFrame("Frame", nil, parent)
    tabs:SetHeight(20)
    tabs.buttons = {}
    local line = UI.Divider(tabs)
    line:SetPoint("BOTTOMLEFT")
    line:SetPoint("BOTTOMRIGHT")
    local x = 0
    for i, name in ipairs(names) do
        local tab = CreateFrame("Button", nil, tabs)
        tab:SetHeight(20)
        tab.text = UI.Text(tab, "tab")
        tab.text:SetPoint("CENTER", 0, 1)
        tab.text:SetText(name)
        tab:SetWidth(math.ceil(tab.text:GetStringWidth()) + 18)
        tab:SetPoint("BOTTOMLEFT", x, 0)
        x = x + tab:GetWidth() + 2
        HoverLight(tab, 2)
        tab.under = tab:CreateTexture(nil, "ARTWORK")
        tab.under:SetTexture(WHITE)
        tab.under:SetPoint("BOTTOMLEFT")
        tab.under:SetPoint("BOTTOMRIGHT")
        tab.under:SetHeight(Draw.Px(2))
        Tint(tab.under, Style.gold)
        tab:SetScript("OnClick", function()
            PlaySound("igCharacterInfoTab")
            tabs:Select(i)
            onSelect(i)
        end)
        tabs.buttons[i] = tab
    end
    function tabs:Select(index)
        self.selected = index
        for i, tab in ipairs(self.buttons) do
            local on = i == index
            LA.SetShown(tab.under, on)
            TextColour(tab.text, on and Style.gold or Style.T().muted)
        end
    end
    -- Room on the right of the tabs for something else (a budget bar).
    function tabs:Right()
        return x
    end
    return tabs
end

-- A gold-rimmed box over a frame that can be dragged to move it while frames are unlocked. width and height are in
-- the target's units; mover:SetArea(width, height) changes them.
function UI.Mover(target, label, width, height, onMoved)
    local mover = CreateFrame("Frame", nil, UIParent)
    Draw.Fit(mover)
    mover:SetFrameStrata("MEDIUM") -- over the party and raid frames, under the addon's windows
    mover:SetFrameLevel(60)
    Draw.Follow(mover, target, "TOPLEFT", "TOPLEFT", -4, 4)
    function mover:SetArea(w, h)
        local ratio = target:GetEffectiveScale() / self:GetEffectiveScale()
        self:SetSize(Draw.Round(w * ratio) + 8, Draw.Round(h * ratio) + 8)
    end
    mover:SetArea(width, height)
    UI.Surface(mover, "mover")
    local bar = CreateFrame("Frame", nil, mover)
    bar:SetPoint("BOTTOMLEFT", mover, "TOPLEFT", 0, 2)
    UI.Surface(bar, "chosen")
    local move = UI.Icon(bar, 12, "plain")
    move:SetPoint("LEFT", 6, 0)
    move:Set("move", Style.gold)
    local text = UI.Text(bar, "button")
    text:SetPoint("LEFT", move, "RIGHT", 5, 0)
    text:SetText(label .. " · drag to move")
    bar:SetSize(math.ceil(text:GetStringWidth()) + 31, 20)
    for _, frame in ipairs({ mover, bar }) do
        frame:EnableMouse(true)
        frame:RegisterForDrag("LeftButton")
        frame:SetScript("OnDragStart", function()
            if not InCombatLockdown() then
                target:SetMovable(true)
                target:StartMoving()
            end
        end)
        frame:SetScript("OnDragStop", function()
            target:StopMovingOrSizing()
            if onMoved then
                onMoved()
            end
        end)
    end
    mover:Hide()
    return mover
end

-- A tooltip with a gold title and white lines, on a frame's OnEnter.
function UI.Tooltip(frame, title, ...)
    local lines = { ... }
    frame:HookScript("OnEnter", function(self)
        GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
        GameTooltip:AddLine(type(title) == "function" and title() or title, 1, 0.82, 0)
        for _, line in ipairs(lines) do
            GameTooltip:AddLine(line, 1, 1, 1, true)
        end
        GameTooltip:Show()
    end)
    frame:HookScript("OnLeave", GameTooltip_Hide)
end
