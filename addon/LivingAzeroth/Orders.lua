-- SPDX-License-Identifier: GPL-2.0-or-later
-- The orders a player can give a bot, how each looks, and sending them: a bot's state changes only once the
-- server says the bot has acted.
local _, LA = ...

local Orders = {}
LA.Orders = Orders

local ICONS = "Interface\\Icons\\"

-- Every order the ring can hold. Switches flip; the others are given as they are.
Orders.catalog = {
    { id = "follow", icon = "Ability_Tracking", label = "Follow me", state = "Following you" },
    { id = "stay", icon = "Spell_Nature_TimeStop", label = "Stay here", state = "Holding position" },
    { id = "guard", icon = "Ability_Defend", label = "Guard this spot", state = "Guarding a spot" },
    { id = "attack", icon = "Ability_SteelMelee", label = "Attack my target" },
    { id = "pull", icon = "INV_ThrowingAxe_01", label = "Pull my target" },
    { id = "flee", icon = "Ability_Rogue_Sprint", label = "Fall back to me" },
    { id = "rest", icon = "Spell_Misc_Drink", label = "Eat and drink" },
    { id = "passive", icon = "Spell_Nature_Sleep", label = "Stay out of fights", switch = true },
    { id = "loot", icon = "INV_Misc_Bag_10", label = "Pick up loot", switch = true },
    { id = "join", icon = "Ability_Warrior_BattleShout", label = "Join in when I attack", switch = true },
}
Orders.byId = {}
for _, order in ipairs(Orders.catalog) do
    order.icon = ICONS .. order.icon
    Orders.byId[order.id] = order
end
Orders.free = { icon = ICONS .. "Ability_Hunter_Pet_Wolf", state = "Doing as it likes" }

Orders.defaultRing = { "follow", "stay", "attack", "guard", "passive", "loot" }
Orders.MAX_RING = 8

local pending = {} -- guid -> { order id = true } being waited for
local done = {} -- guid -> time the last order was confirmed

-- What a bot is doing, for its order icon: the catalog entry of its standing order.
function Orders.Standing(bot)
    return Orders.byId[bot.order] or Orders.free
end

-- Whether an order is the bot's current one (a standing order) or switched on.
function Orders.IsOn(bot, id)
    local order = Orders.byId[id]
    if order.switch then
        return bot.switches and bot.switches[id] == true
    end
    return bot.order == id
end

-- Whether the bot can take this order at all (a switch its class lacks cannot).
function Orders.Offered(bot, id)
    local order = Orders.byId[id]
    return not order.switch or (bot.switches and bot.switches[id] ~= nil)
end

-- An order still waiting for this bot (any one), or nil.
function Orders.Pending(guid)
    return pending[guid] and next(pending[guid])
end

-- Whether this order is waiting for this bot.
function Orders.IsPending(guid, id)
    return pending[guid] ~= nil and pending[guid][id] == true
end

function Orders.RecentlyDone(guid)
    return done[guid] and GetTime() - done[guid] < 1.2
end

local REASONS = {
    timeout = "didn't manage it in time",
    not_yours = "isn't yours to command",
    unknown_bot = "isn't here any more",
    unsupported = "can't do that",
    no_answer = "the server didn't answer",
}

-- Gives an order; for a switch, on says which way (without it, the switch flips). extra adds request fields
-- (a formation's name).
function Orders.Give(bot, id, on, extra)
    local guid = bot.guid
    pending[guid] = pending[guid] or {}
    pending[guid][id] = true
    LA.Fire("orders:changed", guid)
    local request = { op = "order", bot = guid, order = id, on = on }
    for key, value in pairs(extra or {}) do
        request[key] = value
    end
    LA.Bridge.Request(request, function(reply)
        if pending[guid] then
            pending[guid][id] = nil
            if not next(pending[guid]) then
                pending[guid] = nil
            end
        end
        if reply.bot then
            LA.Bots.Update(reply.bot)
        end
        if reply.ok then
            done[guid] = GetTime()
        else
            local why = reply.reason or REASONS[reply.error] or reply.error or "couldn't"
            UIErrorsFrame:AddMessage(bot.name .. ": " .. why .. ".", 1, 0.25, 0.2)
        end
        LA.Fire("orders:changed", guid)
    end)
end
