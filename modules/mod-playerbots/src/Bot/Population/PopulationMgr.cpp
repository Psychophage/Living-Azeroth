/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */
// Living Azeroth population: world-thread scheduling, encounter capture and activity snapshots.
#include "PopulationMgr.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <map>
#include <tuple>
#include <unordered_set>
#include <ctime>
#include <sstream>

#include "CharacterCache.h"
#include "DBCStores.h"
#include "MapMgr.h"
#include "ObjectMgr.h"
#include "Random.h"
#include "RaceMgr.h"
#include "ScriptMgr.h"
#include "World.h"
#include "DatabaseEnv.h"
#include "Group.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotDialogue.h"
#include "PlayerbotFactory.h"
#include "Playerbots.h"
#include "PlayerbotsDatabase.h"
#include "RandomPlayerbotMgr.h"
#include "SocialMgr.h"
#include "WorldSession.h"

using namespace RealmPopulation;

PlayerbotPopulationMgr& PlayerbotPopulationMgr::Instance()
{
    static PlayerbotPopulationMgr instance;
    return instance;
}

bool PlayerbotPopulationMgr::Contains(uint32_t bot) const
{
    return Enabled() && _snapshot.load()->characters.count(bot);
}

bool PlayerbotPopulationMgr::AllowsActivity(uint32_t bot) const
{
    if (!Enabled())
        return true;
    auto snapshot = _snapshot.load();
    return !snapshot->characters.count(bot) || snapshot->active.count(bot);
}

bool PlayerbotPopulationMgr::Protected(uint32_t bot) const
{
    return Enabled() && _snapshot.load()->protectedBots.count(bot);
}

float PlayerbotPopulationMgr::XpMultiplier(Player* bot) const
{
    if (!Enabled() || !bot)
        return 1.0f;
    auto snapshot = _snapshot.load();
    Group* group = bot->GetGroup();
    if (!group)
        return 1.0f;
    float multiplier = 1.0f;
    for (auto ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* human = ref->GetSource();
        if (!human || human->GetSession()->IsBot() || !human->IsInWorld() || human->GetLevel() <= bot->GetLevel() ||
            human->GetMap() != bot->GetMap() || !human->InSamePhase(bot) || !human->IsWithinDistInMap(bot, 100.0f))
            continue;
        auto it = snapshot->catchup.find(Policy::Pair(human->GetGUID().GetCounter(), bot->GetGUID().GetCounter()));
        if (it != snapshot->catchup.end())
            multiplier =
                std::max(multiplier, std::min(it->second, 1.0f + float(human->GetLevel() - bot->GetLevel()) * 0.05f));
    }
    return multiplier;
}

void PlayerbotPopulationMgr::Interaction(uint32_t human, uint32_t bot)
{
    if (!Enabled() || !human || !Contains(bot))
        return;
    std::lock_guard lock(_interactionMutex);
    // Work scales with distinct participant pairs, never chat message frequency.
    if (_interactions.size() < 10000)
        _interactions.insert(Policy::Pair(human, bot));
}

void PlayerbotPopulationMgr::Observe(Player* human, Player* bot, bool meaningful, uint64_t now)
{
    if (!human || !bot || human->GetSession()->IsBot())
        return;
    uint32_t id = bot->GetGUID().GetCounter();
    auto it = _bots.find(id);
    if (it == _bots.end())
        return;
    auto& record = it->second;
    uint64_t key = Policy::Pair(human->GetGUID().GetCounter(), id);
    auto& value = _familiarity[key];
    bool friendListed = human->GetSocial() && human->GetSocial()->HasFriend(bot->GetGUID());
    meaningful |= friendListed;
    if (friendListed)
        value.score = std::max(value.score, _settings.familiarityThreshold);
    if (Policy::Observe(value, _settings, now, now / (6 * 3600), meaningful))
        SaveFamiliarity(key, value);
    if (!record.character.introduced)
    {
        record.character.introduced = true;
        record.character.lastSeen = now;
        Save(record);
    }
    if (meaningful)
        record.protectedUntil = std::max(record.protectedUntil, now + 300);
    record.character.lastSeen = now;
}

bool PlayerbotPopulationMgr::IsEngaged(Player* bot, std::vector<Player*> const& humans, uint64_t now)
{
    auto& record = _bots.at(bot->GetGUID().GetCounter());
    // With nobody online, a fight or a flight protects no one's experience; hostile areas would
    // otherwise keep a bot in combat (and online) indefinitely.
    if (((bot->IsInCombat() || bot->HasUnitState(UNIT_STATE_IN_FLIGHT)) && !humans.empty()) || bot->GetTradeData() ||
        bot->IsBeingTeleported() || bot->InBattleground() || bot->GetMap()->IsDungeon() ||
        record.protectedUntil > now || PlayerbotDialogueBridge::HasPendingWork(bot->GetGUID().GetRawValue()))
        return true;
    for (Player* human : humans)
    {
        if (bot->GetGroup() && human->GetGroup() == bot->GetGroup())
            return true;
        // No population logout/replacement inside an actual human's visible vicinity.
        if (human->GetMap() == bot->GetMap() && human->InSamePhase(bot) && human->IsWithinDistInMap(bot, 120.0f))
            return true;
    }
    return false;
}

