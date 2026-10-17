-- SPDX-License-Identifier: GPL-2.0-or-later
-- The addon's look: colours, fonts and the recipe of every surface (window, buttons, tabs, rows, fields), which
-- Draw.lua draws on whole pixels. Two variants: the default one in dark leather and bronze, and a cooler graphite
-- one that sits with DragonUI (chosen when it is on). Sizes in recipes are screen pixels.
local _, LA = ...

local Style = {}
LA.Style = Style

Style.ART = "Interface\\AddOns\\LivingAzeroth\\Art\\"
Style.WHITE = "Interface\\Buttons\\WHITE8X8"

-- "#rrggbb" (and an alpha) as r, g, b, a.
local function C(hex, alpha)
    return { tonumber(hex:sub(2, 3), 16) / 255, tonumber(hex:sub(4, 5), 16) / 255,
        tonumber(hex:sub(6, 7), 16) / 255, alpha or 1 }
end
Style.C = C

Style.gold = C("#ffd100")
Style.paleGold = C("#fff1a8")
Style.green = C("#4fe05a")
Style.red = C("#ff6a55")

local function Black(alpha)
    return C("#000000", alpha)
end

-- The parts both variants share: a soft shadow under windows, the red button.
local SHADOW = { { 1, Black(0.04) }, { 1, Black(0.08) }, { 1, Black(0.14) }, { 1, Black(0.22) }, { 1, Black(0.34) } }
local RED = {
    up = { C("#a82416"), C("#5e0f08") },
    down = { C("#5a0d07"), C("#801a0f") },
}
local TOP = { TOPLEFT = true, TOPRIGHT = true } -- square top corners, for tabs hanging under a window

-- A dark one-pixel outline, and a sunken edge (dark above, light below).
local function Outline()
    return { 1, Black(0.9) }
end
local function Sunken(alpha)
    return { 1, { Black(alpha), C("#ffffff", 0.07) } }
end

local THEMES = {
    default = {
        text = C("#ebe2cc"), muted = C("#9b917f"), faint = C("#6f675a"), label = C("#e6cf8a"), title = C("#f2e6c8"),
        divider = C("#3c3223"), rule = C("#8a6a30"), stripe = C("#ffffff", 0.03),
        radius = 7,
        frame = { { 1, Black(0.95) }, { 1, { C("#e2c27a"), C("#8a6630") } }, { 1, { C("#a8823e"), C("#5c421c") } },
            { 1, { C("#6e5226"), C("#3a2a12") } }, { 1, Black(0.85) } },
        fill = { C("#221b14", 0.97), C("#0e0c0a", 0.97) },
        band = { C("#2c2318"), C("#16120d") },
        rim = { C("#d9b064"), C("#7a5a26") }, -- gold rims of buttons and chosen things
        plainRim = { C("#7a6438"), C("#4a3c22") }, plain = { C("#2a241b"), C("#17140f") },
        chosen = { C("#4a3816"), C("#2a200e") },
        off = { C("#3a3532"), C("#25211e") }, offRim = C("#4a443b"),
        tab = { C("#18130e"), C("#0d0b08") }, tabOn = { C("#3a2d1a"), C("#1d170f") },
        tabRim = { C("#5e4a2a"), C("#3e311c") },
        field = { C("#060504"), C("#0f0d0b") }, fieldRim = { Black(0.9), C("#5e4c2c") },
        cell = C("#0c0a08", 0.92), cellEmpty = C("#1c1813"),
    },
    dragon = {
        text = C("#ece8e0"), muted = C("#9c9a94"), faint = C("#6c6b67"), label = C("#e2cf98"), title = C("#efe9dc"),
        divider = C("#33353d"), rule = C("#7d6532"), stripe = C("#ffffff", 0.03),
        radius = 10,
        frame = { { 1, Black(0.95) }, { 1, { C("#e0c47c"), C("#7d6532") } }, { 1, Black(0.7) } },
        fill = { C("#24262c", 0.97), C("#121317", 0.97) },
        band = { C("#2d2f36"), C("#1b1c21") },
        rim = { C("#e0c47c"), C("#8a6e36") },
        plainRim = { C("#5c5e66"), C("#3c3e45") }, plain = { C("#2c2e35"), C("#1c1d22") },
        chosen = { C("#45391e"), C("#2a2414") },
        off = { C("#2e2f33"), C("#202125") }, offRim = C("#3e3f44"),
        tab = { C("#1d1e23"), C("#121317") }, tabOn = { C("#34302a"), C("#1f1d1a") },
        tabRim = { C("#4a4c54"), C("#33353b") },
        field = { C("#08090b"), C("#111216") }, fieldRim = { Black(0.9), C("#4a4c54") },
        cell = C("#0b0c0e", 0.92), cellEmpty = C("#1a1b20"),
    },
}

