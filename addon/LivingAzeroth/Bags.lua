-- SPDX-License-Identifier: GPL-2.0-or-later
-- A bot's bags, drawn the way Bagnon draws the player's own: one window with every bag's items in a single grid,
-- quality borders, and the gold at its foot. It opens beside the trade window when the player trades with a bot
-- (and with /la bags for a targeted bot). Clicking an item, or dragging it onto the trade window, has the bot put
-- it in the trade; clicking one of the bot's items in the trade window takes it back out; the gold field sets the
-- bot's gold. What a bot hands over is the server's decision, marked on each item: free (it got it while grouped
-- with the player), for sale at a price (a random bot's own), or not to be had.
local _, LA = ...

local Bags = {}
LA.Bags = Bags

local ITEM, SPACING, COLUMNS = 37, 2, 8
local PAD = 8
local TOP = 52 -- title, note and search above the grid
local MONEY_ROW = 24 -- the bot's gold, under the grid
local GIVE_ROW = 40 -- while trading, above that: "Gold it gives" and its field
local MAX_SLOTS = 16 + 4 * 36

local frame = CreateFrame("Frame", "LivingAzerothBotBags", UIParent)
frame:Hide()
frame:SetFrameStrata("HIGH")
frame:SetClampedToScreen(true)
frame:SetMovable(true)
frame:EnableMouse(true)
frame:RegisterForDrag("LeftButton")
frame:SetBackdrop({
    bgFile = [[Interface\ChatFrame\ChatFrameBackground]],
    edgeFile = [[Interface\Tooltips\UI-Tooltip-Border]],
    edgeSize = 16,
    tile = true,
    tileSize = 16,
    insets = { left = 4, right = 4, top = 4, bottom = 4 },
})
frame:SetBackdropColor(0, 0, 0, 0.5)
frame:SetBackdropBorderColor(1, 1, 1, 1)
frame:SetScript("OnDragStart", frame.StartMoving)
frame:SetScript("OnDragStop", frame.StopMovingOrSizing)
table.insert(UISpecialFrames, "LivingAzerothBotBags") -- Escape closes it

local title = frame:CreateFontString(nil, "OVERLAY", "GameFontNormalLeft")
title:SetPoint("TOPLEFT", PAD + 2, -PAD - 2)
local close = CreateFrame("Button", nil, frame, "UIPanelCloseButton")
close:SetPoint("TOPRIGHT", -2, -2)
local note = frame:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmallLeft")
note:SetPoint("TOPLEFT", title, "BOTTOMLEFT", 0, -4)
note:SetTextColor(0.75, 0.75, 0.75)

local search = CreateFrame("EditBox", "LivingAzerothBotBagsSearch", frame)
search:SetAutoFocus(false)
search:SetFontObject("ChatFontSmall")
search:SetTextInsets(8, 8, 0, 0)
search:SetHeight(20)
search:SetPoint("TOPRIGHT", -30, -PAD)
-- Bagnon's search border, at an edge size the box's height has room for (a 16 edge needs a box 32 tall).
search:SetBackdrop({
    edgeFile = [[Interface\Tooltips\UI-Tooltip-Border]],
    bgFile = [[Interface\ChatFrame\ChatFrameBackground]],
    edgeSize = 10,
    tile = true,
    tileSize = 16,
    insets = { left = 3, right = 3, top = 3, bottom = 3 },
})
search:SetBackdropColor(0, 0, 0, 0.8)
search:SetBackdropBorderColor(1, 1, 1, 0.8)
search:SetScript("OnEscapePressed", search.ClearFocus)
search:SetScript("OnEnterPressed", search.ClearFocus)
local searchHint = search:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
searchHint:SetPoint("LEFT", 8, 0)
searchHint:SetText(SEARCH)

-- The gold row: the bot's gold on the right, what it gives on the left.
local money = CreateFrame("Frame", "LivingAzerothBotBagsMoney", frame, "SmallMoneyFrameTemplate")
money:SetPoint("BOTTOMRIGHT", -PAD + 6, PAD + 6)
MoneyFrame_SetType(money, "STATIC")
local carries = frame:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmallLeft")
carries:SetPoint("BOTTOMLEFT", PAD + 2, PAD + 8)
carries:SetTextColor(0.75, 0.75, 0.75)
carries:SetText("It carries")
local give = CreateFrame("Frame", "LivingAzerothBotBagsGive", frame, "MoneyInputFrameTemplate")
give:SetPoint("BOTTOMLEFT", PAD + 6, PAD + MONEY_ROW + 4)
local giveButton = CreateFrame("Button", nil, frame, "UIPanelButtonTemplate")
giveButton:SetSize(44, 20)
giveButton:SetPoint("LEFT", give, "RIGHT", 2, 0)
giveButton:SetText("Give")
local giveLabel = frame:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmallLeft")
-- While trading with a random bot that wants paying: its price for what is in the trade, and "Pay", which puts that
-- gold on the player's side.
local ASK_ROW = 24
local asks = frame:CreateFontString(nil, "OVERLAY", "GameFontNormalSmallLeft")
local payButton = CreateFrame("Button", nil, frame, "UIPanelButtonTemplate")
payButton:SetSize(44, 20)
payButton:SetText("Pay")

local buttons = {}
local view -- the server's last answer for the bot shown
local botGuid, botName
local trading = false
local fromTrade = false -- opened by a trade, so it closes with it

local function Busy(text)
    UIErrorsFrame:AddMessage(text, 1, 0.25, 0.2)
end

local REASONS = {
    no_trade = "Open a trade with the bot first.",
    not_yours = "That isn't yours to take.",
    not_for_sale = "That isn't for sale.",
    bound = "That can't be traded.",
    in_trade = "That is already in the trade.",
    trade_full = "The trade has no room for more.",
    no_room = "The bot has no room to split that stack.",
    too_much = "That is more gold than it will give you.",
    disabled = "Trading with bots is off on this server.",
    unavailable = "This server doesn't show bots' bags.",
}

local function Refused(reply)
    Busy(REASONS[reply.reason or reply.error] or ("Refused: " .. tostring(reply.reason or reply.error)))
end

-- A stack's item string, with its random property, so the link names "of the Bear".
local function ItemString(stack)
    return "item:" .. stack.entry .. ":0:0:0:0:0:" .. (stack.property or 0)
end

-- What the player may do with a stack: "free", "price", "all" or "no".
local function Access(stack)
    if not stack.tradable then
        return "no"
    elseif view.full then
        return "all"
    elseif (view.shared[tostring(stack.entry)] or 0) > 0 then
        return "free"
    elseif view.sells and stack.price > 0 then
        return "price"
    end
    return "no"
end

local function Offer(stack)
    if not trading then
        Busy("Open a trade with " .. (botName or "the bot") .. " to take things.")
        return
    end
    LA.Bridge.Request({ op = "offer", bot = botGuid, bag = stack.bag, slot = stack.slot }, function(reply)
        if not reply.ok then
            Refused(reply)
        end
        Bags.Refresh()
    end)
end

local function ShowTooltip(button)
    local stack = button.stack
    if not stack then
        return
    end
    GameTooltip:SetOwner(button, "ANCHOR_RIGHT")
    GameTooltip:SetHyperlink(ItemString(stack))
    local access = Access(stack)
    local shared = view.shared[tostring(stack.entry)] or 0
    GameTooltip:AddLine(" ")
    if stack.inTrade then
        GameTooltip:AddLine("In the trade.", 0.6, 0.8, 1)
    elseif access == "all" then
        GameTooltip:AddLine("Yours to take.", 0.4, 1, 0.4)
    elseif access == "free" then
        local text = shared < stack.count and (shared .. " of these are yours to take") or "Yours to take"
        GameTooltip:AddLine(text .. ": it got them while grouped with you.", 0.4, 1, 0.4, true)
    elseif access == "price" then
        GameTooltip:AddLine("For sale; it asks:", 1, 0.82, 0)
        SetTooltipMoney(GameTooltip, stack.price)
    elseif not stack.tradable then
        GameTooltip:AddLine("Can't be traded.", 0.6, 0.6, 0.6)
    else
        GameTooltip:AddLine("Not yours to take.", 0.6, 0.6, 0.6)
    end
    if access ~= "no" and not stack.inTrade then
        GameTooltip:AddLine(trading and "Click, or drag it onto the trade, to have it put in."
            or "Open a trade to take it.", 0.75, 0.75, 0.75, true)
    end
    GameTooltip:Show()
end

local function Button(index)
    local button = buttons[index]
    if button then
        return button
    end
    button = CreateFrame("Button", "LivingAzerothBotBagsItem" .. index, frame, "ItemButtonTemplate")
    button:SetSize(ITEM, ITEM)
    button:RegisterForClicks("AnyUp")
    button:RegisterForDrag("LeftButton")
    local border = button:CreateTexture(nil, "OVERLAY") -- the quality border, as Bagnon draws it
    border:SetSize(67, 67)
    border:SetPoint("CENTER")
    border:SetTexture([[Interface\Buttons\UI-ActionButton-Border]])
    border:SetBlendMode("ADD")
    button.border = border
    local mark = button:CreateTexture(nil, "OVERLAY")
    mark:SetSize(14, 14)
    mark:SetPoint("TOPRIGHT", -1, -1)
    button.mark = mark
    button:SetScript("OnEnter", ShowTooltip)
    button:SetScript("OnLeave", function() GameTooltip:Hide() end)
    button:SetScript("OnClick", function(self)
        if self.stack and (IsShiftKeyDown() or IsControlKeyDown()) then
            HandleModifiedItemClick(select(2, GetItemInfo(ItemString(self.stack))))
        elseif self.stack and not self.stack.inTrade then
            Offer(self.stack)
        end
    end)
    -- Dropped on the trade window: the same as a click.
    button:SetScript("OnDragStop", function(self)
        if self.stack and not self.stack.inTrade and TradeFrame:IsShown() and TradeFrame:IsMouseOver() then
            Offer(self.stack)
        end
    end)
    buttons[index] = button
    return button
end

-- Items the client hasn't seen yet arrive a moment after they are asked for; the grid is drawn again then.
local missing = 0
local retry = CreateFrame("Frame")
retry:Hide()
retry:SetScript("OnUpdate", function(self, delta)
    self.wait = (self.wait or 0) + delta
    if self.wait > 0.5 then
        self.wait = 0
        self:Hide()
        Bags.Draw()
    end
end)

local function Matches(stack, text)
    if text == "" then
        return true
    end
    local name = GetItemInfo(ItemString(stack))
    return name and name:lower():find(text, 1, true) ~= nil
end

local function Order(a, b)
    local bagA, bagB = a.bag == 255 and 0 or a.bag, b.bag == 255 and 0 or b.bag
    if bagA ~= bagB then
        return bagA < bagB
    end
    return a.slot < b.slot
end

-- Draws the window from the last answer.
function Bags.Draw()
    if not view then
        return
    end
    title:SetText((botName or "Bot") .. "'s bags")
    if view.full then
        note:SetText("Yours: everything can be taken.")
    elseif view.sells then
        note:SetText("Ticked: got while grouped with you, free. The rest is for sale.")
    else
        note:SetText("Only what it got while grouped with you can be taken.")
    end
    local text = (search:GetText() or ""):lower()
    LA.SetShown(searchHint, text == "" and not search:HasFocus())
    table.sort(view.stacks, Order)
    missing = 0
    local shown = 0
    for _, stack in ipairs(view.stacks) do
        if Matches(stack, text) or (text ~= "" and not GetItemInfo(ItemString(stack))) then
            shown = shown + 1
            local button = Button(shown)
            button.stack = stack
            local _, _, quality, _, _, _, _, _, _, texture = GetItemInfo(ItemString(stack))
            if not texture then
                missing = missing + 1
                GameTooltip:SetOwner(UIParent, "ANCHOR_NONE") -- asks the server about the item
                GameTooltip:SetHyperlink(ItemString(stack))
                GameTooltip:Hide()
            end
            SetItemButtonTexture(button, texture or [[Interface\Icons\INV_Misc_QuestionMark]])
            SetItemButtonCount(button, stack.count)
            local access = Access(stack)
            SetItemButtonDesaturated(button, access == "no" or stack.inTrade)
            button:SetAlpha((access == "no" or stack.inTrade) and 0.5 or 1)
            if quality and quality > 1 then
                local r, g, b = GetItemQualityColor(quality)
                button.border:SetVertexColor(r, g, b, 0.5)
                button.border:Show()
            else
                button.border:Hide()
            end
            if access == "free" then
                button.mark:SetTexture([[Interface\RaidFrame\ReadyCheck-Ready]])
                button.mark:Show()
            elseif access == "price" then
                button.mark:SetTexture([[Interface\MoneyFrame\UI-GoldIcon]])
                button.mark:Show()
            else
                button.mark:Hide()
            end
            button:ClearAllPoints()
            local column, row = (shown - 1) % COLUMNS, math.floor((shown - 1) / COLUMNS)
            button:SetPoint("TOPLEFT", PAD + column * (ITEM + SPACING), -TOP - row * (ITEM + SPACING))
            button:Show()
            if GameTooltip:IsOwned(button) then
                ShowTooltip(button)
            end
        end
    end
    for i = shown + 1, #buttons do
        buttons[i]:Hide()
        buttons[i].stack = nil
    end
    local rows = math.max(1, math.ceil(shown / COLUMNS))
    -- While trading: the gold field, or a line saying there is no gold to be had; above it, the bot's price.
    local canGive = view.full or view.allowance > 0
    local giveRow = canGive and GIVE_ROW or 18
    local asking = trading and view.asking > 0
    local foot = MONEY_ROW + (trading and giveRow or 0) + (asking and ASK_ROW or 0)
    frame:SetSize(PAD * 2 + COLUMNS * (ITEM + SPACING) - SPACING, TOP + rows * (ITEM + SPACING) - SPACING + foot + PAD)
    search:SetWidth(math.max(80, frame:GetWidth() - title:GetStringWidth() - 60))
    MoneyFrame_Update(money:GetName(), view.money)
    LA.SetShown(asks, asking)
    LA.SetShown(payButton, asking)
    if asking then
        asks:ClearAllPoints()
        asks:SetPoint("BOTTOMLEFT", PAD + 2, PAD + MONEY_ROW + giveRow + 8)
        payButton:ClearAllPoints()
        payButton:SetPoint("BOTTOMRIGHT", -PAD - 2, PAD + MONEY_ROW + giveRow + 3)
        local enough = GetMoney() - GetPlayerTradeMoney() >= view.asking
        asks:SetText("It asks " .. GetCoinTextureString(view.asking) .. " for what is in the trade."
            .. (enough and "" or " |cffff6040You haven't that much.|r"))
        if enough then
            payButton:Enable()
        else
            payButton:Disable()
        end
    end
    LA.SetShown(give, trading and canGive)
    LA.SetShown(giveButton, trading and canGive)
    LA.SetShown(giveLabel, trading)
    giveLabel:ClearAllPoints()
    if canGive then
        giveLabel:SetPoint("BOTTOMLEFT", give, "TOPLEFT", -4, 3)
        giveLabel:SetText(view.full and "Gold it gives"
            or ("Gold it gives (up to " .. GetCoinTextureString(view.allowance) .. ")"))
    else
        giveLabel:SetPoint("BOTTOMLEFT", PAD + 2, PAD + MONEY_ROW + 2)
        giveLabel:SetText("No gold to give: it gained none while grouped with you.")
    end
    if missing > 0 then
        retry:Show()
    end
end

-- Asks the server for the bot's bags again. One question is out at a time; asking again meanwhile asks once more
-- when its answer comes, so the window ends up showing the latest state.
local asking, again
function Bags.Refresh()
    if not botGuid then
        return
    end
    if asking then
        again = true
        return
    end
    asking, again = true, false
    LA.Bridge.Request({ op = "bags", bot = botGuid }, function(reply)
        asking = false
        if again then
            Bags.Refresh()
            return
        end
        if not reply.ok then
            if reply.error ~= "unknown_bot" then
                Refused(reply)
            end
            frame:Hide()
            return
        end
        -- Stacks come as [bag, slot, entry, count, property, price, flags]: flags 1 tradable, 2 in the trade.
        local stacks = {}
        for _, row in ipairs(reply.stacks or {}) do
            local flags = row[7] or 0
            stacks[#stacks + 1] = { bag = row[1], slot = row[2], entry = row[3], count = row[4], property = row[5],
                price = row[6], tradable = flags % 2 == 1, inTrade = flags % 4 >= 2 }
        end
        view = { stacks = stacks, shared = reply.shared or {}, money = reply.money or 0,
            allowance = reply.allowance or 0, full = reply.full, sells = reply.sells, asking = reply.asking or 0 }
        botName = reply.name or botName
        trading = reply.trading and TradeFrame:IsShown()
        Bags.Draw()
        frame:Show()
    end)
end

-- Opens the window for a bot (its GUID number); quiet says nothing when the server doesn't show bots' bags.
function Bags.Open(guid, name, quiet)
    if not LA.Bridge.trade or not LA.Bridge.trade.shares then
        if not quiet then
            Busy(REASONS.unavailable)
        end
        return
    end
    if botGuid ~= guid then
        view = nil
    end
    botGuid, botName = guid, name
    Bags.Refresh()
end

local function Place()
    frame:ClearAllPoints()
    if TradeFrame:IsShown() then
        frame:SetPoint("TOPLEFT", TradeFrame, "TOPRIGHT", -30, -12)
    else
        frame:SetPoint("CENTER", UIParent, "CENTER", 200, 0)
    end
end

frame:SetScript("OnHide", function()
    search:ClearFocus()
    botGuid, view, trading, fromTrade = nil, nil, false, false
end)

search:SetScript("OnTextChanged", Bags.Draw)
search:SetScript("OnEditFocusGained", function() searchHint:Hide() end)
search:SetScript("OnEditFocusLost", Bags.Draw)

local function Give()
    if not botGuid then
        return
    end
    local copper = MoneyInputFrame_GetCopper(give)
    LA.Bridge.Request({ op = "offer_gold", bot = botGuid, copper = copper }, function(reply)
        if not reply.ok then
            Refused(reply)
        end
        Bags.Refresh()
    end)
end
giveButton:SetScript("OnClick", Give)
-- Paying adds the bot's price to the gold already on the player's side, shown in the trade window's own gold field
-- too; the trade then refreshes the window.
payButton:SetScript("OnClick", function()
    if trading and view and view.asking > 0 then
        local total = GetPlayerTradeMoney() + view.asking
        MoneyInputFrame_SetCopper(TradePlayerInputMoneyFrame, total)
        SetTradeMoney(total)
        Bags.Refresh()
    end
end)
for _, part in ipairs({ "Gold", "Silver", "Copper" }) do
    _G[give:GetName() .. part]:HookScript("OnEnterPressed", Give)
end

-- The bot's items in the trade window: clicking one takes it back out.
for i = 1, MAX_TRADABLE_ITEMS do
    local slot = _G["TradeRecipientItem" .. i .. "ItemButton"]
    if slot then
        slot:HookScript("OnClick", function()
            if trading and botGuid and GetTradeTargetItemInfo(i) then
                LA.Bridge.Request({ op = "take_back", bot = botGuid, slot = i - 1 }, function(reply)
                    if not reply.ok then
                        Refused(reply)
                    end
                    Bags.Refresh()
                end)
            end
        end)
    end
end

-- With a trade open with a bot, the bags open beside the trade window and follow what goes in and out.
local events = CreateFrame("Frame")
events:RegisterEvent("TRADE_SHOW")
events:RegisterEvent("TRADE_CLOSED")
events:RegisterEvent("TRADE_TARGET_ITEM_CHANGED")
events:RegisterEvent("TRADE_PLAYER_ITEM_CHANGED") -- what the player puts in changes the bot's price
events:RegisterEvent("TRADE_MONEY_CHANGED")
events:RegisterEvent("TRADE_ACCEPT_UPDATE")
events:SetScript("OnEvent", function(_, event)
    if event == "TRADE_SHOW" then
        -- A bot the addon hasn't heard of yet (one met in the world) is asked about now; the server answers for
        -- bots only, and the window stays shut for players.
        local guid = LA.Bots.Guid("NPC")
        if guid and LA.Bots.Get(guid) ~= false and LivingAzerothDB.tradeBags ~= false then
            Bags.Open(guid, UnitName("NPC"), true)
            fromTrade = true
            Place()
            MoneyInputFrame_ResetMoney(give)
        end
    elseif event == "TRADE_CLOSED" then
        if fromTrade then
            frame:Hide()
        else
            trading = false
            if view then
                Bags.Draw()
            end
        end
    elseif frame:IsShown() and trading then
        Bags.Refresh()
    end
end)

-- /la bags: the window for the target, if it is a bot in the player's group.
function Bags.OpenTarget()
    local guid = LA.Bots.Guid("target")
    if not guid or not LA.Bots.Get(guid) then
        Busy("Target one of the bots in your group first.")
        return
    end
    Bags.Open(guid, UnitName("target"))
    Place()
end