namespace
{
constexpr uint32_t ZONE_LEVEL_MARGIN = 3; // exploration levels start a little above a zone's first quests
constexpr uint32_t STARTER_AREA_MAX_LEVEL = 6;  // characters move on from their starting area by about here
constexpr float CROWD_RANGE = 50.0f;
constexpr uint32_t SERVICE_SPOT_LIMIT = 600;
constexpr uint32_t UNSEEN_SPOT_ATTEMPTS = 40;
constexpr float SIGHT_RANGE = 150.0f;
constexpr float WANDER_IN_RANGE = 300.0f;         // just out of sight: likely to come into view
constexpr float SERVICE_SPOT_SPREAD = 4.0f;       // stand beside the NPC, not inside it
// Where people gather in a capital: services, with the busiest ones counted more often.
constexpr uint32_t HUB_SERVICE_FLAGS = UNIT_NPC_FLAG_VENDOR_MASK | UNIT_NPC_FLAG_TRAINER | UNIT_NPC_FLAG_REPAIR;
constexpr uint32_t HUB_BUSY_FLAGS =
    UNIT_NPC_FLAG_BANKER | UNIT_NPC_FLAG_AUCTIONEER | UNIT_NPC_FLAG_INNKEEPER | UNIT_NPC_FLAG_FLIGHTMASTER;
constexpr uint32_t HUB_BUSY_WEIGHT = 3;

uint32_t RaceBit(uint32_t race) { return race ? 1u << (race - 1) : 0; }

uint64_t PlaceKey(uint32_t map, uint32_t zone) { return (uint64_t(map) << 32) | zone; }
}  // namespace

void PlayerbotPopulationMgr::LoadZoneLevels()
{
    _zoneLevels.clear();
    for (AreaTableEntry const* area : sAreaTableStore)
        if (area && area->area_level > 0)
            _zoneLevels[area->zone ? area->zone : area->ID].levels.push_back(uint32_t(area->area_level));
    for (auto& [zone, profile] : _zoneLevels)
    {
        std::sort(profile.levels.begin(), profile.levels.end());
        profile.minLevel = profile.levels.front() > ZONE_LEVEL_MARGIN ? profile.levels.front() - ZONE_LEVEL_MARGIN : 1;
        profile.maxLevel = profile.levels.back();
    }
    // Where each race starts, from the server's own character creation data.
    _homeZones.clear();
    for (uint8 race = RACE_HUMAN; race < sRaceMgr->GetMaxRaces(); ++race)
        for (uint8 cls = CLASS_WARRIOR; cls < MAX_CLASSES; ++cls)
            if (PlayerInfo const* info = sObjectMgr->GetPlayerInfo(race, cls); info && cls != CLASS_DEATH_KNIGHT)
            {
                AreaTableEntry const* area = sAreaTableStore.LookupEntry(info->areaId);
                _homeZones[area && area->zone ? area->zone : info->areaId] |= RaceBit(race);
                // The area under the starting position is the race's starting area.
                if (Map* map = sMapMgr->CreateBaseMap(info->mapId))
                    _starterAreas[map->GetAreaId(PHASEMASK_NORMAL, info->positionX, info->positionY,
                                                 info->positionZ)] |= RaceBit(race);
            }
}

