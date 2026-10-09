-- SPDX-License-Identifier: GPL-2.0-or-later
-- The manager window, in the style of the Dungeon Finder, with tabs along the bottom as in the game's own
-- windows. Each tab is a page another file adds (roster, tactics, dialogue, guild); a page draws into the
-- window's header band, its metal bar, the paper and the bottom bar.
local _, LA = ...

local Manager = {}
LA.Manager = Manager

local WIDTH, HEIGHT = 700, 560

local window = LA.Window.Create("LivingAzerothManager", WIDTH, HEIGHT, LA.name, "Interface\\Icons\\INV_Misc_Gear_01")

local pages = {}
local tabs = {}

-- Adds a tab. build(page) makes its contents once, into page.header, page.bar, page.content and page.footer;
-- show(page) refreshes it each time it is shown.
function Manager.AddPage(name, build, show)
    local index = #pages + 1
    local page = { tabName = name, build = build, show = show }
    for _, part in ipairs({ "header", "bar", "content", "footer" }) do
        page[part] = CreateFrame("Frame", nil, window[part])
        page[part]:SetAllPoints()
        page[part]:Hide()
    end
    pages[index] = page

    local tab = CreateFrame("Button", "LivingAzerothManagerTab" .. index, window, "CharacterFrameTabButtonTemplate")
    tab:SetID(index)
    tab:SetText(name)
    PanelTemplates_TabResize(tab, 0)
    if index == 1 then
        tab:SetPoint("TOPLEFT", window, "BOTTOMLEFT", 12, 4)
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
        for _, part in ipairs({ "header", "bar", "content", "footer" }) do
            LA.SetShown(page[part], shown)
        end
        if shown then
            window.title:SetText(LA.name .. " - " .. page.tabName)
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
    return window:IsShown() and page.content:IsShown()
end

window:SetScript("OnHide", function()
    PlaySound("igCharacterInfoClose")
end)

-- A Close button at the right of a page's bottom bar, as the Dungeon Finder has.
function Manager.CloseButton(page)
    local close = LA.UI.Button(page.footer, CLOSE, 100)
    close:SetPoint("RIGHT", 0, 0)
    close:SetScript("OnClick", function()
        window:Hide()
    end)
    return close
end
