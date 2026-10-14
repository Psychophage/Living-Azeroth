-- SPDX-License-Identifier: GPL-2.0-or-later
-- The manager window, with tabs along the bottom as in the game's own windows. Each tab is a page another file
-- adds (roster, tactics, dialogue, guild); a page lays out its own contents in page.frame.
local _, LA = ...

local Manager = {}
LA.Manager = Manager

local UI, Style = LA.UI, LA.Style

Manager.WIDTH, Manager.HEIGHT = 620, 420
local TAB_HEIGHT, TUCK = 22, 4 -- tabs hang under the window, their top tucked behind its frame
local window
local pages = {}
local tabs = {}

local function Tab(index, page)
    local tab = CreateFrame("Button", nil, window)
    tab:SetFrameLevel(math.max(0, window:GetFrameLevel() - 1))
    UI.Surface(tab, "tab")
    UI.HoverLight(tab)
    tab.icon = UI.Icon(tab, 11, "plain")
    tab.text = UI.Text(tab, "tab")
    tab.text:SetText(page.tabName)
    tab.icon:SetPoint("LEFT", 10, -TUCK / 2)
    tab.text:SetPoint("LEFT", tab.icon, "RIGHT", 5, 0)
    tab:SetSize(math.ceil(tab.text:GetStringWidth()) + 36, TAB_HEIGHT + TUCK)
    if index == 1 then
        tab:SetPoint("TOPLEFT", window, "BOTTOMLEFT", 16, TUCK)
    else
        tab:SetPoint("TOPLEFT", tabs[index - 1], "TOPRIGHT", 2, 0)
    end
    tab:SetScript("OnClick", function()
        PlaySound("igCharacterInfoTab")
        Manager.Show(index)
    end)
    function tab:SetChosen(chosen)
        self.surface:Set(chosen and "tabOn" or "tab")
        local colour = chosen and Style.gold or Style.T().muted
        self.text:SetTextColor(colour[1], colour[2], colour[3])
        self.icon:Set(page.glyph, colour)
        self:SetHeight(TAB_HEIGHT + TUCK + (chosen and 3 or 0))
    end
    return tab
end

local function Window()
    if window then
        return window
    end
    window = LA.Window.Panel("LivingAzerothManager", Manager.WIDTH, Manager.HEIGHT, LA.name)
    for index, page in ipairs(pages) do
        page.frame = CreateFrame("Frame", nil, window.body)
        page.frame:SetAllPoints()
        page.frame:Hide()
        tabs[index] = Tab(index, page)
    end
    return window
end

-- Adds a tab. build(page) makes its contents once; show(page) refreshes it each time it is shown.
function Manager.AddPage(name, glyph, build, show)
    local page = { tabName = name, glyph = glyph, build = build, show = show }
    pages[#pages + 1] = page
    return page
end

function Manager.Show(index)
    Window()
    index = index or LivingAzerothDB.managerTab or 1
    if not pages[index] then
        index = 1
    end
    LivingAzerothDB.managerTab = index
    window:Show() -- before the page refreshes, which only works on a visible page
    for i, page in ipairs(pages) do
        local shown = i == index
        tabs[i]:SetChosen(shown)
        if shown and not page.built then
            page.built = true
            page.build(page)
        end
        LA.SetShown(page.frame, shown)
        if shown then
            window.title:SetText(LA.name .. " · " .. page.tabName)
            if page.show then
                page.show(page)
            end
        end
    end
end

function Manager.Toggle()
    if window and window:IsShown() then
        window:Hide()
    else
        Manager.Show()
    end
end

function Manager.IsShown(page)
    return window ~= nil and window:IsShown() and page.frame ~= nil and page.frame:IsShown()
end

-- The page's area, width x height.
function Manager.BodySize()
    return LA.Window.BodySize(Manager.WIDTH, Manager.HEIGHT)
end