std::vector<Place> PlayerbotPopulationMgr::BuildPlaces(std::vector<Human> const& humans, uint64_t now)
{
    uint64_t elapsed = _lastPresenceUpdate && now > _lastPresenceUpdate ? now - _lastPresenceUpdate : 0;
    _lastPresenceUpdate = now;
    std::unordered_map<uint32_t, Human const*> current;
    for (auto const& human : humans)
        current[human.id] = &human;
    for (auto const& human : humans)
        if (human.zone)
            _presence[human.id].try_emplace(PlaceKey(human.map, human.zone)).first->second.human = human;
    for (auto it = _presence.begin(); it != _presence.end();)
    {
        auto player = current.find(it->first);
        for (auto place = it->second.begin(); place != it->second.end();)
        {
            bool here = player != current.end() && player->second->zone &&
                        PlaceKey(player->second->map, player->second->zone) == place->first;
            if (player != current.end())
                place->second.human.level = player->second->level;  // peers follow the player's level
            place->second.weight = Policy::AdvancePresence(place->second.weight, here, elapsed, _settings);
            place = !here && place->second.weight <= 0.0f ? it->second.erase(place) : std::next(place);
        }
        it = it->second.empty() ? _presence.erase(it) : std::next(it);
    }
    uint32_t expansion = sWorld->getIntConfig(CONFIG_EXPANSION);
    uint32_t cap = sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL);
    std::map<std::tuple<uint32_t, uint32_t, uint32_t>, Place> merged;
    // While someone plays, their faction's capitals (and the neutral cities their level reaches)
    // keep a few settled residents, so a city is already alive when they arrive.
    float residentWeight = float(_settings.residentPercent) / 100.0f;
    if (residentWeight > 0.0f)
        for (auto const& human : humans)
            for (auto const& hub : Policy::Hubs())
            {
                AreaTableEntry const* area = sAreaTableStore.LookupEntry(hub.zone);
                bool friendly = area && (area->team == AREATEAM_ANY || (area->team == AREATEAM_ALLY) == !human.faction);
                if (!Policy::HubAvailable(hub, expansion, cap) || !friendly ||
                    (!hub.raceMask && human.level + _settings.maximumLevelDifference < hub.minLevel))
                    continue;
                Place& place = merged[{human.faction, hub.map, hub.zone}];
                place.faction = human.faction;
                place.map = hub.map;
                place.zone = hub.zone;
                place.weight = std::max(place.weight * !place.humans.empty(), residentWeight);
                place.resident = place.humans.empty() || place.resident;
                place.accepting = true;
                place.humans.push_back(human);
            }
    for (auto const& [humanId, places] : _presence)
        for (auto const& [key, presence] : places)
        {
            Human const& human = presence.human;
            Place& place = merged[{human.faction, human.map, human.zone}];
            place.faction = human.faction;
            place.map = human.map;
            place.zone = human.zone;
            place.weight = place.humans.empty() ? presence.weight : std::max(place.weight, presence.weight);
            if (std::find_if(place.humans.begin(), place.humans.end(),
                             [&](Human const& h) { return h.id == human.id; }) != place.humans.end())
            {
                bool here = current.count(humanId) && current.at(humanId)->map == human.map &&
                            current.at(humanId)->zone == human.zone;
                place.accepting |= here;
                place.resident &= !here;
                continue;  // already counted as a resident visitor of this capital
            }
            auto here = current.find(humanId);
            bool accepting = here != current.end() && here->second->map == human.map && here->second->zone == human.zone;
            place.accepting = (!place.humans.empty() && place.accepting) || accepting;
            place.resident &= !accepting;
            place.humans.push_back(human);
        }
    std::vector<Place> places;
    for (auto& [key, place] : merged)
    {
        auto hub = std::find_if(Policy::Hubs().begin(), Policy::Hubs().end(),
                                [&](Hub const& h) { return h.zone == place.zone && h.map == place.map; });
        auto profile = _zoneLevels.find(place.zone);
        auto home = _homeZones.find(place.zone);
        place.homeRaceMask = home != _homeZones.end() ? home->second : 0;
        // A starting zone's young characters gather at its few quest givers. Once it holds
        // StarterCapacity of them, newcomers arrive old enough to be elsewhere in the zone.
        uint32_t young = 0;
        if (home != _homeZones.end())
            for (auto const& [id, record] : _bots)
                young += record.character.online && record.character.map == place.map &&
                         record.character.zone == place.zone && record.character.level <= STARTER_AREA_MAX_LEVEL;
        bool youngFull = home != _homeZones.end() && young >= _settings.starterCapacity;
        if (hub != Policy::Hubs().end() && Policy::HubAvailable(*hub, expansion, cap))
        {
            // Capitals see every level from their usual lowest up to the cap, evenly.
            place.hub = true;
            place.minLevel = std::min(hub->minLevel, cap);
            place.maxLevel = cap;
            place.homeRaceMask |= hub->raceMask;
            for (uint32_t level = place.minLevel; level <= cap; ++level)
                place.naturalLevels.push_back(level);
        }
        else if (profile != _zoneLevels.end() && profile->second.minLevel <= cap)
        {
            place.minLevel = profile->second.minLevel;
            if (youngFull)
                place.arrivalMinLevel = STARTER_AREA_MAX_LEVEL + 1;
            place.maxLevel = std::min(profile->second.maxLevel, cap);
            for (uint32_t level : profile->second.levels)
                if (level <= cap)
                    place.naturalLevels.push_back(level);
        }
        else
        {
            // No level data: serve the players' own levels, as before zone levels existed.
            place.minLevel = cap;
            place.maxLevel = 1;
            for (auto const& human : place.humans)
            {
                place.minLevel = std::min(place.minLevel, human.level > _settings.maximumLevelDifference
                                                              ? human.level - _settings.maximumLevelDifference : 1);
                place.maxLevel = std::max(place.maxLevel, std::min(cap, human.level + _settings.maximumLevelDifference));
                place.naturalLevels.push_back(human.level);
            }
        }
        places.push_back(std::move(place));
    }
    return places;
}

