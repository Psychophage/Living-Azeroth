-- SPDX-License-Identifier: GPL-2.0-or-later
-- The orders a player can give a bot, how each looks, and sending them: a bot's state changes only once the
-- server says the bot has acted.
local _, LA = ...

local Orders = {}
LA.Orders = Orders

-- Every order the ring can hold. Switches flip; the others are given as they are. "release" is not chosen in the
-- settings: the ring shows it by itself while one of its bots lies dead.
Orders.catalog = {
    { id = "follow", short = "Follow", label = "Follow me", state = "Following you" },
    { id = "stay", short = "Stay", label = "Stay here", state = "Holding position" },
    { id = "guard", short = "Guard", label = "Guard this spot", state = "Guarding a spot" },
    { id = "attack", short = "Attack", label = "Attack my target" },
    { id = "pull", short = "Pull", label = "Pull my target" },
    { id = "flee", short = "Fall back", label = "Fall back to me" },
    { id = "rest", short = "Eat, drink", label = "Eat and drink" },
    { id = "passive", short = "Passive", label = "Stay out of fights", switch = true },
    { id = "loot", short = "Loot", label = "Pick up loot", switch = true },
    { id = "join", short = "Join in", label = "Join in when I attack", switch = true },
    { id = "release", short = "Release", label = "Release spirit and come back", automatic = true },
}
Orders.byId = {}
for _, order in ipairs(Orders.catalog) do
    order.glyph, order.tone = unpack(LA.Style.ORDER_LOOK[order.id])
    Orders.byId[order.id] = order
end
Orders.free = { glyph = "bot", tone = "stone", state = "Doing as it likes" }

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
    if id == "release" then
        return bot.life == "dead"
    end
    return not order.switch or (bot.switches and bot.switches[id] ~= nil)
end

-- An order still waiting for this bot (any one), or nil.
-- Whether a bot's order icon shows on the raid frames: always, or (if the player chose) only when it is doing
-- something other than following, is waiting for an order, or has its ring open. Party frames always show it;
-- in a raid, a group's name or "All my bots" still reaches the bots without one.
function Orders.IconShown(bot)
    if LivingAzerothDB.iconsAlways ~= false then
        return true
    end
    return bot.order ~= "follow" or pending[bot.guid] ~= nil or LA.Ring.OpenFor() == "bot:" .. bot.guid
end

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
