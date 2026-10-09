-- SPDX-License-Identifier: GPL-2.0-or-later
-- The formation window: pick a formation and the preview's bots glide to where they would stand around the player;
-- they move in the world only once the player uses it. It applies to the bots the Tactics tab is set to.
local _, LA = ...

local Formation = {}
LA.Formation = Formation

local ROLES = "Interface\\LFGFrame\\UI-LFG-ICON-ROLES"

local FORMATIONS = {
    { id = "near", name = "Near", text = "Close around you at follow distance." },
    { id = "far", name = "Far", text = "Hang back at a distance and keep out of the way.",
        pos = { { 0, 86 }, { -44, 118 }, { 44, 118 }, { -90, 100 }, { 90, 100 } } },
    { id = "arrow", name = "Arrow", text = "Tanks in front, damage in the middle, healers behind.",
        pos = { { 0, -96 }, { 0, 58 }, { -50, -50 }, { 50, -50 }, { 0, -54 } } },
    { id = "queue", name = "Queue", text = "Single file behind you.",
        pos = { { 0, 30 }, { 0, 58 }, { 0, 86 }, { 0, 114 }, { 0, 142 } } },
    { id = "circle", name = "Circle", text = "A ring around your target, or around you when there is none." },
    { id = "line", name = "Line", text = "Side by side, with you in the middle.",
        pos = { { -96, 0 }, { -48, 0 }, { 48, 0 }, { 96, 0 }, { 144, 0 } } },
    { id = "shield", name = "Shield", text = "An arc in front of you." },
    { id = "melee", name = "Melee", text = "Stay tight on the group leader for melee fights." },
    { id = "chaos", name = "Chaos", text = "Loosely scattered around you.",
        pos = { { -104, -66 }, { 84, -82 }, { 126, 40 }, { -90, 78 }, { 28, 116 } } },
}

