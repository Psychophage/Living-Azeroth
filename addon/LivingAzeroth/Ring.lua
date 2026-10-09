-- SPDX-License-Identifier: GPL-2.0-or-later
-- The orders ring: a bot's orders around a disc that names the bot and what it is doing. It stays open while
-- orders are given; clicking anywhere else, Escape, or the same order icon again closes it, and clicking another
-- bot's order icon moves it there. Number keys pick orders while it is open (out of combat; the game does not
-- let addons change key bindings during combat).
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
frame:SetFrameStrata("DIALOG")
frame:SetClampedToScreen(true)
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
local current -- { guid, anchor }
local hovered

local function Settings()
    return LivingAzerothDB.ring
end

local function Bot()
    return current and LA.Bots.Mine()[current.guid]
end

local function Refresh()
    local bot = Bot()
    if not bot then
        frame:Hide()
        return
    end
    name:SetText(bot.name)
    local waiting = LA.Orders.Pending(bot.guid)
    if hovered then
        hint:SetText(LA.Orders.byId[hovered].label)
    elseif waiting then
        hint:SetText("Waiting for " .. bot.name .. "...")
    else
        hint:SetText(LA.Orders.Standing(bot).state)
    end
    for _, button in ipairs(buttons) do
        if button:IsShown() then
            local id = button.order
            local offered = LA.Orders.Offered(bot, id)
            button:SetChecked(offered and LA.Orders.IsOn(bot, id))
            button.icon:SetDesaturated(not offered)
            button:SetAlpha(offered and 1 or 0.45)
            button.waiting = waiting == id
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
        local bot = Bot()
        if bot and LA.Orders.Offered(bot, self.order) then
            LA.Orders.Give(bot, self.order)
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

-- Opens the ring for a bot beside anchor (its order icon); the same bot again closes it.
function Ring.Toggle(guid, anchor)
    if frame:IsShown() and current and current.guid == guid then
        frame:Hide()
        return
    end
    current = { guid = guid }
    hovered = nil
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

function Ring.OpenFor()
    return frame:IsShown() and current and current.guid
end

function Ring.Relayout()
    if frame:IsShown() then
        Layout()
        Bind()
        Refresh()
    end
end

frame:SetScript("OnHide", function()
    ClearOverrideBindings(frame)
    current = nil
    LA.Fire("ring:changed")
end)

-- Closing when the player clicks anywhere else, without catching that click: the click still reaches whatever
-- was under the mouse.
local wasDown = false
frame:SetScript("OnUpdate", function()
    local down = IsMouseButtonDown("LeftButton") or IsMouseButtonDown("RightButton")
    if down and not wasDown then
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
    if current and current.guid == guid then
        Refresh()
    end
end)
LA.On("bots:changed", function(guid)
    if current and current.guid == guid then
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
