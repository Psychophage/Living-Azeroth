/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */
// Living Azeroth population: deterministic policy shared by runtime and simulated-time tests.
#ifndef PLAYERBOTS_POPULATION_MGR_H
#define PLAYERBOTS_POPULATION_MGR_H

#include <atomic>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <tuple>
#include <unordered_set>
#include <vector>

#include "AsyncCallbackProcessor.h"
#include "PopulationPolicy.h"
#include "Position.h"
#include "Transaction.h"

class Player;
class ChatHandler;

class PlayerbotPopulationMgr
{
public:
    static PlayerbotPopulationMgr& Instance();
    void Configure();
    void Initialize();
    void Update();  // World thread only, after native playerbot operations.
    void OnLogin(Player* bot);
    bool Enabled() const { return _enabled.load(); }
    bool Contains(uint32_t bot) const;
    bool AllowsActivity(uint32_t bot) const;
    float XpMultiplier(Player* bot) const;
    void Interaction(uint32_t human, uint32_t bot);  // Queue from any map thread; no SQL.
    bool Protected(uint32_t bot) const;
    std::string Status() const;
    // The characters a player has come to know best, best first: (bot GUID counter, familiarity
    // score). World thread only.
    std::vector<std::pair<uint32_t, uint32_t>> FamiliarTo(uint32_t human, std::size_t limit) const;
    RealmPopulation::Settings const& GetSettings() const { return _settings; }

private:
    struct Record
    {
        RealmPopulation::Character character;
        uint32_t account = 0;
        uint32_t playerClass = 0;
        uint32_t race = 0;
        uint32_t demandHuman = 0;
        uint32_t plannedLevel = 1;
        uint64_t loadingUntil = 0;
        uint64_t protectedUntil = 0;
    };
    struct Snapshot
    {
        std::unordered_set<uint32_t> characters;
        std::unordered_set<uint32_t> active;
        std::unordered_set<uint32_t> protectedBots;
        std::unordered_map<uint64_t, float> catchup;
    };
    void Load();
    void Save(Record const& record);
    bool SaveWorld();
    void SaveFamiliarity(uint64_t key, RealmPopulation::Familiarity const& value);
    void AdaptReserve(std::vector<RealmPopulation::Place> const& places, uint64_t now);
    void CreateBatch(std::vector<RealmPopulation::Place> const& places, bool humansPresent, uint64_t now);
    // offlineKept: bots chosen to stay online while nobody plays; only they use the offline allowance.
    void Publish(std::vector<RealmPopulation::Human> const& humans, uint64_t now,
                 std::unordered_set<uint32_t> const& offlineKept = {});
    std::vector<RealmPopulation::Place> BuildPlaces(std::vector<RealmPopulation::Human> const& humans, uint64_t now);
    void LoadZoneLevels();
    // Where people gather in a zone: service NPCs (inns, flight masters, vendors, banks), collected once.
    std::vector<WorldLocation> const& ServiceSpots(uint32_t map, uint32_t zone);
    // A gathering spot just out of players' sight; allowInView lets one arrival appear in view.
    bool ServiceArrival(uint32_t map, uint32_t zone, bool allowInView, Record const& record, WorldLocation& location);
    // A race's starting area (Northshire Valley, Sunstrider Isle, ...) only receives its own young characters.
    bool StarterAreaAllows(WorldLocation const& location, uint32_t race, uint32_t level) const;
    // Arrivals spread out: no new character where LocalCrowd characters already stand within reach.
    bool Crowded(WorldLocation const& location) const;
    std::vector<WorldLocation> _crowd;  // where online population characters stand this update
    std::set<std::tuple<uint32_t, uint32_t, uint32_t>> _occupied;  // faction, map, zone players are in
    std::unordered_map<uint32_t, uint64_t> _lastInViewArrival;  // zone -> time
    bool PlaceNewcomer(Record const& record, RealmPopulation::Assignment const& assignment, WorldLocation& location);
    void Cull(uint64_t now);
    void Observe(Player* human, Player* bot, bool meaningful, uint64_t now);
    bool IsEngaged(Player* bot, std::vector<Player*> const& humans, uint64_t now);

    std::atomic<bool> _enabled{false};
    bool _loaded = false;
    bool _persistenceHealthy = true;
    RealmPopulation::Settings _settings;
    RealmPopulation::WorldState _world;
    std::unordered_map<uint32_t, Record> _bots;
    std::unordered_map<uint64_t, RealmPopulation::Familiarity> _familiarity;
    struct Presence
    {
        RealmPopulation::Human human;  // last known state of the player who made this place
        float weight = 0.0f;
    };
    struct ZoneLevels
    {
        uint32_t minLevel = 0;
        uint32_t maxLevel = 0;
        std::vector<uint32_t> levels;
    };
    // Player -> (map << 32 | zone) -> presence, ramping in and lingering out.
    std::unordered_map<uint32_t, std::unordered_map<uint64_t, Presence>> _presence;
    struct Surroundings
    {
        uint32_t map = 0, zone = 0, level = 0, bots = 0;
    };
    std::vector<Surroundings> _surroundings;  // per player, from the latest update; for Status()
    uint64_t _lastPresenceUpdate = 0;
    std::unordered_map<uint32_t, ZoneLevels> _zoneLevels;
    std::unordered_map<uint32_t, std::vector<WorldLocation>> _serviceSpots;
    std::unordered_map<uint32_t, uint32_t> _homeZones;  // zone -> races that start there (bit race-1)
    std::unordered_map<uint32_t, uint32_t> _starterAreas;  // starting area -> races that start in it
    std::unordered_map<uint32_t, std::unordered_set<uint32_t>> _accountClasses;
    std::vector<uint32_t> _accounts;
    AsyncCallbackProcessor<TransactionCallback> _transactions;
    uint32_t _pendingCreations = 0;
    std::string _pendingAccount;  // Native account creation commits asynchronously.
    std::atomic<std::shared_ptr<Snapshot const>> _snapshot{std::make_shared<Snapshot>()};
    std::mutex _interactionMutex;
    std::unordered_set<uint64_t> _interactions;
    uint64_t _nextUpdate = 0;
    uint64_t _selectionMicros = 0;
};
#endif
