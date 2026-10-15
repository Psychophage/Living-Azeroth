-- SPDX-License-Identifier: GPL-2.0-or-later
-- "All my bots": a small button of its own, for the party and the raid alike, that opens the orders ring for every
-- bot of the player's in the group. It stands where the player puts it ("Move frames"), apart from the party and raid
-- frames, at the size and in the look they choose in the settings. While bots are picked in the raid frames
-- (shift-click), "Orders for N selected" and "Clear" show beside it.
local _, LA = ...

local AllBots = {}
LA.AllBots = AllBots

local UI, Style, Draw = LA.UI, LA.Style, LA.Draw

-- The button's sizes as { least, most, default }: the medal's side, in units of a fitted frame.
AllBots.SIZE = { 16, 40, 22 }
-- How it looks: the medal with the name on a plate beside it, the medal alone, or the name alone.
AllBots.LOOKS = { both = true, medal = true, name = true }

local frame = CreateFrame("Frame", "LivingAzerothAllBots", UIParent)
Draw.Fit(frame)
frame:SetSize(1, 1)
frame:SetMovable(true)
frame:SetClampedToScreen(true)
frame:Hide()

local function Settings()
    return LivingAzerothDB.allBots
end

-- Builds the button (on the screen, and as a preview in the settings): a round bot medal over the left end of a
-- dark plate with the name. button:Arrange() lays it out for the player's size and look.
local function MakeButton(parent)
    local button = CreateFrame("Button", nil, parent)
    button.livingAzerothPip = true -- clicking it does not close the ring
    local plate = CreateFrame("Frame", nil, button)
    UI.Surface(plate, "chosen")
    plate.hover = CreateFrame("Frame", nil, plate)
    plate.hover:SetAllPoints()
    plate.hover:Hide()
    Draw.Box(plate.hover, { radius = 3, fill = Style.C("#ffe8b0", 0.10) })
    local label = UI.Text(plate, "button")
    label:SetText("All my bots")
    local medal = UI.Icon(button, 22, "medal")
    medal:Set("bot", "gold")
    medal:SetFrameLevel(plate:GetFrameLevel() + 2)
    local glow = UI.MedalGlow(medal, 22, "OVERLAY")
    glow:Hide()
    local hover = UI.MedalGlow(button, 22, "HIGHLIGHT")
    hover:ClearAllPoints()
    hover:SetPoint("CENTER", medal)
    hover:SetAlpha(0.45)
    button:HookScript("OnEnter", function() plate.hover:Show() end)
    button:HookScript("OnLeave", function() plate.hover:Hide() end)
    button.glow = glow

    function button:Arrange()
        local settings = Settings()
        local look, side = settings.look, settings.size
        local k = side / AllBots.SIZE[3] -- the default size is 1
        local plateHeight = Draw.Round(16 * k)
        UI.TextSize(label, 9.5 * k)
        local textWidth = math.ceil(label:GetStringWidth())
        medal:Resize(side)
        glow:SetSize(side * 2, side * 2)
        hover:SetSize(side * 2, side * 2)
        LA.SetShown(medal, look ~= "name")
        LA.SetShown(hover, look ~= "name")
        LA.SetShown(plate, look ~= "medal")
        medal:ClearAllPoints()
        medal:SetPoint("LEFT")
        plate:ClearAllPoints()
        label:ClearAllPoints()
        local height = look == "name" and plateHeight or math.max(side, plateHeight)
        if look == "both" then
            plate:SetPoint("LEFT", Draw.Round(side / 2), 0)
            plate:SetPoint("RIGHT")
            label:SetPoint("LEFT", Draw.Round(side / 2 + 5 * k), 0)
            self:SetWidth(Draw.Round(side + textWidth + 14 * k))
        elseif look == "name" then
            plate:SetPoint("LEFT")
            plate:SetPoint("RIGHT")
            label:SetPoint("LEFT", Draw.Round(8 * k), 0)
            self:SetWidth(Draw.Round(textWidth + 16 * k))
        else
            self:SetWidth(side)
        end
        plate:SetHeight(plateHeight)
        self:SetHeight(height)
    end
    return button
end

local button = MakeButton(frame)
button:SetPoint("TOPLEFT")

local function SmallButton(text)
    local small = UI.Button(frame, text, nil, "plain")
    small:SetHeight(16)
    small.livingAzerothPip = true
    return small