-- The variant in use, settled the first time a window is built (by then every addon has loaded).
local theme
function Style.T()
    if not theme then
        theme = THEMES[IsAddOnLoaded("DragonUI") and "dragon" or "default"]
    end
    return theme
end

-- The window frame's thickness in px.
function Style.Rim()
    local px = 0
    for _, edge in ipairs(Style.T().frame) do
        px = px + edge[1]
    end
    return px
end

-- Surface recipes for Draw.Box, by kind.
local RECIPES = {
    window = function(T)
        local edges = {}
        for _, edge in ipairs(SHADOW) do
            edges[#edges + 1] = edge
        end
        for _, edge in ipairs(T.frame) do
            edges[#edges + 1] = edge
        end
        return { radius = T.radius + #SHADOW, outset = #SHADOW, edges = edges, fill = T.fill, grain = 0.045 }
    end,
    band = function(T)
        return { radius = T.radius - Style.Rim(), square = { BOTTOMLEFT = true, BOTTOMRIGHT = true }, fill = T.band }
    end,
    well = function() return { radius = 4, edges = { Sunken(0.45) }, fill = Black(0.24) } end,
    row = function(T) return { radius = 3, fill = T.stripe } end,
    rowOn = function(T) return { radius = 3, edges = { { 1, T.rim } }, fill = C("#ffd100", 0.09) } end,
    button = function(T)
        local light = { 1, { C("#ff9a7a", 0.4), Black(0) } } -- a lit top edge fading down the sides
        return { radius = 3, edges = { Outline(), { 1, T.rim }, light }, fill = RED.up }
    end,
    buttonDown = function(T) return { radius = 3, edges = { Outline(), { 1, T.rim } }, fill = RED.down } end,
    off = function(T) return { radius = 3, edges = { Outline(), { 1, T.offRim } }, fill = T.off } end,
    plain = function(T) return { radius = 3, edges = { Outline(), { 1, T.plainRim } }, fill = T.plain } end,
    plainDown = function(T)
        return { radius = 3, edges = { Outline(), { 1, T.plainRim } }, fill = { T.plain[2], T.plain[1] } }
    end,
    chosen = function(T) return { radius = 3, edges = { Outline(), { 1, T.rim } }, fill = T.chosen } end,
    close = function(T) return { radius = 3, edges = { Outline(), { 1, T.rim } }, fill = RED.up } end,
    closeDown = function(T) return { radius = 3, edges = { Outline(), { 1, T.rim } }, fill = RED.down } end,
    tab = function(T) return { radius = 5, square = TOP, edges = { Outline(), { 1, T.tabRim } }, fill = T.tab } end,
    tabOn = function(T) return { radius = 5, square = TOP, edges = { Outline(), { 1, T.rim } }, fill = T.tabOn } end,
    field = function(T) return { radius = 2, edges = { { 1, T.fieldRim } }, fill = T.field } end,
    fieldFocus = function(T) return { radius = 2, edges = { { 1, { Black(0.9), T.rim[2] } } }, fill = T.field } end,
    thumb = function()
        return { radius = 2, edges = { Outline(), { 1, { C("#fff0b0"), C("#a07a30") } } },
            fill = { C("#e8c058"), C("#94701f") } }
    end,
    bar = function() return { radius = 2, fill = C("#c9a24a", 0.6) } end,
    radio = function(T) return { radius = "round", edges = { Outline(), { 1, T.plainRim } }, fill = T.field } end,
    dot = function() return { radius = "round", fill = { C("#fff1a8"), C("#d9a520") } } end,
    rim = function() return { edges = { Sunken(0.7) } } end,
    mover = function(T) return { radius = 3, edges = { Outline(), { 1, T.rim } }, fill = C("#ffd36b", 0.04) } end,
    cell = function(T) return { edges = { { 1, Black(1) } }, fill = T.cell } end,
}
local recipes = {}

-- A surface recipe for Draw.Box, made once per kind.
function Style.Surface(kind)
    if not recipes[kind] then
        recipes[kind] = RECIPES[kind](Style.T())
    end
    return recipes[kind]
end

-- Fonts: Friz Quadrata (the game's own) for titles, headings, labels and buttons; Arial Narrow for running text.
-- Sizes are units of a fitted window (two screen pixels each on a 1440 screen).
local FRIZ = "Fonts\\FRIZQT__.TTF"
local NARROW = "Fonts\\ARIALN.TTF"
local FONTS = {
    title = { FRIZ, 11, "title" },
    heading = { FRIZ, 10.5, Style.gold },
    name = { FRIZ, 13, Style.gold },
    label = { FRIZ, 9.5, "text" },
    value = { FRIZ, 9.5, "text" },
    body = { NARROW, 11, "text" },
    small = { NARROW, 10, "muted" },
    button = { FRIZ, 9.5, Style.gold },
    tab = { FRIZ, 10, "muted" },
    cell = { NARROW, 10.5, C("#ffffff") },
    cellSmall = { NARROW, 9, "text" },
}
local fonts = {}

-- A font object for a kind of text, made once.
function Style.Font(kind)
    if not fonts[kind] then
        local spec = FONTS[kind] or FONTS.body
        local font = CreateFont("LivingAzerothFont_" .. kind)
        font:SetFont(spec[1], spec[2])
        font:SetShadowColor(0, 0, 0, 0.9)
        font:SetShadowOffset(0.5, -0.5)
        local colour = type(spec[3]) == "string" and Style.T()[spec[3]] or spec[3]
        font:SetTextColor(colour[1], colour[2], colour[3])
        fonts[kind] = font
    end
    return fonts[kind]
end

-- The tones orders and roles come in: a light colour for a glyph on a dark ground, and the middle of the tile's
-- gradient (make_art.py), for a ground in that tone.
Style.TONES = {
    gold = { light = C("#ffd36b"), middle = C("#82571a") },
    bronze = { light = C("#e8b27a"), middle = C("#5f3a17") },
    steel = { light = C("#9fc2ff"), middle = C("#33507a") },
    green = { light = C("#8ee27c"), middle = C("#2f6d2b") },
    red = { light = C("#ff8a6a"), middle = C("#7d2116") },
    violet = { light = C("#d2a8ff"), middle = C("#52307c") },
    blue = { light = C("#8fd0ff"), middle = C("#215f92") },
    teal = { light = C("#8fe8dc"), middle = C("#216f66") },
    stone = { light = C("#d8d0c0"), middle = C("#4c4840") },
}

-- Order and tactic glyphs with their tile colours, as the mockups drew them.
Style.ORDER_LOOK = {
    follow = { "follow", "gold" },
    stay = { "stay", "gold" },
    guard = { "guard", "steel" },
    attack = { "attack", "red" },
    pull = { "pull", "red" },
    flee = { "flee", "stone" },
    release = { "up", "blue" },
    runback = { "spirit", "blue" },
    rest = { "food", "teal" },
    passive = { "passive", "violet" },
    loot = { "loot", "bronze" },
    join = { "regroup", "bronze" },
    free = { "bot", "stone" },
}
Style.ROLE_LOOK = {
    tank = { "tank", "steel", C("#7fa6e0") },
    healer = { "healer", "green", C("#7fe07a") },
    damage = { "damage", "red", C("#ff8a6a") },
}
