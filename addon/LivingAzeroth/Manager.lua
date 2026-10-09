-- SPDX-License-Identifier: GPL-2.0-or-later
-- The manager window: the Quest Log's two-pane frame, with tabs along the bottom as in the game's own windows.
-- Each tab is a page another file adds (roster, tactics, dialogue, guild). A page has a strip under the title
-- (page.top), the left pane for a list (page.left), a parchment page on the right (page.detail, which scrolls),
-- buttons under the list (page.controls) and under the parchment (page.actions).
local _, LA = ...

local Manager = {}
LA.Manager = Manager

local window = LA.Window.DualPane("LivingAzerothManager", LA.name, "Interface\\Icons\\INV_Misc_Gear_01")

local pages = {}
local tabs = {}

local close = LA.UI.Button(window.actions, CLOSE, 80)
close:SetPoint("RIGHT", 0, 1)
close:SetScript("OnClick", function() window:Hide() end)

-- Adds a tab. build(page) makes its contents once; show(page) refreshes it each time it is shown.
function Manager.AddPage(name, build, show)
    local index = #pages + 1
    local page = { tabName = name, build = build, show = show }
    for part, area in pairs({ top = "top", left = "list", controls = "controls", actions = "actions" }) do
        page[part] = CreateFrame("Frame", nil, window[area])
        page[part]:SetAllPoints()
        page[part]:Hide()
    end
    page.detail = CreateFrame("Frame", nil, window.detail)
    page.detail:SetSize(298, 333)
    page.detail:Hide()
    -- The parchment page's height, once its contents are laid out, so it scrolls when it is long.
    function page:SetDetailHeight(height)
        self.detail:SetHeight(math.max(height, 333))
        window.detail:UpdateScrollChildRect()
    end
    pages[index] = page

    local tab = CreateFrame("Button", "LivingAzerothManagerTab" .. index, window, "CharacterFrameTabButtonTemplate")
    tab:SetID(index)
    tab:SetText(name)
    PanelTemplates_TabResize(tab, 0)
    if index == 1 then
        tab:SetPoint("TOPLEFT", window, "BOTTOMLEFT", 11, 2)
    else
        tab:SetPoint("LEFT", tabs[index - 1], "RIGHT", -15, 0)
    end
    tab:SetScript("OnClick", function(self)
        PlaySound("igCharacterInfoTab")
        Manager.Show(self:GetID())
    end)
    tabs[index] = tab
    PanelTemplates_SetNumTabs(window, #pages)
    return page
end

function Manager.Show(index)
    index = index or LivingAzerothDB.managerTab or 1
    if not pages[index] then
        index = 1
    end
    LivingAzerothDB.managerTab = index
    PanelTemplates_SetTab(window, index)
    window:Show() -- before the page refreshes, which only work on a visible page
    for i, page in ipairs(pages) do
        local shown = i == index
        if shown and not page.built then
            page.built = true
            page.build(page)
        end
        for _, part in ipairs({ "top", "left", "controls", "actions", "detail" }) do
            LA.SetShown(page[part], shown)
        end
        if shown then
            window.title:SetText(LA.name .. " - " .. page.tabName)
            window.detail:SetScrollChild(page.detail)
            window.detail:SetVerticalScroll(0)
            if page.show then
                page.show(page)
            end
        end
    end
end

function Manager.Toggle()
    if window:IsShown() then
        window:Hide()
    else
        PlaySound("igCharacterInfoOpen")
        Manager.Show()
    end
end

function Manager.IsShown(page)
    return window:IsShown() and page.left:IsShown()
end

-- Back to the top of the parchment, when what it shows changes.
function Manager.ScrollToTop()
    window.detail:SetVerticalScroll(0)
end

window:SetScript("OnHide", function()
    PlaySound("igCharacterInfoClose")
end)
