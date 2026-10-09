-- SPDX-License-Identifier: GPL-2.0-or-later
-- A round button on the minimap's edge, like the game's own: click for the manager window, right-click for the
-- orders ring's settings, drag to move it around the minimap.
local _, LA = ...

local MinimapButton = {}
LA.MinimapButton = MinimapButton

local ICON = "Interface\\Icons\\INV_Misc_Gear_01"

local button = CreateFrame("Button", "LivingAzerothMinimapButton", Minimap)
button:SetSize(31, 31)
button:SetFrameStrata("MEDIUM")
button:SetFrameLevel(8)
button:SetHighlightTexture("Interface\\Minimap\\UI-Minimap-ZoomButton-Highlight")
button:RegisterForClicks("LeftButtonUp", "RightButtonUp")
button:RegisterForDrag("LeftButton")

local background = button:CreateTexture(nil, "BACKGROUND")
background:SetTexture("Interface\\Minimap\\UI-Minimap-Background")
background:SetSize(20, 20)
background:SetPoint("TOPLEFT", 7, -5)
local icon = button:CreateTexture(nil, "ARTWORK")
icon:SetTexture(ICON)
icon:SetSize(17, 17)
icon:SetPoint("TOPLEFT", 7, -6)
icon:SetTexCoord(0.08, 0.92, 0.08, 0.92)
local border = button:CreateTexture(nil, "OVERLAY")
border:SetTexture("Interface\\Minimap\\MiniMap-TrackingBorder")
border:SetSize(53, 53)
border:SetPoint("TOPLEFT")

local function Place()
    local angle = math.rad(LivingAzerothDB.minimapAngle or 200)
    local radius = Minimap:GetWidth() / 2 + 5
    button:ClearAllPoints()
    button:SetPoint("CENTER", Minimap, "CENTER", radius * math.cos(angle), radius * math.sin(angle))
end

local function FollowCursor()
    local x, y = GetCursorPosition()
    local scale = Minimap:GetEffectiveScale()
    local centerX, centerY = Minimap:GetCenter()
    LivingAzerothDB.minimapAngle = math.deg(math.atan2(y / scale - centerY, x / scale - centerX))
    Place()
end

button:SetScript("OnDragStart", function(self)
    self:LockHighlight()
    icon:SetTexCoord(0, 1, 0, 1)
    self:SetScript("OnUpdate", FollowCursor)
end)
button:SetScript("OnDragStop", function(self)
    self:UnlockHighlight()
    icon:SetTexCoord(0.08, 0.92, 0.08, 0.92)
    self:SetScript("OnUpdate", nil)
end)
button:SetScript("OnMouseDown", function()
    icon:SetPoint("TOPLEFT", 8, -7)
end)
button:SetScript("OnMouseUp", function()
    icon:SetPoint("TOPLEFT", 7, -6)
end)
button:SetScript("OnClick", function(_, mouse)
    GameTooltip:Hide()
    if mouse == "RightButton" then
        LA.RingSettings.Toggle()
    else
        LA.Manager.Toggle()
    end
end)
button:SetScript("OnEnter", function(self)
    GameTooltip:SetOwner(self, "ANCHOR_LEFT")
    GameTooltip:AddLine(LA.name)
    local states = { ready = "Connected", connecting = "Connecting...", missing = "No bot control on this server" }
    GameTooltip:AddLine(states[LA.Bridge.state] or "", 1, 1, 1)
    GameTooltip:AddLine("Click for the manager, right-click for the orders ring.", 0.6, 0.6, 0.6)
    GameTooltip:AddLine("Drag to move this button.", 0.6, 0.6, 0.6)
    GameTooltip:Show()
end)
button:SetScript("OnLeave", function()
    GameTooltip:Hide()
end)

LA.On("loaded", function()
    LA.SetShown(button, not LivingAzerothDB.hideMinimapButton)
    Place()
end)

function MinimapButton.SetShown(shown)
    LivingAzerothDB.hideMinimapButton = not shown
    LA.SetShown(button, shown)
end
