-- SPDX-License-Identifier: GPL-2.0-or-later
-- The orders ring: orders around a disc that names who they are for (one bot, a raid group, all the player's
-- bots, or a selection) and what they are doing. It stays open while orders are given; clicking anywhere else,
-- Escape, or what opened it again closes it, and opening it for someone else moves it there. Number keys pick
-- orders while it is open (out of combat; the game does not let addons change key bindings during combat).
local _, LA = ...

local Ring = {}
LA.Ring = Ring

local SIZES = { small = 0.82, normal = 1, large = 1.22 }
local TILE = 36
local RADIUS = 68
local DISC = 100
local BOX = 2 * RADIUS + TILE + 8

local frame = CreateFrame("Frame", "LivingAzerothRing", UIParent)
frame:SetSize(BOX, BOX)
frame:SetFrameStrata("FULLSCREEN_DIALOG") -- above the windows it is opened from
frame:SetClampedToScreen(true)
frame:SetMovable(true)
frame:EnableMouse(false) -- only the disc and the buttons take the mouse
frame:Hide()
table.insert(UISpecialFrames, "LivingAzerothRing") -- Escape closes it

-- The disc's art has room for its shadow around it: the disc itself is 0.88 of the texture.
local disc = frame:CreateTexture(nil, "BACKGROUND")
disc:SetTexture(LA.Style.ART .. "disc")
disc:SetSize(DISC / 0.88, DISC / 0.88)
disc:SetPoint("CENTER")

-- The disc takes clicks so that clicking it does not count as clicking elsewhere.
local discButton = CreateFrame("Frame", nil, frame)
discButton:SetSize(DISC, DISC)
discButton:SetPoint("CENTER")
discButton:EnableMouse(true)

local name = frame:CreateFontString(nil, "OVERLAY")
name:SetFontObject(LA.Style.Font("heading"))
name:SetPoint("BOTTOM", frame, "CENTER", 0, 3)
name:SetWidth(DISC - 14)
local hint = frame:CreateFontString(nil, "OVERLAY")
hint:SetFontObject(LA.Style.Font("body"))
hint:SetTextColor(1, 1, 1)
hint:SetPoint("TOP", frame, "CENTER", 0, -2)
hint:SetWidth(DISC - 16)
hint:SetHeight(30)
hint:SetJustifyV("TOP")

local buttons = {}
local current -- { key, label, guids }
local hovered
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

