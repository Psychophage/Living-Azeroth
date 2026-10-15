-- SPDX-License-Identifier: GPL-2.0-or-later
-- The orders ring: a disc that names who the orders are for (one bot, a raid group, all the player's bots, or a
-- selection) and what they are doing, with the orders as the parts of a ring around it. It stays open while orders
-- are given; clicking anywhere else, Escape, or what opened it again closes it, and opening it for someone else
-- moves it there. Number keys pick orders while it is open (out of combat; the game does not let addons change key
-- bindings during combat).
local _, LA = ...

local Ring = {}
LA.Ring = Ring

local UI, Style, Draw = LA.UI, LA.Style, LA.Draw

-- Sizes in units of the fitted ring; the parts' art (make_art.py) is drawn for the same numbers.
local INNER, OUTER = 43, 75 -- the ring of orders
local DISC = 80
local GLYPH = 18
local BOX = 2 * OUTER + 8
local ART_SCALE, ART_CENTRE_Y, ART_HEIGHT = 2.56, 194, 256 -- texels per unit, the centre's row, texture height
local MIN_SIZE, MAX_SIZE = 0.7, 1.3

local frame = CreateFrame("Frame", "LivingAzerothRing", UIParent)
Draw.Fit(frame)
frame:SetSize(BOX, BOX)
frame:SetFrameStrata("FULLSCREEN_DIALOG") -- above the windows it is opened from
frame:SetClampedToScreen(true)
frame:SetMovable(true)
frame:Hide()
table.insert(UISpecialFrames, "LivingAzerothRing") -- Escape closes it

-- Everything is drawn on body, which is scaled by the player's size and grows into place as the ring opens.
local body = CreateFrame("Frame", nil, frame)
body:SetSize(BOX, BOX)
body:SetPoint("CENTER")

-- The disc's art has room for its shadow around it: the disc itself is 0.88 of the texture.
local disc = body:CreateTexture(nil, "BACKGROUND")
disc:SetTexture(Style.ART .. "disc")
disc:SetSize(DISC / 0.88, DISC / 0.88)
disc:SetPoint("CENTER")

local name = body:CreateFontString(nil, "OVERLAY")
name:SetFontObject(Style.Font("heading"))
name:SetPoint("BOTTOM", body, "CENTER", 0, 2)
name:SetWidth(DISC - 12)
name:SetWordWrap(false)
local hint = body:CreateFontString(nil, "OVERLAY")
hint:SetFontObject(Style.Font("body"))
hint:SetTextColor(1, 1, 1)
hint:SetPoint("TOP", body, "CENTER", 0, -2)
hint:SetWidth(DISC - 8)
hint:SetHeight(26)
hint:SetJustifyV("TOP")

-- The whole ring takes the mouse; where the mouse is on it says which order it points at.
local hit = CreateFrame("Button", nil, body)
hit:SetSize(2 * OUTER + 4, 2 * OUTER + 4)
hit:SetPoint("CENTER")
hit:RegisterForClicks("AnyUp")

local parts = {} -- the ring's parts; the first `shown` hold its orders, in ring order
local shown = 0
local current -- { key, label, guids }
local hovered, pressed -- the part under the mouse, the part held down
local overDisc = false -- the mouse is on the middle, where a click closes the ring
local anchorFrame -- what the ring opened beside
local previewing = false -- shown where it opens, to be dragged, while frames are unlocked

local function Settings()
    return LivingAzerothDB.ring
end