std::vector<WorldLocation> const& PlayerbotPopulationMgr::ServiceSpots(uint32_t mapId, uint32_t zone)
{
    auto [it, inserted] = _serviceSpots.try_emplace(zone);
    if (!inserted)
        return it->second;
    Map* map = sMapMgr->CreateBaseMap(mapId);
    if (!map)
        return it->second;
    auto hub = std::find_if(Policy::Hubs().begin(), Policy::Hubs().end(),
                            [&](Hub const& h) { return h.zone == zone && h.map == mapId; });
    for (auto const& [spawnId, data] : sObjectMgr->GetAllCreatureData())
    {
        if (data.mapid != mapId || it->second.size() >= SERVICE_SPOT_LIMIT)
            continue;
        if (hub != Policy::Hubs().end())
        {
            float dx = data.posX - hub->x, dy = data.posY - hub->y;
            if (dx * dx + dy * dy > hub->radius * hub->radius)
                continue;
        }
        CreatureTemplate const* creature = sObjectMgr->GetCreatureTemplate(data.id);
        uint32 flags = data.npcflag ? data.npcflag : (creature ? creature->npcflag : 0);
        if (!(flags & (HUB_SERVICE_FLAGS | HUB_BUSY_FLAGS)))
            continue;
        if (map->GetZoneId(PHASEMASK_NORMAL, data.posX, data.posY, data.posZ) != zone)
            continue;
        it->second.insert(it->second.end(), flags & HUB_BUSY_FLAGS ? HUB_BUSY_WEIGHT : 1,
                          WorldLocation(mapId, data.posX, data.posY, data.posZ, 0.0f));
    }
    LOG_INFO("playerbots", "Population zone {}: {} gathering spots", zone, it->second.size());
    return it->second;
}

bool PlayerbotPopulationMgr::StarterAreaAllows(WorldLocation const& location, uint32_t race, uint32_t level) const
{
    Map* map = sMapMgr->CreateBaseMap(location.GetMapId());
    if (!map)
        return true;
    auto starter = _starterAreas.find(map->GetAreaId(PHASEMASK_NORMAL, location.GetPositionX(),
                                                     location.GetPositionY(), location.GetPositionZ()));
    return starter == _starterAreas.end() || ((starter->second & RaceBit(race)) && level <= STARTER_AREA_MAX_LEVEL);
}

bool PlayerbotPopulationMgr::Crowded(WorldLocation const& location) const
{
    uint32_t nearby = 0;
    for (auto const& other : _crowd)
        if (other.GetMapId() == location.GetMapId() &&
            other.GetExactDist2dSq(location.GetPositionX(), location.GetPositionY()) < CROWD_RANGE * CROWD_RANGE &&
            ++nearby >= _settings.localCrowd)
            return true;
    return false;
}

bool PlayerbotPopulationMgr::ServiceArrival(uint32_t mapId, uint32_t zone, bool allowInView, Record const& record,
                                            WorldLocation& location)
{
    auto const& spots = ServiceSpots(mapId, zone);
    if (spots.empty())
        return false;
    // Logging in at an inn or bank in view is ordinary, but only now and then: a trickle in view,
    // the rest just out of sight so they wander in, rather than a crowd buffing on arrival.
    WorldLocation nearby, distant;
    bool haveNearby = false, haveDistant = false;
    for (uint32_t attempt = 0; attempt < UNSEEN_SPOT_ATTEMPTS; ++attempt)
    {
        WorldLocation spot = spots[urand(0, spots.size() - 1)];
        Map* map = sMapMgr->CreateBaseMap(spot.GetMapId());
        float x = spot.GetPositionX() + frand(-SERVICE_SPOT_SPREAD, SERVICE_SPOT_SPREAD);
        float y = spot.GetPositionY() + frand(-SERVICE_SPOT_SPREAD, SERVICE_SPOT_SPREAD);
        float ground = map ? map->GetHeight(PHASEMASK_NORMAL, x, y, spot.GetPositionZ() + 2.0f) : INVALID_HEIGHT;
        if (ground > INVALID_HEIGHT && std::fabs(ground - spot.GetPositionZ()) < 3.0f)
            spot.Relocate(x, y, ground + 0.05f);
        if (!StarterAreaAllows(spot, record.race, record.plannedLevel) || Crowded(spot))
            continue;
        float closest = std::numeric_limits<float>::max();
        for (Player* player : sRandomPlayerbotMgr.GetPlayers())
            if (player && player->IsInWorld() && player->GetMapId() == spot.GetMapId())
                closest = std::min(closest, player->GetDistance(spot.GetPositionX(), spot.GetPositionY(),
                                                                spot.GetPositionZ()));
        if (closest < SIGHT_RANGE)
        {
            if (allowInView)
            {
                location = spot;
                return true;
            }
        }
        else if (closest < WANDER_IN_RANGE && !haveNearby)
        {
            nearby = spot;
            haveNearby = true;
        }
        else if (!haveDistant)
        {
            distant = spot;
            haveDistant = true;
        }
    }
    if (!haveNearby && !haveDistant)
        return false;
    location = haveNearby ? nearby : distant;
    return true;
}