local function Refresh()
    if previewing then
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
        hint:SetText(LA.Orders.byId[hovered].label)
    elseif waiting.any then
        hint:SetText("Waiting for " .. (#bots == 1 and bots[1].name or #bots .. " bots") .. "...")
    else
        hint:SetText(State(bots))
    end
    for _, button in ipairs(buttons) do
        if button:IsShown() then
            local id = button.order
            local offered, on = false, true
            for _, bot in ipairs(bots) do
                if LA.Orders.Offered(bot, id) then
                    offered = true
                    on = on and LA.Orders.IsOn(bot, id)
                end
            end
            button.on = offered and on
            button.tile:SetActive(button.on)
            button.tile:SetDim(not offered)
            button.waiting = waiting[id] or false
        end
    end
end

-- Where each order sits: around the disc, the first at the top. spread runs 0.7 to 1 as the ring opens.
local spread = 1
local function Position()
    local count = 0
    for _, button in ipairs(buttons) do
        if button:IsShown() then
            count = count + 1
        end
    end
    for i, button in ipairs(buttons) do
        if button:IsShown() then
            local angle = (i - 1) / count * 2 * math.pi
            local radius = RADIUS * spread
            button:ClearAllPoints()
            button:SetPoint("CENTER", frame, "CENTER", radius * math.sin(angle), radius * math.cos(angle))
        end
    end
end

local function Layout()
    local orders = Settings().orders
    for i = 1, LA.Orders.MAX_RING do
        local button = buttons[i]
        local id = orders[i]
        if id and LA.Orders.byId[id] then
            local order = LA.Orders.byId[id]
            button.order = id
            button.tile:Set(order.glyph, order.tone)
            button.hotkey:SetText(i)
            button:Show()
        else
            button:Hide()
        end
    end
    Position()
end

-- Opening: the orders spread out from the disc and fade in, as in the mockup.
local opening = CreateFrame("Frame")
local function Open()
    local started = GetTime()
    spread = 0.7
    frame:SetAlpha(0)
    opening:SetScript("OnUpdate", function(self)
        local t = math.min(1, (GetTime() - started) / 0.14)
        local eased = 1 - (1 - t) * (1 - t)
        spread = 0.7 + 0.3 * eased
        frame:SetAlpha(eased)
        Position()
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
    for i, button in ipairs(buttons) do
        if button:IsShown() then
            SetOverrideBindingClick(frame, true, tostring(i), button:GetName())
        end
    end
end

for i = 1, LA.Orders.MAX_RING do
    local button = CreateFrame("Button", "LivingAzerothRingButton" .. i, frame)
    button:SetSize(TILE, TILE)
    button.tile = LA.UI.Icon(button, TILE, "tile")
    button.tile:SetPoint("CENTER")
    button.hotkey = button:CreateFontString(nil, "OVERLAY")
    button.hotkey:SetFontObject(LA.Style.Font("small"))
    button.hotkey:SetTextColor(0.92, 0.92, 0.92)
    button.hotkey:SetPoint("TOPRIGHT", -3, -2)
    button.hotkey:SetParent(button.tile)
    local hover = button:CreateTexture(nil, "HIGHLIGHT")
    hover:SetTexture(LA.Style.ART .. "tile-glow")
    hover:SetBlendMode("ADD")
    hover:SetSize(TILE * 2, TILE * 2)
    hover:SetPoint("CENTER")
    hover:SetAlpha(0.35)
    button.glow = button:CreateTexture(nil, "OVERLAY")
    button.glow:SetTexture(LA.Style.ART .. "tile-glow")
    button.glow:SetBlendMode("ADD")
    button.glow:SetSize(TILE * 2, TILE * 2)
    button.glow:SetPoint("CENTER")
    button.glow:Hide()
    button:RegisterForClicks("AnyUp")
    button:SetScript("OnClick", function(self)
        for _, bot in ipairs(Bots()) do
            if LA.Orders.Offered(bot, self.order) then
                LA.Orders.Give(bot, self.order)
            end
        end
        Refresh() -- the bot's real state decides how it shows
    end)
    button:SetScript("OnEnter", function(self)
        hovered = self.order
        Refresh()
    end)
    button:SetScript("OnLeave", function()
        hovered = nil
        Refresh()
    end)
    button:SetScript("OnUpdate", function(self)
        if self.waiting then
            self.glow:SetAlpha(0.45 + 0.45 * math.sin(GetTime() * 9))
            self.glow:Show()
        else
            self.glow:Hide()
        end
    end)
    button:SetScript("OnMouseDown", function(self) self.tile:SetPoint("CENTER", 0, -1) end)
    button:SetScript("OnMouseUp", function(self) self.tile:SetPoint("CENTER", 0, 0) end)
    buttons[i] = button
end

-- Where the ring's centre goes, in UIParent's units.
local function Place(anchor)
    local settings = Settings()
    local scale = SIZES[settings.size] or 1
    frame:SetScale(scale)
    frame:ClearAllPoints()
    if settings.placement == "cursor" then
        local x, y = GetCursorPosition()
        local ui = UIParent:GetEffectiveScale()
        frame:SetPoint("CENTER", UIParent, "BOTTOMLEFT", x / ui / scale, y / ui / scale)
    elseif settings.placement == "fixed" then
        frame:SetPoint("CENTER", UIParent, "BOTTOMLEFT", settings.x / scale, settings.y / scale)
    else
        frame:SetPoint("LEFT", anchor, "RIGHT", 6, 0)
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
    hovered = nil
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
    hovered = nil
    Layout()
    Place(nil)
    name:SetText("Orders ring")
    hint:SetText("Drag to place it")
    for _, button in ipairs(buttons) do
        button.on = false
        button.tile:SetActive(false)
        button.tile:SetDim(false)
        button.waiting = false
    end
    frame:Show()
end

discButton:RegisterForDrag("LeftButton")
discButton:SetScript("OnDragStart", function()
    if previewing then
        frame:StartMoving()
    end
end)
discButton:SetScript("OnDragStop", function()
    if previewing then
        frame:StopMovingOrSizing()
        local x, y = frame:GetCenter()
        local scale = frame:GetScale()
        Settings().x, Settings().y = x * scale, y * scale
        Place(nil)
    end
end)

frame:SetScript("OnHide", function()
    ClearOverrideBindings(frame)
    previewing = false
    current = nil
    LA.Fire("ring:changed")
end)

-- Closing when the player clicks anywhere else, without catching that click: the click still reaches whatever
-- was under the mouse.
local wasDown = false
frame:SetScript("OnUpdate", function()
    local down = IsMouseButtonDown("LeftButton") or IsMouseButtonDown("RightButton")
    if down and not wasDown and not previewing then
        local focus = GetMouseFocus()
        local ours = focus and (focus.livingAzerothPip or focus:GetParent() == frame)
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

LA.On("loaded", function()
    local settings = LivingAzerothDB.ring or {}
    settings.placement = settings.placement or "beside"
    settings.size = settings.size or "normal"
    settings.orders = settings.orders or { unpack(LA.Orders.defaultRing) }
    settings.x = settings.x or UIParent:GetWidth() / 2
    settings.y = settings.y or UIParent:GetHeight() * 0.35
    LivingAzerothDB.ring = settings
end)