end
local selectionButton = SmallButton("")
selectionButton:SetPoint("LEFT", button, "RIGHT", 4, 0)
local clearButton = SmallButton("Clear")
clearButton:SetPoint("LEFT", selectionButton, "RIGHT", 4, 0)

-- Every bot of the player's in the raid or the party that takes orders.
function AllBots.Guids()
    local guids = {}
    local raid = GetNumRaidMembers()
    for i = 1, raid > 0 and raid or GetNumPartyMembers() do
        local guid = LA.Bots.Guid(raid > 0 and "raid" .. i or "party" .. i)
        local bot = guid and LA.Bots.Mine()[guid]
        if bot and bot.commandable then
            guids[#guids + 1] = guid
        end
    end
    return guids
end

local function Key()
    return GetNumRaidMembers() > 0 and "all" or "party"
end

button:SetScript("OnClick", function(self)
    GameTooltip:Hide()
    local guids = AllBots.Guids()
    if #guids > 0 then
        LA.Ring.Toggle({ key = Key(), label = "All my bots", guids = guids }, self)
    end
end)
UI.Tooltip(button, "All my bots", "Orders for every bot of yours in the group.",
    "Move it with Move frames in the settings.")

selectionButton:SetScript("OnClick", function(self)
    local guids = LA.Raid.Selection()
    LA.Ring.Toggle({ key = "selection:" .. table.concat(guids, ","), label = #guids .. " selected", guids = guids },
        self)
end)
clearButton:SetScript("OnClick", function()
    LA.Raid.ClearSelection()
end)

local mover

-- Shown with two or more of the player's bots in a party, or any in a raid; the player can turn it off.
function AllBots.Update()
    if not LivingAzerothDB or not Settings() then
        return -- not loaded yet
    end
    local count = #AllBots.Guids()
    local raid = GetNumRaidMembers() > 0
    local wanted = Settings().shown ~= false and (raid and count > 0 or count >= 2)
    LA.SetShown(frame, wanted or (LA.unlocked and Settings().shown ~= false))
    LA.SetShown(button.glow, LA.Ring.OpenFor() == Key())
    local selected = raid and #LA.Raid.Selection() or 0
    LA.SetShown(selectionButton, selected > 0)
    LA.SetShown(clearButton, selected > 0)
    selectionButton:SetText("Orders for " .. selected .. " selected")
    button:Arrange()
    local width = button:GetWidth()
    if selected > 0 then
        width = width + 8 + selectionButton:GetWidth() + clearButton:GetWidth()
    end
    frame:SetSize(width, button:GetHeight())
    if mover then
        mover:SetArea(button:GetWidth(), button:GetHeight())
    end
end

-- The button as it looks with the player's settings, for the settings window.
function AllBots.Preview(parent)
    local preview = MakeButton(parent)
    preview:EnableMouse(false)
    return preview
end

function AllBots.Unlock(unlocked)
    if unlocked and InCombatLockdown() then
        return
    end
    AllBots.Update()
    LA.SetShown(mover, unlocked and frame:IsShown())
end

local function Place()
    local saved = Settings()
    frame:ClearAllPoints()
    if saved.x then
        frame:SetPoint("TOPLEFT", UIParent, "BOTTOMLEFT", saved.x, saved.y)
    else
        -- At first, just above where the party frames start.
        frame:SetPoint("TOPLEFT", UIParent, "TOPLEFT", 16, -150)
    end
    Draw.Settle(frame)
end

LA.On("loaded", function()
    local saved = LivingAzerothDB.allBots or {}
    saved.size = math.max(AllBots.SIZE[1], math.min(AllBots.SIZE[2], tonumber(saved.size) or AllBots.SIZE[3]))
    saved.look = AllBots.LOOKS[saved.look] and saved.look or "both"
    LivingAzerothDB.allBots = saved
    Place()
    button:Arrange()
    mover = UI.Mover(frame, "All my bots", button:GetWidth(), button:GetHeight(), function()
        Draw.Settle(frame)
        Settings().x, Settings().y = frame:GetLeft(), frame:GetTop()
    end)
    AllBots.Update()
end)

LA.On("bots:changed", AllBots.Update)
LA.On("ring:changed", AllBots.Update)
LA.On("raid:selection", AllBots.Update)

local events = CreateFrame("Frame")
events:RegisterEvent("PARTY_MEMBERS_CHANGED")
events:RegisterEvent("RAID_ROSTER_UPDATE")
events:RegisterEvent("PLAYER_ENTERING_WORLD")
events:SetScript("OnEvent", AllBots.Update)