bool PlayerbotPopulationMgr::PlaceNewcomer(Record const& record, Assignment const& assignment, WorldLocation& location)
{
    auto hub = std::find_if(Policy::Hubs().begin(), Policy::Hubs().end(), [&](Hub const& h)
                            { return h.zone == assignment.zone && h.map == assignment.map; });
    uint64_t now = time(nullptr);
    uint64_t& lastInView = _lastInViewArrival[assignment.zone];
    bool allowInView = now >= lastInView + _settings.inViewArrivalSeconds;
    auto arrive = [&]()
    {
        if (!ServiceArrival(assignment.map, assignment.zone, allowInView, record, location))
            return false;
        if (sRandomPlayerbotMgr.VisibleToPlayers(location))
            lastInView = now;
        return true;
    };
    if (assignment.zone && hub != Policy::Hubs().end())
        return arrive();
    // Some of a zone's arrivals come by way of its towns and inns.
    if (assignment.zone && urand(0, 99) < _settings.townPercent && arrive())
        return true;
    return sRandomPlayerbotMgr.PopulationSpawnLocation(
        record.race, record.plannedLevel, assignment.map, assignment.zone, location,
        [&](WorldLocation const& spot)
        {
            if (!StarterAreaAllows(spot, record.race, record.plannedLevel) || Crowded(spot))
                return false;
            if (assignment.zone)
                return true;
            // A general arrival is not added to a place a player is in; that place has its own seats.
            Map* map = sMapMgr->CreateBaseMap(spot.GetMapId());
            uint32_t zone = map ? map->GetZoneId(PHASEMASK_NORMAL, spot.GetPositionX(), spot.GetPositionY(),
                                                 spot.GetPositionZ())
                                : 0;
            return !_occupied.count({record.character.faction, spot.GetMapId(), zone});
        });
}

void PlayerbotPopulationMgr::Cull(uint64_t now)
{
    uint32_t known = 0;
    for (auto const& [id, record] : _bots)
        known += record.character.introduced;
    if (known <= _settings.knownLimit)
        return;
    std::unordered_map<uint32_t, uint32_t> best;
    for (auto const& [pair, value] : _familiarity)
        best[uint32_t(pair)] = std::max(best[uint32_t(pair)], value.score);
    std::vector<Retention> candidates;
    uint32_t active = 0, belongings = 0, onlineFree = 0, engagedCount = 0, fighting = 0, promised = 0, pending = 0, travelling = 0,
             flying = 0;
    for (auto const& [id, record] : _bots)
    {
        auto const& c = record.character;
        if (!c.introduced)
            continue;
        if (c.online || c.engaged || record.loadingUntil > now || record.protectedUntil > now)
        {
            ++active;
            if (c.online && !c.engaged)
                ++onlineFree;
            else if (c.engaged)
            {
                ++engagedCount;
                Player* bot = ObjectAccessor::FindConnectedPlayer(ObjectGuid::Create<HighGuid::Player>(id));
                if (bot && bot->IsInCombat())
                    ++fighting;
                if (bot && (!bot->IsInWorld() || bot->IsBeingTeleported()))
                    ++travelling;
                if (bot && bot->IsInWorld() && bot->HasUnitState(UNIT_STATE_IN_FLIGHT))
                    ++flying;
                if (record.protectedUntil > now)
                    ++promised;
                if (PlayerbotDialogueBridge::HasPendingWork(ObjectGuid::Create<HighGuid::Player>(id).GetRawValue()))
                    ++pending;
            }
            continue;
        }
        auto cached = sCharacterCache->GetCharacterCacheByGuid(ObjectGuid::Create<HighGuid::Player>(id));
        if (!cached || cached->MailCount || !cached->GroupGuid.IsEmpty())
        {
            ++belongings;  // unread mail or a party: someone is still involved with them
            continue;
        }
        candidates.push_back({id, c.lastSeen, best[id]});
    }
    LOG_DEBUG("playerbots",
              "Population known {} over limit {}: {} eligible, {} active ({} engaged: {} fighting, {} protected, {} "
              "pending, {} travelling, {} flying; {} online and free), {} with mail or party",
              known, _settings.knownLimit, candidates.size(), active, engagedCount, fighting, promised, pending,
              travelling, flying, onlineFree, belongings);
    for (Guid id : Policy::CullOrder(std::move(candidates), std::min(known - _settings.knownLimit, _settings.loginBatch),
                                     _settings))
    {
        auto it = _bots.find(id);
        uint32_t account = it->second.account;
        ObjectGuid guid = ObjectGuid::Create<HighGuid::Player>(id);
        Player::DeleteFromDB(id, account, true, true);
        sScriptMgr->OnPlayerDelete(guid, account);
        sRandomPlayerbotMgr.Forget(id);
        auto character = PlayerbotsDatabase.GetPreparedStatement(PLAYERBOTS_DEL_POPULATION_CHARACTER);
        character->SetData(0, id);
        PlayerbotsDatabase.Execute(character);
        auto familiarity = PlayerbotsDatabase.GetPreparedStatement(PLAYERBOTS_DEL_POPULATION_FAMILIARITY_BOT);
        familiarity->SetData(0, id);
        PlayerbotsDatabase.Execute(familiarity);
        std::erase_if(_familiarity, [&](auto const& entry) { return uint32_t(entry.first) == id; });
        _accountClasses[account].erase(it->second.playerClass);
        _bots.erase(it);
        LOG_INFO("playerbots", "Population retired known character {} (over the known-character limit)", id);
    }
}

