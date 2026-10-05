/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */
// Living Azeroth population: deterministic policy shared by runtime and simulated-time tests.
#include "PopulationPolicy.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <tuple>
#include <unordered_set>

namespace RealmPopulation
{
uint64_t Policy::Pair(Guid human, Guid bot) { return (uint64_t(human) << 32) | bot; }

uint32_t Policy::Mix(uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7feb352dU;
    value ^= value >> 15;
    value *= 0x846ca68bU;
    return value ^ (value >> 16);
}

void Policy::Advance(WorldState& state, Settings const& settings, Seconds now, bool humansPresent)
{
    // No catch-up credit for server downtime or wall-clock corrections. The runtime persists
    // allowance, and sets lastTick to startup time when restoring this state.
    Seconds elapsed = now > state.lastTick && state.lastTick ? now - state.lastTick : 0;
    state.lastTick = now;
    if (humansPresent)
        state.offlineRemaining = std::min<Seconds>(
            settings.offlineAllowance, state.offlineRemaining + std::min<Seconds>(elapsed, settings.updateSeconds * 2));
    else
        state.offlineRemaining -= std::min(state.offlineRemaining, elapsed);
}

bool Policy::Observe(Familiarity& value, Settings const& settings, Seconds now, Seconds humanSession, bool meaningful)
{
    // A session cannot be manufactured by relogging; runtime supplies a persisted six-hour
    // encounter window. The independent interval also limits a sighting across its boundary.
    bool changed = false;
    if (humanSession != value.lastSession)
    {
        value.lastSession = humanSession;
        value.sessionSightings = 0;
    }
    if ((!value.lastSighting || now >= value.lastSighting + settings.sightingInterval) &&
        value.sessionSightings < settings.sightingSessionCap)
    {
        value.lastSighting = now;
        ++value.sightings;
        ++value.sessionSightings;
        value.score = std::min(100u, value.score + 1);
        changed = true;
    }
    if (meaningful && (!value.lastInteraction || now >= value.lastInteraction + settings.interactionInterval))
    {
        value.lastInteraction = now;
        ++value.interactions;
        value.score = std::min(100u, value.score + 4);
        changed = true;
    }
    // Persist coarse sightings, not an update per entity per tick.
    if (!value.lastSeen || now >= value.lastSeen + 60)
    {
        value.lastSeen = now;
        changed = true;
    }
    return changed;
}

float Policy::FamiliarityWeight(Familiarity const& value, Seconds now)
{
    Seconds age = now > value.lastSeen ? now - value.lastSeen : 0;
    float recency = 1.0f / (1.0f + float(age) / (7 * 86400));
    // Recency changes availability, never the stored score or historical identity.
    return float(value.score) * (0.5f + 0.5f * recency);
}

float Policy::ActivityFraction(Character const& bot, std::vector<Human> const& humans)
{
    uint32_t difference = 80;
    for (auto const& human : humans)
        if (human.faction == bot.faction)
            difference = std::min(difference, uint32_t(std::abs(int(bot.level) - int(human.level))));
    return std::max(0.15f, 1.0f / (1.0f + float(difference) / 10.0f));
}

float Policy::Catchup(Character const& bot, Human const& human, Familiarity const& value, Settings const& settings)
{
    if (human.faction != bot.faction || value.score < settings.familiarityThreshold || bot.level >= human.level)
        return 1.0f;
    return std::min(settings.catchupMaximum, 1.0f + float(human.level - bot.level) * 0.05f);
}

uint32_t Policy::CreationCount(uint32_t total, bool humansPresent, WorldState const& state, Settings const& settings,
                               Seconds now)
{
    uint32_t target =
        humansPresent ? settings.reserveTarget : std::min(settings.reserveTarget, settings.seedCharacters);
    if (total >= target || now < state.nextCreation)
        return 0;
    return std::min(settings.creationBatch, target - total);
}

namespace
{
// First player level of each expansion's phase; a hub opens once its phase is reached.
constexpr uint32_t EXPANSION_LEVEL_FLOOR[] = {1, 61, 71};
constexpr uint32_t PEER_SPREAD = 2;
}  // namespace

float Policy::AdvancePresence(float weight, bool present, Seconds elapsed, Settings const& settings)
{
    float step = float(elapsed) / float(std::max(1u, present ? settings.rampSeconds : settings.lingerSeconds));
    return std::clamp(present ? weight + step : weight - step, 0.0f, 1.0f);
}

Seconds Policy::SessionLength(Guid bot, Settings const& settings)
{
    return Seconds(settings.sessionSeconds) * (500 + Mix(bot) % 1001) / 1000;
}

bool Policy::CanBePresent(Character const& bot, Settings const& settings, Seconds now)
{
    if (bot.online)
        return bot.engaged || !bot.sessionStarted || now < bot.sessionStarted + SessionLength(bot.id, settings);
    return bot.restUntil <= now;
}

uint32_t Policy::Quota(Place const& place, Settings const& settings)
{
    return uint32_t(std::lround(float(settings.regionTarget) * place.weight));
}

std::vector<uint32_t> Policy::Peers(Place const& place, Settings const& settings)
{
    std::vector<uint32_t> peers;
    for (auto const& human : place.humans)
        if (human.level + settings.maximumLevelDifference >= place.minLevel &&
            human.level <= place.maxLevel + settings.maximumLevelDifference)
            peers.push_back(human.level);
    return peers;
}

bool Policy::PeerSeat(Place const& place, uint32_t seat, Settings const& settings)
{
    // A capital is shared by every level evenly; peers matter in the zones people level in.
    return !place.hub && !Peers(place, settings).empty() && (seat * 37) % 100 < settings.peerPercent;
}

bool Policy::Fits(Place const& place, uint32_t level, bool peerSeat, Settings const& settings)
{
    if (!peerSeat)
        return level >= place.minLevel && level <= place.maxLevel;
    for (uint32_t peer : Peers(place, settings))
        if (uint32_t(std::abs(int(level) - int(peer))) <= settings.maximumLevelDifference)
            return true;
    return false;
}

bool Policy::RaceFits(Place const& place, uint32_t race, uint32_t level)
{
    return level >= STARTER_LEVEL || !race || (place.homeRaceMask & (1u << (race - 1)));
}

uint32_t Policy::PlanLevel(Place const& place, bool peerSeat, uint32_t hash, uint32_t levelCap,
                           Settings const& settings)
{
    auto peers = Peers(place, settings);
    // New arrivals may start higher than people already living here (a full starting zone).
    int lowest = int(std::max(place.minLevel, place.arrivalMinLevel));
    int level;
    if (peerSeat && !peers.empty())
        // A peer still belongs to the place: a level-1 player in a capital meets its youngest regulars.
        level = std::clamp(int(peers[hash % peers.size()]) + int((hash / 7) % (2 * PEER_SPREAD + 1)) -
                               int(PEER_SPREAD),
                           lowest, std::max(lowest, int(place.maxLevel)));
    else if (!place.naturalLevels.empty())
        level = std::clamp(int(place.naturalLevels[hash % place.naturalLevels.size()]) + int((hash / 7) % 4) - 2,
                           lowest, std::max(lowest, int(place.maxLevel)));
    else
        level = lowest + int(hash % uint32_t(std::max(1, int(place.maxLevel) - lowest + 1)));
    return uint32_t(std::clamp(level, 1, int(levelCap)));
}

std::vector<Hub> const& Policy::Hubs()
{
    // Capitals by zone ID with approximate centres; walkable spots are taken from city NPC spawns.
    static std::vector<Hub> const hubs = {
        // zone, map, expansion, centre, radius, lowest usual level, home races (bit race-1)
        {1637, 1, 0, 1650.0f, -4400.0f, 500.0f, 10, (1u << 1) | (1u << 7)},    // Orgrimmar: Orc, Troll
        {1638, 1, 0, -1280.0f, 120.0f, 350.0f, 10, 1u << 5},                  // Thunder Bluff: Tauren
        {1497, 0, 0, 1600.0f, 240.0f, 250.0f, 10, 1u << 4},                   // Undercity: Undead
        {1519, 0, 0, -8800.0f, 650.0f, 550.0f, 10, 1u << 0},                  // Stormwind City: Human
        {1537, 0, 0, -4870.0f, -980.0f, 300.0f, 10, (1u << 2) | (1u << 6)},   // Ironforge: Dwarf, Gnome
        {1657, 1, 0, 9900.0f, 2300.0f, 450.0f, 10, 1u << 3},                  // Darnassus: Night Elf
        {3557, 530, 1, -3966.0f, -11654.0f, 350.0f, 10, 1u << 10},            // The Exodar: Draenei
        {3487, 530, 1, 9488.0f, -7279.0f, 400.0f, 10, 1u << 9},               // Silvermoon City: Blood Elf
        {3703, 530, 1, -1850.0f, 5400.0f, 450.0f, 58, 0},                     // Shattrath City
        {4395, 571, 2, 5800.0f, 650.0f, 400.0f, 68, 0},                       // Dalaran
    };
    return hubs;
}

bool Policy::HubAvailable(Hub const& hub, uint32_t expansion, uint32_t levelCap)
{
    return hub.expansion <= expansion && hub.expansion < std::size(EXPANSION_LEVEL_FLOOR) &&
           levelCap >= EXPANSION_LEVEL_FLOOR[hub.expansion];
}

std::vector<Guid> Policy::CullOrder(std::vector<Retention> candidates, uint32_t count, Settings const& settings)
{
    auto expiry = [&](Retention const& r)
    { return r.lastSeen + Seconds(settings.retentionSeconds) * (Seconds(r.score) + 1); };
    std::stable_sort(candidates.begin(), candidates.end(),
                     [&](Retention const& a, Retention const& b) { return expiry(a) < expiry(b); });
    std::vector<Guid> order;
    for (uint32_t i = 0; i < count && i < candidates.size(); ++i)
        order.push_back(candidates[i].id);
    return order;
}

std::vector<Assignment> Policy::Select(std::vector<Character> const& bots, std::vector<Human> const& humans,
                                       std::vector<Place> const& places,
                                       std::unordered_map<uint64_t, Familiarity> const& familiarity,
                                       Settings const& settings, Seconds now)
{
    uint32_t target = humans.empty() ? settings.offlineOnline : settings.onlineTarget;
    std::vector<Assignment> selected;
    std::unordered_set<Guid> used;

    // Finishing a fight or promise outranks a lowered population target. No new admissions
    // take place while protected actors alone exceed that target.
    for (auto const& bot : bots)
        if (bot.online && bot.engaged)
        {
            selected.push_back({bot.id, 0});
            used.insert(bot.id);
        }

    auto available = [&](Character const& bot)
    {
        if (used.count(bot.id) || (!bot.online && bot.restUntil > now))
            return false;
        if (bot.online && !CanBePresent(bot, settings, now))
            return false;
        return true;
    };

    // Rotate which place gets a remainder seat; ordering cannot permanently starve one faction.
    std::vector<Place const*> demands;
    for (auto const& place : places)
        if (place.zone && Quota(place, settings))
            demands.push_back(&place);
    if (!demands.empty())
        std::rotate(demands.begin(), demands.begin() + (now / settings.sessionSeconds) % demands.size(), demands.end());

    uint32_t wanted = 0;
    std::vector<uint32_t> quota(demands.size(), 0);
    for (size_t place = 0; place < demands.size(); ++place)
        wanted += quota[place] = Quota(*demands[place], settings);
    uint32_t regionalTarget = humans.empty() ? 0 : std::min(target * settings.regionalPercent / 100, wanted);
    if (wanted > regionalTarget)
        for (auto& seats : quota)
            seats = seats * regionalTarget / wanted;

    std::vector<uint32_t> allocated(demands.size(), 0);
    uint32_t regionalCount = 0;
    auto inPlace = [](Character const& bot, Place const& place)
    { return bot.faction == place.faction && bot.map == place.map && bot.zone == place.zone; };
    for (auto const& bot : bots)
    {
        if (!used.count(bot.id))
            continue;
        for (size_t place = 0; place < demands.size(); ++place)
            if (inPlace(bot, *demands[place]))
            {
                if (allocated[place] < quota[place])
                {
                    ++allocated[place];
                    ++regionalCount;
                }
                break;
            }
    }
    // Protected crowds count only toward their place's fair allocation. Their
    // excess still occupies overall capacity, without consuming another place's share.
    // Places players are in are filled first; capitals' standing residents take what is left.
    for (bool residents : {false, true})
    for (uint32_t round = 0;
         !demands.empty() && selected.size() < target && regionalCount < regionalTarget && round < target; ++round)
    {
        bool found = false;
        for (size_t index = 0; index < demands.size() && selected.size() < target && regionalCount < regionalTarget;
             ++index)
        {
            if (allocated[index] >= quota[index] || demands[index]->resident != residents)
                continue;
            Place const& place = *demands[index];
            uint32_t preference = (allocated[index] * 37) % 100;
            bool peerFirst = PeerSeat(place, allocated[index], settings);
            Character const* best = nullptr;
            Guid bestHuman = 0;
            for (bool peerSeat : {peerFirst, !peerFirst})
            {
                float bestScore = -std::numeric_limits<float>::infinity();
                for (auto const& bot : bots)
                {
                    if (!available(bot) || bot.faction != place.faction)
                        continue;
                    // An established adventurer elsewhere cannot satisfy local demand merely
                    // by logging in. Preserve their actual location; never teleport a reunion.
                    if (bot.initialized && (bot.map != place.map || bot.zone != place.zone))
                        continue;
                    // A place its players have left only keeps the residents still there.
                    if (!place.accepting && !(bot.online && inPlace(bot, place)))
                        continue;
                    if (!Fits(place, bot.level, peerSeat, settings) ||
                        (!bot.initialized && !RaceFits(place, bot.race, bot.level)))
                        continue;
                    float familiar = 0.0f;
                    bool recognized = false;
                    Guid owner = place.humans.empty() ? 0 : place.humans.front().id;
                    for (auto const& human : place.humans)
                    {
                        auto it = familiarity.find(Pair(human.id, bot.id));
                        if (it != familiarity.end())
                        {
                            recognized |= it->second.score >= settings.familiarityThreshold;
                            if (FamiliarityWeight(it->second, now) > familiar)
                            {
                                familiar = FamiliarityWeight(it->second, now);
                                owner = human.id;
                            }
                        }
                    }
                    bool familiarSlot = preference < settings.familiarPercent;
                    bool freshSlot = preference >= settings.familiarPercent &&
                                     preference < settings.familiarPercent + settings.freshPercent;
                    float score = float(Mix(bot.id ^ uint32_t(now / settings.sessionSeconds)) % 1000) / 1000.0f;
                    score += familiarSlot ? std::min(20.0f, familiar) : (!recognized ? 10.0f : 0.0f);
                    if (freshSlot && !bot.introduced)
                        score += 15.0f;
                    if (inPlace(bot, place))
                        score += 8.0f;
                    if (bot.online)
                        score += now < bot.sessionStarted + settings.minimumResidence ? 30.0f : 3.0f;
                    if (score > bestScore)
                    {
                        best = &bot;
                        bestScore = score;
                        bestHuman = owner;
                    }
                }
                if (best)
                    break;
            }
            if (best)
            {
                selected.push_back({best->id, bestHuman, place.map, place.zone});
                used.insert(best->id);
                ++allocated[index];
                ++regionalCount;
                found = true;
            }
        }
        if (!found)
            break;
    }

    // Baseline seats preserve every faction/level range without tying the world to the
    // highest human. Prefer underrepresented brackets, then stable existing sessions.
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> counts;
    for (auto const& bot : bots)
        if (used.count(bot.id))
            ++counts[{bot.faction, (bot.level - 1) / 10}];
    // A place a player is in has its own seats; baseline seats never add to it, so its size holds.
    std::set<std::tuple<uint32_t, uint32_t, uint32_t>> occupied;
    for (auto const& place : places)
        if (place.accepting && !place.resident)
            occupied.insert({place.faction, place.map, place.zone});
    while (selected.size() < target)
    {
        Character const* best = nullptr;
        float bestScore = -std::numeric_limits<float>::infinity();
        for (auto const& bot : bots)
        {
            if (!available(bot) || (bot.initialized && occupied.count({bot.faction, bot.map, bot.zone})))
                continue;
            float score = float(Mix(bot.id ^ uint32_t(now / settings.sessionSeconds)) % 1000) / 1000.0f;
            score -= float(counts[{bot.faction, (bot.level - 1) / 10}]) * 10.0f;
            if (bot.online)
                score += now < bot.sessionStarted + settings.minimumResidence ? 30.0f : 3.0f;
            if (score > bestScore)
            {
                best = &bot;
                bestScore = score;
            }
        }
        if (!best)
            break;
        selected.push_back({best->id, 0});
        used.insert(best->id);
        ++counts[{best->faction, (best->level - 1) / 10}];
    }
    return selected;
}
}  // namespace RealmPopulation
