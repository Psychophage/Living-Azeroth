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
local DISC = 98
local BOX = 2 * RADIUS + TILE + 8
local CIRCLE = "Interface\\CharacterFrame\\TempPortraitAlphaMask"

local frame = CreateFrame("Frame", "LivingAzerothRing", UIParent)
frame:SetSize(BOX, BOX)
frame:SetFrameStrata("FULLSCREEN_DIALOG") -- above the windows it is opened from
frame:SetClampedToScreen(true)
frame:SetMovable(true)
frame:EnableMouse(false) -- only the disc and the buttons take the mouse
frame:Hide()
table.insert(UISpecialFrames, "LivingAzerothRing") -- Escape closes it

local rim = frame:CreateTexture(nil, "BACKGROUND")
rim:SetTexture(CIRCLE)
rim:SetVertexColor(0.79, 0.64, 0.29)
rim:SetSize(DISC + 6, DISC + 6)
rim:SetPoint("CENTER")
local shadow = frame:CreateTexture(nil, "BACKGROUND", nil, -1)
shadow:SetTexture(CIRCLE)
shadow:SetVertexColor(0, 0, 0, 0.6)
shadow:SetSize(DISC + 16, DISC + 16)
shadow:SetPoint("CENTER", 0, -3)
local disc = frame:CreateTexture(nil, "BORDER")
disc:SetTexture(CIRCLE)
disc:SetVertexColor(0.07, 0.055, 0.03, 0.96)
disc:SetSize(DISC, DISC)
disc:SetPoint("CENTER")

-- The disc takes clicks so that clicking it does not count as clicking elsewhere.
local discButton = CreateFrame("Frame", nil, frame)
discButton:SetSize(DISC, DISC)
discButton:SetPoint("CENTER")
discButton:EnableMouse(true)

local name = frame:CreateFontString(nil, "OVERLAY", "GameFontNormal")
name:SetPoint("BOTTOM", frame, "CENTER", 0, 4)
name:SetWidth(DISC - 14)
local hint = frame:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
hint:SetPoint("TOP", frame, "CENTER", 0, -2)
hint:SetWidth(DISC - 16)
hint:SetHeight(28)
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
            button:SetChecked(offered and on)
            button.icon:SetDesaturated(not offered)
            button:SetAlpha(offered and 1 or 0.45)
            button.waiting = waiting[id] or false
        end
    end
end

local function Layout()
    local orders = Settings().orders
    for i = 1, LA.Orders.MAX_RING do
        local button = buttons[i]
        local id = orders[i]
        if id and LA.Orders.byId[id] then
            local angle = (i - 1) / #orders * 2 * math.pi
            button:ClearAllPoints()
            button:SetPoint("CENTER", frame, "CENTER", RADIUS * math.sin(angle), RADIUS * math.cos(angle))
            button.order = id
            button.icon:SetTexture(LA.Orders.byId[id].icon)
            button.hotkey:SetText(i)
            button:Show()
        else
            button:Hide()
        end
    end
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
    local button = CreateFrame("CheckButton", "LivingAzerothRingButton" .. i, frame, "ActionButtonTemplate")
    button.icon = _G[button:GetName() .. "Icon"]
    button.hotkey = _G[button:GetName() .. "HotKey"]
    button.hotkey:SetVertexColor(0.9, 0.9, 0.9)
    button.glow = button:CreateTexture(nil, "OVERLAY")
    button.glow:SetTexture("Interface\\Buttons\\CheckButtonHilight")
    button.glow:SetBlendMode("ADD")
    button.glow:SetAllPoints()
    button.glow:Hide()
    button:RegisterForClicks("AnyUp")
    button:SetScript("OnClick", function(self)
        for _, bot in ipairs(Bots()) do
            if LA.Orders.Offered(bot, self.order) then
                LA.Orders.Give(bot, self.order)
            end
        end
        Refresh() -- a click toggles the check; the bot's real state decides it
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
            self.glow:SetAlpha(0.35 + 0.35 * math.sin(GetTime() * 9))
            self.glow:Show()
        else
            self.glow:Hide()
        end
    end)
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
        button:SetChecked(false)
        button.icon:SetDesaturated(false)
        button:SetAlpha(1)
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