void PlayerbotPopulationMgr::Publish(std::vector<Human> const& humans, uint64_t now,
                                     std::unordered_set<uint32_t> const& offlineKept)
{
    auto snapshot = std::make_shared<Snapshot>();
    std::unordered_set<uint64_t> occupied;
    for (auto const& human : humans)
        if (human.zone)
            occupied.insert(PlaceKey(human.map, human.zone));
    for (auto const& [id, record] : _bots)
    {
        snapshot->characters.insert(id);
        auto const& c = record.character;
        if (c.engaged)
            snapshot->protectedBots.insert(id);
        bool opportunity = !humans.empty() && Policy::Mix(id ^ uint32_t(now / 60)) % 100 <
                                                  uint32_t(100.0f * Policy::ActivityFraction(c, humans));
        // Retaining a promise/identity is distinct from granting unlimited offline
        // simulation. Packet handling and finishing combat remain native exceptions.
        // Everyone in a zone with a player keeps moving, so occupied places look alive.
        if ((!humans.empty() && (c.engaged || (c.online && occupied.count(PlaceKey(c.map, c.zone))))) ||
            (_persistenceHealthy &&
             (opportunity || (humans.empty() && _world.offlineRemaining && offlineKept.count(id)))))
            snapshot->active.insert(id);
        for (auto const& human : humans)
        {
            auto relation = _familiarity.find(Policy::Pair(human.id, id));
            if (relation == _familiarity.end())
                continue;
            float multiplier = Policy::Catchup(c, human, relation->second, _settings);
            if (multiplier > 1.0f)
                snapshot->catchup[Policy::Pair(human.id, id)] = multiplier;
        }
    }
    _snapshot.store(std::move(snapshot));
}

void PlayerbotPopulationMgr::OnLogin(Player* bot)
{
    if (!Enabled() || !bot)
        return;
    auto it = _bots.find(bot->GetGUID().GetCounter());
    if (it == _bots.end())
        return;
    auto& record = it->second;
    uint64_t now = time(nullptr);
    record.loadingUntil = 0;
    if (!record.character.initialized && record.character.introduced)
    {
        // After a crash, never re-randomize an identity already encountered by a human.
        record.character.initialized = true;
        Save(record);
    }
    if (!record.character.initialized)
    {
        // Factory work is confined to an unintroduced identity. Complete its character
        // transaction before publishing the durable initialized state.
        PlayerbotFactory factory(bot, record.plannedLevel);
        factory.Randomize(false);
        auto transaction = CharacterDatabase.BeginTransaction();
        bot->SaveToDB(transaction, false, false);
        uint32_t id = record.character.id;
        _transactions.AddCallback(CharacterDatabase.AsyncCommitTransaction(transaction))
            .AfterComplete(
                [this, id](bool success)
                {
                    if (!success)
                    {
                        LOG_ERROR("playerbots", "Population initialization save failed for {}", id);
                        return;
                    }
                    auto it = _bots.find(id);
                    if (it != _bots.end())
                    {
                        it->second.character.initialized = true;
                        Save(it->second);
                        // Initialization can be saved before native login adds the bot to
                        // its map. Restore online presence only if that session still exists.
                        auto guid = ObjectGuid::Create<HighGuid::Player>(id);
                        Player* connected = ObjectAccessor::FindConnectedPlayer(guid);
                        if (connected && !connected->GetSession()->PlayerLogout())
                        {
                            auto online = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CHAR_ONLINE);
                            online->SetData(0, id);
                            CharacterDatabase.Execute(online);
                        }
                    }
                });
        WorldLocation home(bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());
        bot->SetHomebind(home, bot->GetZoneId());
    }
    if (!record.character.sessionStarted)
        record.character.sessionStarted = now;
    record.character.online = true;
    record.character.level = bot->GetLevel();
    record.character.map = bot->GetMapId();
    record.character.zone = bot->GetZoneId();
    Save(record);
}

