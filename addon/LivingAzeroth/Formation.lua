-- SPDX-License-Identifier: GPL-2.0-or-later
-- The formation window: pick a formation and the preview's bots glide to where they would stand around the player;
-- they move in the world only once the player uses it. It applies to the bots the Tactics tab is set to.
local _, LA = ...

local Formation = {}
LA.Formation = Formation

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

local UI, Style = LA.UI, LA.Style

local WIDTH, HEIGHT = 480, 330
local CARD, MINI = 0.13, 0.5 -- yards to the card's and the preview's units

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

-- Each medal glides from where it is to its place in the chosen formation.
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
        local on = card.formation.id == chosen
        card.surface:Set(on and "rowOn" or "row")
        local colour = on and Style.gold or Style.T().text
        card.label:SetTextColor(colour[1], colour[2], colour[3])
        LA.SetShown(card.current, card.formation.id == current)
    end
    local bots = LA.Tactics.Targets()
    for i, dot in ipairs(dots) do
        local place, bot = formation.pos[i], bots[i]
        if place and (bot or #bots == 0) then
            dot.tx, dot.ty = LA.Draw.Round(place[1] * MINI), LA.Draw.Round(-place[2] * MINI)
            dot.x, dot.y = dot.x or 0, dot.y or 0
            local look = Style.ROLE_LOOK[bot and bot.role or (i == 1 and "tank" or i == 2 and "healer" or "damage")]
            dot:Set(look[1], look[2])
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
    window.use:FitText(24)
    local status, colour = "", Style.T().muted
    if #bots == 0 then
        status = "No bots with you to move."
        window.use:Disable()
    elseif waiting then
        status, colour = "Moving...", Style.gold
        window.use:Disable()
    elseif current == chosen then
        status, colour = "Your bots are in " .. formation.name .. ".", Style.green
        window.use:Disable()
    else
        status = current and ("Now in " .. Find(current).name .. ".") or ""
        window.use:Enable()
    end
    window.status:SetText(status)
    window.status:SetTextColor(colour[1], colour[2], colour[3])
    LA.SetShown(window.done, current == chosen and #bots > 0 and not waiting)
end

local function Card(parent, formation, width)
    local card = CreateFrame("Button", nil, parent)
    card:SetSize(width, 58)
    UI.Surface(card, "row")
    UI.HoverLight(card)
    local mini = card:CreateTexture(nil, "ARTWORK")
    mini:SetTexture(Style.ART .. "field")
    mini:SetSize(width - 10, 36)
    mini:SetPoint("TOP", 0, -5)
    local function Dot(x, y, size, colour)
        local dot = card:CreateTexture(nil, "OVERLAY")
        dot:SetTexture(Style.ART .. "dot")
        UI.Tint(dot, colour)
        dot:SetSize(size, size)
        dot:SetPoint("CENTER", mini, "CENTER", LA.Draw.Round(x), LA.Draw.Round(y))
    end
    Dot(0, 0, 6, Style.gold)
    for i, place in ipairs(formation.pos) do
        local look = Style.ROLE_LOOK[i == 1 and "tank" or i == 2 and "healer" or "damage"]
        Dot(place[1] * CARD, -place[2] * CARD, 5, look[3])
    end
    card.label = UI.Text(card, "label", width - 4)
    card.label:SetJustifyH("CENTER")
    card.label:SetWordWrap(false)
    card.label:SetPoint("BOTTOM", 0, 4)
    card.label:SetText(formation.name)
    card.current = UI.Icon(card, 10, "plain")
    card.current:SetPoint("TOPRIGHT", -3, -3)
    card.current:Set("check", Style.green)
    card.formation = formation
    card:SetScript("OnClick", function()
        chosen = formation.id
        Refresh()
    end)
    return card
end

local function Build()
    window = LA.Window.Panel("LivingAzerothFormation", WIDTH, HEIGHT, "Formation", "formation")
    local body = window.body
    local width, height = LA.Window.BodySize(WIDTH, HEIGHT)
    local cardWidth, gap = 66, 4
    for i, formation in ipairs(FORMATIONS) do
        local card = Card(body, formation, cardWidth)
        local column, row = (i - 1) % 3, math.floor((i - 1) / 3)
        card:SetPoint("TOPLEFT", column * (cardWidth + gap), -row * (card:GetHeight() + gap))
        cards[i] = card
    end

    local left = 3 * cardWidth + 2 * gap + 12
    local field = CreateFrame("Frame", nil, body)
    field:SetPoint("TOPLEFT", left, 0)
    field:SetSize(width - left, 182)
    local ground = field:CreateTexture(nil, "BACKGROUND")
    ground:SetTexture(Style.ART .. "field")
    ground:SetAllPoints()
    local rim = CreateFrame("Frame", nil, field)
    rim:SetAllPoints()
    UI.Surface(rim, "rim")
    local facing = UI.Text(field, "small")
    facing:SetPoint("TOPLEFT", 6, -5)
    facing:SetText("Facing up")
    window.preview = CreateFrame("Frame", nil, field)
    window.preview:SetAllPoints()
    local you = UI.Icon(window.preview, 22, "medal")
    you:SetPoint("CENTER")
    you:Set("leader", "gold")
    local youText = UI.Text(window.preview, "small")
    youText:SetPoint("TOP", you, "BOTTOM", 0, -1)
    youText:SetTextColor(1, 0.82, 0)
    youText:SetText("You")
    for i = 1, 5 do
        local dot = UI.Icon(window.preview, 18, "medal")
        dot:SetPoint("CENTER")
        dot:SetFrameLevel(you:GetFrameLevel() + 1)
        dots[i] = dot
    end

    window.name = UI.Text(body, "name")
    window.name:SetPoint("TOPLEFT", field, "BOTTOMLEFT", 0, -8)
    window.text = UI.Text(body, "label", width - left)
    window.text:SetPoint("TOPLEFT", window.name, "BOTTOMLEFT", 0, -3)

    window.use = UI.Button(body, "Use", 90)
    window.use:SetPoint("BOTTOMLEFT", left, 0)
    window.use:SetScript("OnClick", function()
        for _, bot in ipairs(LA.Tactics.Targets()) do
            if bot.formation ~= chosen then
                LA.Orders.Give(bot, "formation", nil, { formation = chosen })
            end
        end
        Refresh()
    end)
    window.done = UI.Icon(body, 12, "plain")
    window.done:SetPoint("LEFT", window.use, "RIGHT", 8, 0)
    window.done:Set("check", Style.green)
    window.status = UI.Text(body, "small")
    window.status:SetPoint("LEFT", window.use, "RIGHT", 26, 0)
    window.status:SetPoint("RIGHT", body, "RIGHT")
    window.status:SetWordWrap(false)
    window:HookScript("OnShow", Refresh)
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
