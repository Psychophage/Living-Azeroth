-- SPDX-License-Identifier: GPL-2.0-or-later
-- The formation window: pick a formation to preview where the bots stand around the player; they move only
-- once the player uses it. It applies to the bots the Tactics tab is set to.
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
        pos = { { -116, 0 }, { -58, 0 }, { 58, 0 }, { 116, 0 }, { 150, 0 } } },
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

local window
local chosen = "near"
local cards = {}
local dots = {}
local sent -- the formation last used, until every target shows it

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
    local token = role == "tank" and "TANK" or role == "healer" and "HEALER" or "DAMAGER"
    return GetTexCoordsForRole(token)
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
    local scale = 0.85
    for i, dot in ipairs(dots) do
        local place, bot = formation.pos[i], bots[i]
        if place and (bot or #bots == 0) then
            dot:ClearAllPoints()
            dot:SetPoint("CENTER", window.preview, "CENTER", place[1] * scale, -place[2] * scale)
            dot:SetTexCoord(RoleCoords(bot and bot.role))
            dot:Show()
        else
            dot:Hide()
        end
    end
    local waiting = false
    for _, bot in ipairs(bots) do
        if LA.Orders.IsPending(bot.guid, "formation") then
            waiting = true
        end
    end
    if #bots == 0 then
        window.status:SetText("No bots to move.")
        window.use:Disable()
    elseif waiting then
        window.status:SetText("Moving...")
        window.use:Disable()
    elseif current == chosen then
        local tick = "|TInterface\\RaidFrame\\ReadyCheck-Ready:14|t "
        window.status:SetText(tick .. "Your bots are in " .. formation.name .. ".")
        window.use:Disable()
    else
        window.status:SetText("")
        window.use:Enable()
    end
    window.use:SetText("Use " .. formation.name)
end

local function Card(parent, formation)
    local card = CreateFrame("Button", nil, parent)
    card:SetSize(64, 64)
    local background = card:CreateTexture(nil, "BACKGROUND")
    background:SetTexture("Interface\\Buttons\\WHITE8X8")
    background:SetVertexColor(0, 0, 0, 0.45)
    background:SetAllPoints()
    local you = card:CreateTexture(nil, "ARTWORK")
    you:SetTexture("Interface\\CharacterFrame\\TempPortraitAlphaMask")
    you:SetVertexColor(1, 0.82, 0)
    you:SetSize(8, 8)
    you:SetPoint("CENTER")
    for _, place in ipairs(formation.pos) do
        local dot = card:CreateTexture(nil, "ARTWORK")
        dot:SetTexture("Interface\\CharacterFrame\\TempPortraitAlphaMask")
        dot:SetVertexColor(0.9, 0.9, 0.9)
        dot:SetSize(6, 6)
        dot:SetPoint("CENTER", place[1] * 0.17, -place[2] * 0.17)
    end
    card.chosen = card:CreateTexture(nil, "OVERLAY")
    card.chosen:SetTexture("Interface\\Buttons\\CheckButtonHilight")
    card.chosen:SetBlendMode("ADD")
    card.chosen:SetAllPoints()
    card.current = card:CreateTexture(nil, "OVERLAY")
    card.current:SetTexture("Interface\\RaidFrame\\ReadyCheck-Ready")
    card.current:SetSize(14, 14)
    card.current:SetPoint("TOPRIGHT", -2, -2)
    card:SetHighlightTexture("Interface\\Buttons\\ButtonHilight-Square", "ADD")
    local label = card:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    label:SetPoint("TOP", card, "BOTTOM", 0, -2)
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
    window = LA.Window.Create("LivingAzerothFormation", 560, 500, "Formation",
        "Interface\\Icons\\Ability_Warrior_BattleShout")
    window:SetPoint("CENTER", 380, 40)

    window.name = UI.Heading(window.header)
    window.name:SetPoint("TOPLEFT", 64, -14)
    window.text = UI.Text(window.header, 340)
    window.text:SetPoint("TOPLEFT", window.name, "BOTTOMLEFT", 0, -4)

    local hint = window.bar:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    hint:SetPoint("LEFT", 12, 0)
    hint:SetText("Pick one to see it; your bots move only when you use it.")

    -- The preview: the player in gold, facing up, and each target bot by role.
    window.preview = CreateFrame("Frame", nil, window.content)
    window.preview:SetSize(280, 280)
    window.preview:SetPoint("LEFT", 0, 0)
    local facing = window.preview:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
    facing:SetPoint("TOP", 0, -2)
    facing:SetText("Facing up")
    local you = window.preview:CreateTexture(nil, "ARTWORK")
    you:SetTexture(ROLES)
    you:SetTexCoord(GetTexCoordsForRole("GUIDE"))
    you:SetSize(30, 30)
    you:SetPoint("CENTER")
    for i = 1, 5 do
        local dot = window.preview:CreateTexture(nil, "OVERLAY")
        dot:SetTexture(ROLES)
        dot:SetSize(26, 26)
        dots[i] = dot
    end

    for i, formation in ipairs(FORMATIONS) do
        local card = Card(window.content, formation)
        local column, row = (i - 1) % 3, math.floor((i - 1) / 3)
        card:SetPoint("TOPLEFT", 290 + column * 74, -10 - row * 88)
        cards[i] = card
    end

    window.use = UI.Button(window.footer, "Use", 130)
    window.use:SetPoint("LEFT", 0, 0)
    window.use:SetScript("OnClick", function()
        for _, bot in ipairs(LA.Tactics.Targets()) do
            if bot.formation ~= chosen then
                LA.Orders.Give(bot, "formation", nil, { formation = chosen })
            end
        end
        Refresh()
    end)
    window.status = window.footer:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    window.status:SetPoint("LEFT", window.use, "RIGHT", 10, 0)
    local close = UI.Button(window.footer, CLOSE, 90)
    close:SetPoint("RIGHT", 0, 0)
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