void PlayerbotPopulationMgr::Update()
{
    if (!Enabled() || !_loaded)
        return;
    uint64_t now = time(nullptr);
    if (now < _nextUpdate)
        return;
    _nextUpdate = now + _settings.updateSeconds;
    _transactions.ProcessReadyCallbacks();
    std::vector<Player*> players;
    std::vector<Human> humans;
    std::unordered_set<uint32_t> present;
    for (Player* player : sRandomPlayerbotMgr.GetPlayers())
    {
        if (!player || !player->IsInWorld() || player->GetSession()->IsBot())
            continue;
        uint32_t id = player->GetGUID().GetCounter();
        players.push_back(player);
        present.insert(id);
        // Explicit friends are retained even when offline or far outside sight range.
        if (player->GetSocial())
            for (auto& [botId, record] : _bots)
                if (player->GetSocial()->HasFriend(ObjectGuid::Create<HighGuid::Player>(botId)))
                {
                    auto& relation = _familiarity[Policy::Pair(id, botId)];
                    if (relation.score < _settings.familiarityThreshold)
                    {
                        relation.score = _settings.familiarityThreshold;
                        relation.lastSeen = now;
                        SaveFamiliarity(Policy::Pair(id, botId), relation);
                    }
                    if (!record.character.introduced)
                    {
                        record.character.introduced = true;
                        Save(record);
                    }
                }
        Human human{id, IsAlliance(player->getRace()) ? 0u : 1u, player->GetMapId(), player->GetZoneId(),
                    player->GetLevel()};
        // Private instances retain their party; they do not request fresh strangers
        // inside the dungeon. These humans still count for activity and level demand.
        if (player->GetMap()->IsDungeon() || player->InBattleground())
            human.zone = 0;
        humans.push_back(human);
    }
    auto places = BuildPlaces(humans, now);
    _occupied.clear();
    for (auto const& place : places)
        if (place.accepting && !place.resident)
            _occupied.insert({place.faction, place.map, place.zone});
    Policy::Advance(_world, _settings, now, !players.empty());
    if (!SaveWorld())
    {
        Publish({}, now);
        return;
    }
    // Re-planning unseen reserve identities is not rate-limited by new character creation.
    AdaptReserve(places, now);
    CreateBatch(places, !players.empty(), now);
    std::unordered_set<uint64_t> interactions;
    {
        std::lock_guard lock(_interactionMutex);
        interactions.swap(_interactions);
    }
    for (uint64_t pair : interactions)
    {
        Player* human = ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(uint32_t(pair >> 32)));
        Player* bot = ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(uint32_t(pair)));
        if (human && bot)
            Observe(human, bot, true, now);
        else if (auto it = _bots.find(uint32_t(pair)); it != _bots.end())
        {
            // A real delivered interaction survives either participant logging out before
            // this world-thread drain. The hook already established the participants.
            auto& relation = _familiarity[pair];
            Policy::Observe(relation, _settings, now, now / (6 * 3600), true);
            SaveFamiliarity(pair, relation);
            it->second.character.introduced = true;
            it->second.character.lastSeen = now;
            Save(it->second);
        }
    }
    std::vector<Character> candidates;
    uint32_t online = 0;
    _crowd.clear();
    for (auto& [id, record] : _bots)
    {
        auto& c = record.character;
        Player* bot = ObjectAccessor::FindConnectedPlayer(ObjectGuid::Create<HighGuid::Player>(id));
        c.online = bot != nullptr;
        c.engaged = c.online && (!bot->IsInWorld() || IsEngaged(bot, players, now));
        if (c.online)
            ++online;
        if (c.online && bot->IsInWorld())
        {
            _crowd.emplace_back(bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());
            bool locationChanged = c.level != bot->GetLevel() || c.map != bot->GetMapId() || c.zone != bot->GetZoneId();
            c.level = bot->GetLevel();
            c.map = bot->GetMapId();
            c.zone = bot->GetZoneId();
            if (locationChanged)
                Save(record);
            for (Player* human : players)
            {
                bool party = bot->GetGroup() && bot->GetGroup() == human->GetGroup();
                bool nearby = human->GetMap() == bot->GetMap() && human->InSamePhase(bot) &&
                              human->IsWithinDistInMap(bot, 80.0f) && human->IsWithinLOSInMap(bot);
                bool sharedCombat = bot->IsInCombat() && human->IsInCombat() && bot->GetVictim() &&
                                    (bot->GetVictim() == human->GetVictim() || bot->GetVictim()->GetVictim() == human);
                if (nearby)
                    Observe(human, bot,
                            party || sharedCombat || (bot->GetTradeData() && bot->GetTradeData()->GetTrader() == human),
                            now);
            }
        }
        // Loading characters reserve a real slot, including when a second update arrives
        // before the asynchronous login callback. A failed login backs off for one minute.
        if (record.loadingUntil > now)
        {
            if (!c.online)
                ++online;
            continue;
        }
        c.race = record.race;
        candidates.push_back(c);
    }
    // What each player can actually meet: online bots in their zone within five levels.
    _surroundings.clear();
    for (auto const& human : humans)
    {
        uint32_t nearby = 0;
        for (auto const& [id, record] : _bots)
            nearby += record.character.online && record.character.map == human.map &&
                      record.character.zone == human.zone && record.character.level + 5 >= human.level &&
                      record.character.level <= human.level + 5;
        _surroundings.push_back({human.map, human.zone, human.level, nearby});
    }
    auto started = std::chrono::steady_clock::now();
    auto selection = Policy::Select(candidates, humans, places, _familiarity, _settings, now);
    _selectionMicros =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count();
    std::unordered_map<uint32_t, uint32_t> desired;
    std::unordered_set<uint32_t> offlineKept;
    for (auto const& assignment : selection)
    {
        desired.emplace(assignment.bot, assignment.human);
        // A fight in progress is finished, but does not earn new autonomous activity once
        // players have left; otherwise one fight leads to the next and nobody ever logs off.
        if (humans.empty() && !_bots.at(assignment.bot).character.engaged)
            offlineKept.insert(assignment.bot);
    }
    for (auto const& place : places)
    {
        if (!place.accepting || place.resident)
            continue;
        uint32_t seats = 0, present = 0, fitting = 0;
        for (auto const& assignment : selection)
            seats += assignment.map == place.map && assignment.zone == place.zone;
        for (auto const& c : candidates)
        {
            bool here = c.faction == place.faction && c.map == place.map && c.zone == place.zone;
            present += c.online && here;
            fitting += c.faction == place.faction && (here || !c.initialized) &&
                       Policy::Fits(place, c.level, false, _settings) && Policy::CanBePresent(c, _settings, now);
        }
        LOG_DEBUG("playerbots",
                  "Population place {}/{}/{}: weight {:.2f} quota {} seats {} online here {} fitting available {} "
                  "levels {}-{} arrivals from {}",
                  place.faction, place.map, place.zone, place.weight, Policy::Quota(place, _settings), seats, present,
                  fitting, place.minLevel, place.maxLevel, place.arrivalMinLevel);
    }
    uint32_t changes = 0;
    for (auto& [id, record] : _bots)
    {
        auto& c = record.character;
        if (c.online && !c.engaged && !desired.count(id) && changes < _settings.loginBatch)
        {
            c.sessionStarted = 0;
            c.restUntil = now + uint64_t(float(_settings.restSeconds) / Policy::ActivityFraction(c, humans));
            Save(record);
            sRandomPlayerbotMgr.LogoutPlayerBot(ObjectGuid::Create<HighGuid::Player>(id));
            c.online = false;
            --online;
            ++changes;
        }
    }
    uint32_t target = players.empty() ? _settings.offlineOnline : _settings.onlineTarget;
    for (auto const& assignment : selection)
    {
        auto& record = _bots.at(assignment.bot);
        if (record.character.online || record.loadingUntil > now || changes >= _settings.loginBatch || online >= target)
            continue;
        record.demandHuman = assignment.human;
        record.loadingUntil = now + 60;
        ++online;
        ++changes;
        auto guid = ObjectGuid::Create<HighGuid::Player>(assignment.bot);
        if (!record.character.initialized && !record.character.introduced)
        {
            WorldLocation location;
            if (!PlaceNewcomer(record, assignment, location))
            {
                // No spot for this level here: return the stranger to the reserve rather than
                // let an unplaceable plan hold a seat. Never first appear in a visible spot.
                record.loadingUntil = 0;
                record.demandHuman = 0;
                if (assignment.zone)
                    record.character.map = record.character.zone = 0;
                --online;
                --changes;
                continue;
            }
            if (assignment.zone)
            {
                record.character.map = assignment.map;
                record.character.zone = assignment.zone;
            }
            _crowd.push_back(location);
            auto transaction = CharacterDatabase.BeginTransaction();
            Player::SavePositionInDB(location, assignment.zone, guid, transaction);
            _transactions.AddCallback(CharacterDatabase.AsyncCommitTransaction(transaction))
                .AfterComplete([guid](bool success)
                               {
                                   if (success)
                                       sRandomPlayerbotMgr.AddPlayerBot(guid, 0);
                               });
        }
        else
            sRandomPlayerbotMgr.AddPlayerBot(guid, 0);
    }
    Cull(now);
    Publish(humans, now, offlineKept);
}

std::string PlayerbotPopulationMgr::Status() const
{
    std::ostringstream out;
    uint32_t online = 0, engaged = 0, established = 0, active = 0;
    auto snapshot = _snapshot.load();
    for (auto const& [id, record] : _bots)
    {
        online += record.character.online;
        engaged += record.character.engaged;
        established += record.character.introduced;
        active += record.character.online && snapshot->active.count(id);
    }
    out << "Population " << (Enabled() ? "enabled" : "disabled") << ": roster=" << _bots.size() << " online=" << online
        << " protected=" << engaged << " established=" << established << " active=" << active
        << " humans=" << _presence.size()
        << " persistence=" << (_persistenceHealthy ? "ok" : "failed") << " reserve_target=" << _settings.reserveTarget
        << " online_target=" << _settings.onlineTarget << " pending_creations=" << _pendingCreations
        << " relationships=" << _familiarity.size() << " offline_seconds=" << _world.offlineRemaining
        << " selection_us=" << _selectionMicros;
    // Per player: map/zone/level:online bots within five levels in that zone.
    out << " nearby=";
    for (auto const& place : _surroundings)
        out << place.map << '/' << place.zone << '/' << place.level << ':' << place.bots << ' ';
    return out.str();
}