-- Five places on a circle or an arc around the player (x right, y towards the back), as in the mockups.
local function Arc(radius, start, span)
    local places = {}
    for i = 0, 4 do
        local angle = math.rad(start + span * i / (span >= 360 and 5 or 4))
        places[#places + 1] = { radius * math.cos(angle), radius * math.sin(angle) }
    end
    return places
end
FORMATIONS[1].pos = Arc(48, -90, 360)
FORMATIONS[5].pos = Arc(84, -90, 360)
FORMATIONS[7].pos = Arc(84, -160, 140)
FORMATIONS[8].pos = Arc(28, -90, 360)

local PREVIEW = 150 -- the preview's size; formations span about 300 units
local SCALE = PREVIEW / 330

local window
local chosen = "near"
local cards = {}
local dots = {}

local function Find(id)
    for _, formation in ipairs(FORMATIONS) do
        if formation.id == id then
            return formation
        end
    end
    return FORMATIONS[1]
end

-- The targets' current formation, if they share one.
local function Current()
    local bots = LA.Tactics.Targets()
    local current = bots[1] and bots[1].formation
    for _, bot in ipairs(bots) do
        if bot.formation ~= current then
            return nil
        end
    end
    return current
end

local function RoleCoords(role)
    return GetTexCoordsForRole(role == "tank" and "TANK" or role == "healer" and "HEALER" or "DAMAGER")
end

-- Each dot glides from where it is to its place in the chosen formation.
local function Animate(self, elapsed)
    local moving = false
    for _, dot in ipairs(dots) do
        if dot.tx then
            local step = math.min(1, elapsed * 6)
            dot.x = dot.x + (dot.tx - dot.x) * step
            dot.y = dot.y + (dot.ty - dot.y) * step
            if math.abs(dot.tx - dot.x) + math.abs(dot.ty - dot.y) > 0.3 then
                moving = true
            else
                dot.x, dot.y = dot.tx, dot.ty
            end
            dot:SetPoint("CENTER", window.preview, "CENTER", dot.x, dot.y)
        end
    end
    if not moving then
        self:SetScript("OnUpdate", nil)
    end
end

local function Refresh()
    if not window or not window:IsShown() then
        return
    end
    local formation = Find(chosen)
    local current = Current()
    window.name:SetText(formation.name)
    window.text:SetText(formation.text)
    for _, card in ipairs(cards) do
        LA.SetShown(card.chosen, card.formation.id == chosen)
        LA.SetShown(card.current, card.formation.id == current)
    end
    local bots = LA.Tactics.Targets()
    for i, dot in ipairs(dots) do
        local place, bot = formation.pos[i], bots[i]
        if place and (bot or #bots == 0) then
            dot.tx, dot.ty = place[1] * SCALE, -place[2] * SCALE
            dot.x, dot.y = dot.x or 0, dot.y or 0
            dot:SetTexCoord(RoleCoords(bot and bot.role))
            dot:Show()
        else
            dot:Hide()
        end
    end
    window.preview:SetScript("OnUpdate", Animate)

    local waiting = false
    for _, bot in ipairs(bots) do
        waiting = waiting or LA.Orders.IsPending(bot.guid, "formation")
    end
    window.use:SetText("Use " .. formation.name)
    if #bots == 0 then
        window.status:SetText("No bots to move.")
        window.use:Disable()
    elseif waiting then
        window.status:SetText("Moving...")
        window.use:Disable()
    elseif current == chosen then
        window.status:SetText("Your bots are in " .. formation.name .. ".")
        window.use:Disable()
    else
        window.status:SetText(current and ("Now in " .. Find(current).name .. ".") or "")
        window.use:Enable()
    end
end

local function Card(parent, formation)
    local card = CreateFrame("Button", nil, parent)
    card:SetSize(46, 46)
    local background = card:CreateTexture(nil, "BACKGROUND")
    background:SetTexture(0, 0, 0, 0.35)
    background:SetAllPoints()
    local function Dot(x, y, size, r, g, b)
        local dot = card:CreateTexture(nil, "ARTWORK")
        dot:SetTexture("Interface\\CharacterFrame\\TempPortraitAlphaMask")
        dot:SetVertexColor(r, g, b)
        dot:SetSize(size, size)
        dot:SetPoint("CENTER", x, y)
    end
    Dot(0, 0, 8, 1, 0.82, 0)
    for _, place in ipairs(formation.pos) do
        Dot(place[1] * 0.13, -place[2] * 0.13, 6, 0.9, 0.9, 0.9)
    end
    card.chosen = card:CreateTexture(nil, "OVERLAY")
    card.chosen:SetTexture("Interface\\Buttons\\CheckButtonHilight")
    card.chosen:SetBlendMode("ADD")
    card.chosen:SetAllPoints()
    card.current = card:CreateTexture(nil, "OVERLAY")
    card.current:SetTexture("Interface\\RaidFrame\\ReadyCheck-Ready")
    card.current:SetSize(12, 12)
    card.current:SetPoint("TOPRIGHT", -1, -1)
    card:SetHighlightTexture("Interface\\Buttons\\ButtonHilight-Square", "ADD")
    local label = card:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    label:SetPoint("TOP", card, "BOTTOM", 0, -1)
    label:SetText(formation.name)
    card.formation = formation
    card:SetScript("OnClick", function()
        chosen = formation.id
        Refresh()
    end)
    return card
end

local function Build()
    local UI = LA.UI
    window = LA.Window.Finder("LivingAzerothFormation", "Formation", "Interface\\Icons\\Ability_Warrior_BattleShout")
    window:SetPoint("CENTER", 420, 40)

    window.name = window.header:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
    window.name:SetPoint("TOPLEFT", 58, -10)
    window.text = UI.Text(window.header, 250)
    window.text:SetPoint("TOPLEFT", window.name, "BOTTOMLEFT", 0, -6)

    local hint = window.bar:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    hint:SetPoint("CENTER", 0, 0)
    hint:SetText("Pick one to see it; your bots move when you use it.")

    -- The preview: the player in the middle facing up, and each bot by its role.
    window.preview = CreateFrame("Frame", nil, window.content)
    window.preview:SetSize(PREVIEW, PREVIEW)
    window.preview:SetPoint("TOPLEFT", 4, -40)
    local facing = UI.Dark(window.preview, "small")
    facing:SetPoint("BOTTOM", window.preview, "TOP", 0, 6)
    facing:SetText("You, facing up")
    local you = window.preview:CreateTexture(nil, "ARTWORK")
    you:SetTexture(ROLES)
    you:SetTexCoord(GetTexCoordsForRole("GUIDE"))
    you:SetSize(24, 24)
    you:SetPoint("CENTER")
    for i = 1, 5 do
        local dot = window.preview:CreateTexture(nil, "OVERLAY")
        dot:SetTexture(ROLES)
        dot:SetSize(20, 20)
        dot:SetPoint("CENTER")
        dots[i] = dot
    end

    for i, formation in ipairs(FORMATIONS) do
        local card = Card(window.content, formation)
        local column, row = (i - 1) % 3, math.floor((i - 1) / 3)
        card:SetPoint("TOPLEFT", 164 + column * 50, -8 - row * 76)
        cards[i] = card
    end

    window.use = UI.Button(window.footer, "Use", 110)
    window.use:SetPoint("LEFT", 0, 1)
    window.use:SetScript("OnClick", function()
        for _, bot in ipairs(LA.Tactics.Targets()) do
            if bot.formation ~= chosen then
                LA.Orders.Give(bot, "formation", nil, { formation = chosen })
            end
        end
        Refresh()
    end)
    window.status = window.footer:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    window.status:SetPoint("LEFT", window.use, "RIGHT", 8, 0)
    window.status:SetWidth(120)
    window.status:SetJustifyH("LEFT")
    local close = UI.Button(window.footer, CLOSE, 80)
    close:SetPoint("RIGHT", -8, 1)
    close:SetScript("OnClick", function() window:Hide() end)
    window:SetScript("OnShow", Refresh)
end

function Formation.Toggle()
    if not window then
        Build()
    end
    if window:IsShown() then
        window:Hide()
    else
        chosen = Current() or chosen
        window:Show()
    end
end

LA.On("bots:changed", Refresh)
LA.On("orders:changed", Refresh)
