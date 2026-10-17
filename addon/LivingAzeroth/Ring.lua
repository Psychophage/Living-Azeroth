-- SPDX-License-Identifier: GPL-2.0-or-later
-- The orders ring: a disc that names who the orders are for (one bot, a raid group, all the player's bots, or a
-- selection) and what they are doing, with the orders as the parts of a ring around it. It stays open while orders
-- are given; clicking anywhere else, Escape, or what opened it again closes it, and opening it for someone else
-- moves it there. Number keys pick orders while it is open (out of combat; the game does not let addons change key
-- bindings during combat).
--
-- The orders are of three kinds: where the bot is (follow, stay, guard: one at a time), things it does once
-- (attack, pull, fall back, eat), and switches (passive, loot, join in). The player chooses the layout: "classic"
-- (every order a part, in their order), "kinds" (the same ring grouped by kind, with small lights showing a
-- switch's state) or "two" (the bots' state on an inner ring, things to do on an outer ring); and how the movement
-- orders show: "separate" parts, one part that toggles Follow and Stay, or one that steps through Follow, Stay and
-- Guard. A combined movement part shows the bots' current state, with a light for each state; clicking it gives the
-- next one (right-click, the one before).
local _, LA = ...

local Ring = {}
LA.Ring = Ring

local UI, Style, Draw = LA.UI, LA.Style, LA.Draw

-- Sizes in units of the fitted ring. Each band's art (make_art.py, RING_BANDS) is drawn for the same numbers: radii,
-- texels per unit, and the row of the texture the ring's centre is on.
local BANDS = {
    one = { inner = 43, outer = 75, art = "ring", scale = 2.56, centre = 194, glyph = 18 },
    inside = { inner = 43, outer = 66, art = "ring-in", scale = 2.56, centre = 194, glyph = 15 },
    outside = { inner = 69, outer = 100, art = "ring-out", scale = 2, centre = 208, glyph = 18 },
}
local ART_HEIGHT = 256
local DISC = 80
local DOT = 5 -- a state light
local MIN_SIZE, MAX_SIZE = 0.7, 1.3
local MOVES = { follow = true, stay = true, guard = true }
local STEPS = { toggle = { "follow", "stay" }, cycle = { "follow", "stay", "guard" } }
local LAYOUTS = { classic = true, kinds = true, two = true }
local MOVEMENTS = { separate = true, toggle = true, cycle = true }

local box = 2 * BANDS.one.outer + 8 -- the ring's box, for the layout in use

local frame = CreateFrame("Frame", "LivingAzerothRing", UIParent)
Draw.Fit(frame)
frame:SetSize(box, box)
frame:SetFrameStrata("FULLSCREEN_DIALOG") -- above the windows it is opened from
frame:SetClampedToScreen(true)
frame:SetMovable(true)
frame:Hide()
table.insert(UISpecialFrames, "LivingAzerothRing") -- Escape closes it

-- Everything is drawn on body, which is scaled by the player's size and grows into place as the ring opens.
local body = CreateFrame("Frame", nil, frame)
body:SetSize(box, box)
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
hit:SetPoint("CENTER")
hit:RegisterForClicks("AnyUp")

local parts = {} -- the ring's parts; the first `shown` hold its entries, in key order
local shownKey -- which of the bots lay dead when the parts were laid out
local shown = 0
local bandsInUse = {} -- band -> { count, parts }
local current -- { key, label, guids }
local hovered, pressed -- the part under the mouse, the part held down
local anchorFrame -- what the ring opened beside
local previewing = false -- shown without bots: to be dragged into place, or beside the settings
local previewAnchor -- the space in the settings it is previewed in (it is not dragged then)

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

-- What the bots are doing: dead or on their way back, their shared standing order, or "Mixed orders".
local function State(bots)
    local dead, ghosts = 0, 0
    for _, bot in ipairs(bots) do
        dead = dead + (bot.life == "dead" and 1 or 0)
        ghosts = ghosts + (bot.life == "ghost" and 1 or 0)
    end
    if dead > 0 then
        return #bots == 1 and "Dead" or dead .. " dead"
    elseif ghosts > 0 then
        return #bots == 1 and "Running back" or ghosts .. " running back"
    end
    local first = LA.Orders.Standing(bots[1])
    for i = 2, #bots do
        if LA.Orders.Standing(bots[i]) ~= first then
            return "Mixed orders"
        end
    end
    return first.state