-- The bots the ring is for that are still here and commandable.
local function Bots()
    local bots = {}
    for _, guid in ipairs(current and current.guids or {}) do
        local bot = LA.Bots.Mine()[guid]
        if bot and bot.commandable then
            bots[#bots + 1] = bot
        end
    end
    return bots
end

local function For(guid)
    for _, candidate in ipairs(current and current.guids or {}) do
        if candidate == guid then
            return true
        end
    end
    return false
end

-- What the bots are doing: their shared standing order, or "Mixed orders".
local function State(bots)
    local first = LA.Orders.Standing(bots[1])
    for i = 2, #bots do
        if LA.Orders.Standing(bots[i]) ~= first then
            return "Mixed orders"
        end
    end
    return first.state
end

local CREAM = Style.C("#f7e8c2")

-- a mixed with b by f (0 to 1), at alpha.
local function Mix(a, b, f, alpha)
    return { a[1] + (b[1] - a[1]) * f, a[2] + (b[2] - a[2]) * f, a[3] + (b[3] - a[3]) * f, alpha or a[4] or 1 }
end

-- Paints a part for its state: resting, pointed at, held down, the bots' current order (lit in its tone), or
-- greyed when none of the bots can take it.
local function Paint(part)
    local T = Style.T()
    local tone = Style.TONES[part.order.tone] or Style.TONES.gold
    local top, bottom = T.plain[1], T.plain[2]
    local edge = T.plainRim[1]
    local glyph = tone.light
    if part.on then
        top, bottom = Mix(tone.middle, CREAM, 0.12), Mix(tone.middle, T.plain[2], 0.35)
        edge, glyph = T.rim[1], CREAM
    end
    if part == hovered and part.offered and not previewing then
        top, bottom = Mix(top, tone.middle, 0.45), Mix(bottom, tone.middle, 0.3)
        edge = T.rim[1]
        if part == pressed then
            top, bottom = bottom, Mix(bottom, T.plain[2], 0.5)
        end
    end
    -- (A texture's SetAlpha would undo its gradient, so a greyed part's alpha goes into its colours.)
    local alpha = part.offered and 1 or 0.7
    Draw.Paint(part.fill, Mix(top, top, 0, alpha), Mix(bottom, bottom, 0, alpha))
    part.edge:SetVertexColor(edge[1], edge[2], edge[3])
    part.glyph:Set(part.order.glyph, glyph)
    part.glyph:SetDim(not part.offered)
    local number = part.on and T.text or T.muted
    part.number:SetTextColor(number[1], number[2], number[3])
end

local function Refresh()
    if previewing then
        for i = 1, shown do
            local part = parts[i]
            part.on, part.offered, part.waiting = false, true, false
            Paint(part)
        end
        return
    end
    local bots = Bots()
    if #bots == 0 then
        frame:Hide()
        return
    end
    name:SetText(current.label or bots[1].name)
    local waiting = {}
    for _, bot in ipairs(bots) do
        for _, order in ipairs(LA.Orders.catalog) do
            if LA.Orders.IsPending(bot.guid, order.id) then
                waiting[order.id] = true
                waiting.any = true
            end
        end
    end
    if hovered then
        hint:SetText(hovered.order.label)
    elseif overDisc then
        hint:SetText("Click to close")
    elseif waiting.any then
        hint:SetText("Waiting for " .. (#bots == 1 and bots[1].name or #bots .. " bots") .. "...")
    else
        hint:SetText(State(bots))
    end
    for i = 1, shown do
        local part = parts[i]
        local id = part.order.id
        local offered, on = false, true
        for _, bot in ipairs(bots) do
            if LA.Orders.Offered(bot, id) then
                offered = true
                on = on and LA.Orders.IsOn(bot, id)
            end
        end
        part.offered = offered
        part.on = offered and on
        part.waiting = waiting[id] or false
        Paint(part)
    end
end

-- Turns a part's texture to its place: the art points up, and texture coordinates turn it by angle (clockwise
-- from the top) inside a box around where it lands.
local function Turn(texture, count, angle)
    local width = count <= 4 and 512 or 256
    local half = math.pi / count
    local left, bottom, right, top = math.huge, math.huge, -math.huge, -math.huge
    for step = 0, 8 do
        local a = angle - half + 2 * half * step / 8
        for _, radius in ipairs({ INNER, OUTER }) do
            local x, y = radius * math.sin(a), radius * math.cos(a)
            left, right = math.min(left, x), math.max(right, x)
            bottom, top = math.min(bottom, y), math.max(top, y)
        end
    end
    left, bottom, right, top = left - 2, bottom - 2, right + 2, top + 2
    texture:ClearAllPoints()
    texture:SetPoint("BOTTOMLEFT", body, "CENTER", left, bottom)
    texture:SetSize(right - left, top - bottom)
    local cos, sin = math.cos(angle), math.sin(angle)
    local function At(x, y)
        local u, v = x * cos - y * sin, x * sin + y * cos
        return (width / 2 + u * ART_SCALE) / width, (ART_CENTRE_Y - v * ART_SCALE) / ART_HEIGHT
    end
    local ulx, uly = At(left, top)
    local llx, lly = At(left, bottom)
    local urx, ury = At(right, top)
    local lrx, lry = At(right, bottom)
    texture:SetTexCoord(ulx, uly, llx, lly, urx, ury, lrx, lry)
end

local function Give(part)
    if not part or not part.offered or previewing then
        return
    end
    for _, bot in ipairs(Bots()) do
        if LA.Orders.Offered(bot, part.order.id) then
            LA.Orders.Give(bot, part.order.id)
        end
    end
    PlaySound("igMainMenuOptionCheckBoxOn")
    Refresh() -- the bot's real state decides how it shows
end

local function Part(i)
    if parts[i] then
        return parts[i]
    end
    local part = {}
    part.fill = body:CreateTexture(nil, "BORDER")
    part.edge = body:CreateTexture(nil, "ARTWORK")
    part.glyph = UI.Icon(body, GLYPH, "plain")
    part.number = body:CreateFontString(nil, "OVERLAY")
    part.number:SetFontObject(Style.Font("small"))
    -- Number keys reach the order through a named button.
    part.key = CreateFrame("Button", "LivingAzerothRingKey" .. i, frame)
    part.key:SetSize(1, 1)
    part.key:SetPoint("CENTER")
    part.key:SetScript("OnClick", function() Give(part) end)
    parts[i] = part
    return part
end

local function Layout()
    local orders = Settings().orders
    local count = 0
    for _, id in ipairs(orders) do
        if LA.Orders.byId[id] then
            count = count + 1
        end
    end
    local slot = 0
    for _, id in ipairs(orders) do
        local order = LA.Orders.byId[id]
        if order then
            slot = slot + 1
            local part = Part(slot)
            part.order = order
            part.angle = (slot - 1) / count * 2 * math.pi
            part.fill:SetTexture(Style.ART .. "ring-" .. count .. "-fill")
            part.edge:SetTexture(Style.ART .. "ring-" .. count .. "-edge")
            Turn(part.fill, count, part.angle)
            Turn(part.edge, count, part.angle)
            local middle = (INNER + OUTER) / 2
            part.glyph:ClearAllPoints()
            part.glyph:SetPoint("CENTER", body, "CENTER", middle * math.sin(part.angle), middle * math.cos(part.angle))
            -- The key's number sits by the outer edge, a little clockwise of the glyph.
            local numberAngle = part.angle + math.min(0.32, math.pi / count * 0.55)
            local numberRadius = OUTER - 9
            part.number:ClearAllPoints()
            part.number:SetPoint("CENTER", body, "CENTER", numberRadius * math.sin(numberAngle),
                numberRadius * math.cos(numberAngle))
            part.number:SetText(slot)
            for _, region in ipairs({ part.fill, part.edge, part.glyph, part.number, part.key }) do
                region:Show()
            end
        end
    end
    for i = slot + 1, #parts do
        for _, region in ipairs({ parts[i].fill, parts[i].edge, parts[i].glyph, parts[i].number, parts[i].key }) do
            region:Hide()
        end
    end
    shown = slot
end

-- What the mouse is over: a part, "disc", or nil (outside the ring).
local function Under()
    local x, y = GetCursorPosition()
    local scale = body:GetEffectiveScale()
    local cx, cy = body:GetCenter()
    if not cx then
        return nil
    end
    x, y = x / scale - cx, y / scale - cy
    local distance = math.sqrt(x * x + y * y)
    if distance < INNER - 1 then
        return "disc"
    elseif distance > OUTER + 2 or shown == 0 then
        return nil
    end
    local step = 2 * math.pi / shown
    local index = math.floor((math.atan2(x, y) % (2 * math.pi)) / step + 0.5) % shown + 1
    return parts[index]
end

-- The ring's size: the player's choice, 70% to 130%.
local function Size()
    local size = tonumber(Settings().size) or 1
    return math.max(MIN_SIZE, math.min(MAX_SIZE, size))
end

-- Opening: the ring grows into place and fades in.
local opening = CreateFrame("Frame")
local function Open()
    local started = GetTime()
    frame:SetAlpha(0)
    opening:SetScript("OnUpdate", function(self)
        local t = math.min(1, (GetTime() - started) / 0.14)
        local eased = 1 - (1 - t) * (1 - t)
        body:SetScale(Size() * (0.88 + 0.12 * eased))
        frame:SetAlpha(eased)
        if t >= 1 then
            self:SetScript("OnUpdate", nil)
        end
    end)
end

local function Bind()
    ClearOverrideBindings(frame)
    if InCombatLockdown() then
        return
    end
    for i = 1, math.min(shown, 9) do
        SetOverrideBindingClick(frame, true, tostring(i), parts[i].key:GetName())
    end
end

-- Where the ring goes. "beside": to the right of what opened it; "cursor": centred on the mouse; "fixed": the
-- spot the player dragged it to (its centre in UIParent's units).
local function Place(anchor)
    local settings = Settings()
    local size = Size()
    frame:SetSize(BOX * size, BOX * size)
    body:SetScale(size)
    local scale = frame:GetScale()
    frame:ClearAllPoints()
    if settings.placement == "cursor" then
        local x, y = GetCursorPosition()
        local ui = UIParent:GetEffectiveScale()
        frame:SetPoint("CENTER", UIParent, "BOTTOMLEFT", x / ui / scale, y / ui / scale)
    elseif settings.placement == "fixed" or not anchor then
        frame:SetPoint("CENTER", UIParent, "BOTTOMLEFT", settings.x / scale, settings.y / scale)
    else
        frame:SetPoint("LEFT", anchor, "RIGHT", 4, 0)
    end
end

-- Opens the ring beside anchor for target = { key, label, guids }; the same key again closes it. Without a label
-- it is named after its only bot.
function Ring.Toggle(target, anchor)
    if frame:IsShown() and current and current.key == target.key then
        frame:Hide()
        return
    end
    previewing = false
    current = target
    hovered, pressed = nil, nil
    anchorFrame = anchor
    Layout()
    Place(anchor)
    Open()
    frame:Show()
    Bind()
    Refresh()
    LA.Fire("ring:changed")
end

function Ring.Close()
    frame:Hide()
end

-- The key of what the ring is open for, if it is open.
function Ring.OpenFor()
    return frame:IsShown() and current and current.key
end

-- After a settings change: the ring, open or previewed, takes its new orders, size and place at once.
function Ring.Relayout()
    if not frame:IsShown() then
        return
    end
    Layout()
    if previewing then
        if Settings().placement ~= "fixed" then
            Ring.Preview(false)
            return
        end
        Place(nil)
        Refresh()
        return
    end
    Place(anchorFrame)
    Bind()
    Refresh()
end

-- The ring itself at its fixed spot and size, to be dragged into place (frames unlocked, placement "fixed").
function Ring.Preview(show)
    if not show then
        if previewing then
            previewing = false
            frame:Hide()
        end
        return
    end
    frame:Hide()
    previewing = true
    current = nil
    hovered, pressed = nil, nil
    Layout()
    Place(nil)
    name:SetText("Orders ring")
    hint:SetText("Drag to place it")
    frame:SetAlpha(1)
    frame:Show()
    Refresh()
end

-- While it is shown to be placed, pressing anywhere on it moves it straight away.
local moving = false
hit:SetScript("OnMouseDown", function(_, button)
    if previewing then
        if button == "LeftButton" then
            moving = true
            frame:StartMoving()
        end
        return
    end
    pressed = hovered
    if pressed then
        Paint(pressed)
    end
end)
hit:SetScript("OnMouseUp", function()
    if moving then
        moving = false
        frame:StopMovingOrSizing()
        frame:SetUserPlaced(false) -- the addon keeps the position, not the game's layout cache
        local x, y = frame:GetCenter()
        local scale = frame:GetScale()
        Settings().x, Settings().y = x * scale, y * scale
        Place(nil)
        return
    end
    local was = pressed
    pressed = nil
    if was then
        Paint(was)
    end
end)
hit:SetScript("OnClick", function()
    local under = Under()
    if type(under) == "table" then
        Give(under)
    elseif not previewing then
        frame:Hide() -- the middle closes it, and the corners of the box around the ring are not part of it
    end
end)

frame:SetScript("OnHide", function()
    if moving then
        moving = false
        frame:StopMovingOrSizing()
    end
    ClearOverrideBindings(frame)
    previewing = false
    current = nil
    hovered, pressed, overDisc = nil, nil, false
    LA.Fire("ring:changed")
end)

-- Following the mouse over the parts; closing when the player clicks anywhere else, without catching that click
-- (it still reaches whatever was under the mouse).
local wasDown = false
frame:SetScript("OnUpdate", function()
    local under = hit:IsMouseOver() and Under() or nil
    local part = type(under) == "table" and under or nil
    if part ~= hovered or (under == "disc") ~= overDisc then
        local was = hovered
        hovered, overDisc = part, under == "disc"
        if was then
            Paint(was)
        end
        if not previewing then
            Refresh()
        elseif part then
            Paint(part)
        end
    end
    for i = 1, shown do
        local each = parts[i]
        if each.waiting then
            each.edge:SetAlpha(0.55 + 0.45 * math.sin(GetTime() * 9))
        else
            each.edge:SetAlpha(1)
        end
    end
    local down = IsMouseButtonDown("LeftButton") or IsMouseButtonDown("RightButton")
    if down and not wasDown and not previewing then
        local focus = GetMouseFocus()
        local ours = focus and (focus.livingAzerothPip or focus == hit)
        if not ours then
            frame:Hide()
        end
    end
    wasDown = down
end)

local events = CreateFrame("Frame")
events:RegisterEvent("PLAYER_REGEN_DISABLED")
events:RegisterEvent("PLAYER_REGEN_ENABLED")
events:SetScript("OnEvent", function(_, event)
    if frame:IsShown() then
        if event == "PLAYER_REGEN_DISABLED" then
            ClearOverrideBindings(frame) -- the game clears them anyway; the ring stays usable by mouse
        else
            Bind()
        end
    end
end)

LA.On("orders:changed", function(guid)
    if For(guid) then
        Refresh()
    end
end)
LA.On("bots:changed", function(guid)
    if For(guid) then
        Refresh()
    end
end)

-- Sizes were once three steps.
local OLD_SIZES = { small = 0.8, normal = 1, large = 1.2 }

LA.On("loaded", function()
    local settings = LivingAzerothDB.ring or {}
    settings.placement = settings.placement or "beside"
    settings.size = OLD_SIZES[settings.size] or tonumber(settings.size) or 1
    settings.orders = settings.orders or { unpack(LA.Orders.defaultRing) }
    settings.x = settings.x or UIParent:GetWidth() / 2
    settings.y = settings.y or UIParent:GetHeight() * 0.35
    LivingAzerothDB.ring = settings
end)

Ring.MIN_SIZE, Ring.MAX_SIZE = MIN_SIZE, MAX_SIZE