end

-- The movement state all the bots share, or nil.
local function SharedMove(bots)
    local id = bots[1] and bots[1].order
    for i = 2, #bots do
        if bots[i].order ~= id then
            return nil
        end
    end
    return MOVES[id] and id or nil
end

local CREAM = Style.C("#f7e8c2")

-- a mixed with b by f (0 to 1), at alpha.
local function Mix(a, b, f, alpha)
    return { a[1] + (b[1] - a[1]) * f, a[2] + (b[2] - a[2]) * f, a[3] + (b[3] - a[3]) * f, alpha or a[4] or 1 }
end

-- Paints a part for its state: resting, pointed at, held down, lit in its tone (the bots' current order, a switch
-- that is on, or a movement part showing where the bots are), or greyed when none of the bots can take it. Its lights
-- show which state is on.
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
    for i, light in ipairs(part.lights) do
        if i <= part.lightCount then
            local lit = part.lit == i
            local colour = lit and (part.entry.kind == "switch" and Style.green or Style.gold)
                or Style.C("#ffffff", 0.22)
            light:SetVertexColor(colour[1], colour[2], colour[3], colour[4] or 1)
        end
    end
end

-- The ring's entries from the player's orders, in key order, each { kind, id, band }: "move" (the combined
-- movement part, its steps in .steps), "stand" (follow, stay or guard as parts of their own), "action" or "switch".
-- While one of the bots lies dead, "Release spirit" comes first.
local function Entries(bots)
    local settings = Settings()
    local steps = STEPS[settings.movement]
    local stepping = {}
    for _, id in ipairs(steps or {}) do
        stepping[id] = true
    end
    local entries, move = {}, nil
    for _, id in ipairs(settings.orders) do
        local order = LA.Orders.byId[id]
        if order then
            if stepping[id] then
                if not move then
                    move = { kind = "move", id = "move", steps = steps }
                    entries[#entries + 1] = move
                end
            else
                local kind = MOVES[id] and "stand" or order.switch and "switch" or "action"
                entries[#entries + 1] = { kind = kind, id = id }
            end
        end
    end
    -- Grouped by kind (keeping the player's order within each): where the bots are, things to do, switches. On two
    -- rings, the bots' state goes inside.
    local layout = settings.layout
    if layout ~= "classic" then
        local rank = { move = 1, stand = 1, action = 2, switch = 3 }
        if layout == "two" then
            rank = { move = 1, stand = 1, switch = 2, action = 3 }
        end
        for i, entry in ipairs(entries) do
            entry.place = i
        end
        table.sort(entries, function(a, b)
            if rank[a.kind] ~= rank[b.kind] then
                return rank[a.kind] < rank[b.kind]
            end
            return a.place < b.place
        end)
    end
    for _, bot in ipairs(bots) do
        if bot.life == "dead" then
            table.insert(entries, 1, { kind = "action", id = "release" })
            break
        end
    end
    for _, entry in ipairs(entries) do
        entry.band = layout ~= "two" and "one" or entry.kind == "action" and "outside" or "inside"
    end
    return entries
end

-- What an entry's part shows and does now: { order (its look and label), on, offered, lit, next } for these bots.
local function Read(entry, bots)
    if entry.kind == "move" then
        local shared = SharedMove(bots)
        local at
        for i, id in ipairs(entry.steps) do
            if id == shared then
                at = i
            end
        end
        if previewing then
            at = 1
        end
        local offered = #bots > 0 or previewing
        local nextId = entry.steps[at and at % #entry.steps + 1 or 1]
        return { order = LA.Orders.byId[entry.steps[at or 1]], on = at ~= nil, offered = offered, lit = at,
            lights = #entry.steps, next = nextId }
    end
    local order = LA.Orders.byId[entry.id]
    local offered, on = previewing, not previewing
    for _, bot in ipairs(bots) do
        if LA.Orders.Offered(bot, entry.id) then
            offered = true
            on = on and LA.Orders.IsOn(bot, entry.id)
        end
    end
    on = offered and on
    -- A switch's light: on the grouped layouts only (the classic ring shows a switch only by lighting it).
    local lights = (entry.kind == "switch" and Settings().layout ~= "classic") and 1 or 0
    return { order = order, on = on, offered = offered, lit = on and 1 or nil, lights = lights }
end

local Relayout

local function Refresh()
    local bots = previewing and {} or Bots()
    if not previewing and #bots == 0 then
        frame:Hide()
        return
    end
    -- A bot that died or came back changes which parts the ring has.
    local key = ""
    for _, bot in ipairs(bots) do
        key = key .. (bot.life == "dead" and "d" or "")
    end
    if shownKey and key ~= shownKey then
        shownKey = key
        Relayout()
        return
    end
    shownKey = key
    local waiting = {}
    for _, bot in ipairs(bots) do
        for _, order in ipairs(LA.Orders.catalog) do
            if LA.Orders.IsPending(bot.guid, order.id) then
                waiting[order.id] = true
                waiting.any = true
            end
        end
    end
    for i = 1, shown do
        local part = parts[i]
        local state = Read(part.entry, bots)
        part.order, part.on, part.offered, part.lit, part.lightCount = state.order, state.on, state.offered,
            state.lit, state.lights
        part.next = state.next
        local pending = false
        for _, id in ipairs(part.entry.steps or { part.entry.id }) do
            pending = pending or waiting[id] or false
        end
        part.waiting = pending
        for j, light in ipairs(part.lights) do
            LA.SetShown(light, j <= part.lightCount)
        end
        Paint(part)
    end
    if previewing then
        return
    end
    name:SetText(current.label or bots[1].name)
    if hovered then
        -- What a click does: for a movement part, the next place to be.
        hint:SetText(hovered.next and LA.Orders.byId[hovered.next].label or hovered.order.label)
    elseif waiting.any then
        hint:SetText("Waiting for " .. (#bots == 1 and bots[1].name or #bots .. " bots") .. "...")
    else
        hint:SetText(State(bots))
    end
end

-- How wide a band's art is for count parts: the same rule as make_art.py's art_width.
local function ArtWidth(band, count)
    return (band.outer * math.sin(math.min(math.pi / count, math.pi / 2)) + 3) * band.scale > 126 and 512 or 256
end

-- Turns a part's texture to its place: the art points up, and texture coordinates turn it by angle (clockwise
-- from the top) inside a box around where it lands.
local function Turn(texture, band, count, angle)
    local width = ArtWidth(band, count)
    local half = math.pi / count
    local left, bottom, right, top = math.huge, math.huge, -math.huge, -math.huge
    for step = 0, 8 do
        local a = angle - half + 2 * half * step / 8
        for _, radius in ipairs({ band.inner, band.outer }) do
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
        return (width / 2 + u * band.scale) / width, (band.centre - v * band.scale) / ART_HEIGHT
    end
    local ulx, uly = At(left, top)
    local llx, lly = At(left, bottom)
    local urx, ury = At(right, top)
    local lrx, lry = At(right, bottom)
    texture:SetTexCoord(ulx, uly, llx, lly, urx, ury, lrx, lry)
end

local function Give(part, back)
    if not part or not part.offered or previewing then
        return
    end
    local id = part.next or part.entry.id
    if back and part.entry.steps then
        -- The step before the current one.
        local steps, at = part.entry.steps, part.lit or 1
        id = steps[(at - 2) % #steps + 1]
    end
    for _, bot in ipairs(Bots()) do
        if LA.Orders.Offered(bot, id) then
            LA.Orders.Give(bot, id)
        end
    end
    PlaySound("igMainMenuOptionCheckBoxOn")
    Refresh() -- the bot's real state decides how it shows
end

local function Part(i)
    if parts[i] then
        return parts[i]
    end
    local part = { lights = {} }
    part.fill = body:CreateTexture(nil, "BORDER")
    part.edge = body:CreateTexture(nil, "ARTWORK")
    part.glyph = UI.Icon(body, BANDS.one.glyph, "plain")
    part.number = body:CreateFontString(nil, "OVERLAY")
    part.number:SetFontObject(Style.Font("small"))
    for j = 1, 3 do
        part.lights[j] = body:CreateTexture(nil, "OVERLAY")
        part.lights[j]:SetTexture(Style.ART .. "dot")
        part.lights[j]:SetSize(DOT, DOT)
    end
    -- Number keys reach the order through a named button.
    part.key = CreateFrame("Button", "LivingAzerothRingKey" .. i, frame)
    part.key:SetSize(1, 1)
    part.key:SetPoint("CENTER")
    part.key:SetScript("OnClick", function() Give(part) end)
    parts[i] = part
    return part
end

local function ShowPart(part, show)
    for _, region in ipairs({ part.fill, part.edge, part.glyph, part.number, part.key }) do
        LA.SetShown(region, show)
    end
    if not show then
        for _, light in ipairs(part.lights) do
            light:Hide()
        end
    end
end

-- Places a part at its slot of a band (a band with one entry is drawn as half a ring of two), with its key's
-- number and room for lights state lights.
local function Place1(part, band, count, slot, number, lights)
    local angle = (slot - 1) / count * 2 * math.pi
    part.angle, part.band, part.slot = angle, band, slot
    part.fill:SetTexture(Style.ART .. band.art .. "-" .. count .. "-fill")
    part.edge:SetTexture(Style.ART .. band.art .. "-" .. count .. "-edge")
    Turn(part.fill, band, count, angle)
    Turn(part.edge, band, count, angle)
    -- The glyph in the middle of the band, or further out where the lights by the inner edge need the room.
    local middle = math.max((band.inner + band.outer) / 2, band.inner + DOT + 3 + band.glyph / 2)
    part.glyph:Resize(band.glyph)
    part.glyph:ClearAllPoints()
    part.glyph:SetPoint("CENTER", body, "CENTER", middle * math.sin(angle), middle * math.cos(angle))
    -- The key's number sits by the outer edge, a little clockwise of the glyph.
    local numberAngle = angle + math.min(0.32, math.pi / count * 0.55) * BANDS.one.outer / band.outer
    local numberRadius = band.outer - 8
    part.number:ClearAllPoints()
    part.number:SetPoint("CENTER", body, "CENTER", numberRadius * math.sin(numberAngle),
        numberRadius * math.cos(numberAngle))
    part.number:SetText(number)
    -- The state lights, side by side along the inner edge.
    local lightRadius = band.inner + DOT / 2 + 2
    for j = 1, lights do
        local light = part.lights[j]
        local offset = (j - (lights + 1) / 2) * (DOT + 2) / lightRadius
        light:ClearAllPoints()
        light:SetPoint("CENTER", body, "CENTER", lightRadius * math.sin(angle + offset),
            lightRadius * math.cos(angle + offset))
    end
end

-- The parts for the entries, band by band, and the ring's box for the layout.
local function Layout()
    local entries = Entries(previewing and {} or Bots())
    shownKey = nil
    wipe(bandsInUse)
    for _, entry in ipairs(entries) do
        local band = BANDS[entry.band]
        bandsInUse[band] = bandsInUse[band] or { entries = {}, parts = {} }
        table.insert(bandsInUse[band].entries, entry)
    end
    local outer = BANDS.one.outer
    for i, entry in ipairs(entries) do
        local part = Part(i)
        part.entry = entry
        local band = BANDS[entry.band]
        local use = bandsInUse[band]
        local slot = #use.parts + 1
        use.parts[slot] = part
        Place1(part, band, math.max(2, #use.entries), slot, i, entry.steps and #entry.steps or 1)
        ShowPart(part, true)
        outer = math.max(outer, band.outer)
    end
    for i = #entries + 1, #parts do
        ShowPart(parts[i], false)
    end
    shown = #entries
    box = 2 * outer + 8
    body:SetSize(box, box)
    hit:SetSize(2 * outer + 4, 2 * outer + 4)
end

-- What the mouse is over: a part, "disc", or nil (outside the ring, or a band's empty half).
local function Under()
    local x, y = GetCursorPosition()
    local scale = body:GetEffectiveScale()
    local cx, cy = body:GetCenter()
    if not cx then
        return nil
    end
    x, y = x / scale - cx, y / scale - cy
    local distance = math.sqrt(x * x + y * y)
    if distance < BANDS.one.inner - 1 then
        return "disc"
    end
    for band, use in pairs(bandsInUse) do
        if distance >= band.inner - 1 and distance <= band.outer + 2 then
            local count = math.max(2, #use.entries)
            local step = 2 * math.pi / count
            local index = math.floor((math.atan2(x, y) % (2 * math.pi)) / step + 0.5) % count + 1
            return use.parts[index]
        end
    end
    return nil
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
    if previewing and previewAnchor then
        -- In the settings: at the player's size, or smaller to fit its space.
        size = math.min(size, previewAnchor:GetWidth() / box, previewAnchor:GetHeight() / box)
    end
    frame:SetSize(box * size, box * size)
    body:SetScale(size)
    local scale = frame:GetScale()
    frame:ClearAllPoints()
    if previewing and previewAnchor then
        frame:SetPoint("CENTER", previewAnchor, "CENTER")
    elseif settings.placement == "cursor" and anchor then
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
    previewing, previewAnchor = false, nil
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

-- After a settings change, or a bot dying or coming back: the ring, open or previewed, takes its new orders, layout,
-- size and place at once.
function Ring.Relayout()
    if not frame:IsShown() then
        return
    end
    Layout()
    if previewing then
        Place(previewAnchor)
        Refresh()
        return
    end
    Place(anchorFrame)
    Bind()
    Refresh()
end

-- The ring without bots, as it will look: in a space in the settings (anchor), or at its fixed spot to be dragged
-- into place (no anchor; frames unlocked, placement "fixed"). Hidden with show false.
function Ring.Preview(show, anchor)
    if not show then
        if previewing then
            previewing, previewAnchor = false, nil
            frame:Hide()
        end
        return
    end
    if not previewing or previewAnchor ~= anchor then
        frame:Hide()
    end
    previewing, previewAnchor = true, anchor
    current = nil
    hovered, pressed = nil, nil
    Layout()
    Place(anchor)
    name:SetText("Orders ring")
    hint:SetText(anchor and "" or "Drag to place it")
    frame:SetAlpha(1)
    frame:Show()
    Refresh()
end

-- While it is shown to be placed, pressing anywhere on it moves it straight away.
local moving = false
hit:SetScript("OnMouseDown", function(_, button)
    if previewing then
        if button == "LeftButton" and not previewAnchor then
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
hit:SetScript("OnClick", function(_, button)
    local under = Under()
    if type(under) == "table" then
        Give(under, button == "RightButton")
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
    previewing, previewAnchor = false, nil
    current = nil
    hovered, pressed = nil, nil
    LA.Fire("ring:changed")
end)

-- Following the mouse over the parts; closing when the player clicks anywhere else, without catching that click
-- (it still reaches whatever was under the mouse).
local wasDown = false
frame:SetScript("OnUpdate", function()
    local under = hit:IsMouseOver() and Under() or nil
    local part = type(under) == "table" and under or nil
    if part ~= hovered then
        local was = hovered
        hovered = part
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
    settings.layout = LAYOUTS[settings.layout] and settings.layout or "kinds"
    settings.movement = MOVEMENTS[settings.movement] and settings.movement or "toggle"
    settings.x = settings.x or UIParent:GetWidth() / 2
    settings.y = settings.y or UIParent:GetHeight() * 0.35
    LivingAzerothDB.ring = settings
end)

Ring.MIN_SIZE, Ring.MAX_SIZE = MIN_SIZE, MAX_SIZE
Relayout = Ring.Relayout
