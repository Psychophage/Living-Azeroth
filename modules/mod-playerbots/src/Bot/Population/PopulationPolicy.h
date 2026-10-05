/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */
// Living Azeroth population: deterministic policy shared by runtime and simulated-time tests.
#ifndef PLAYERBOTS_POPULATION_POLICY_H
#define PLAYERBOTS_POPULATION_POLICY_H

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace RealmPopulation
{
using Guid = uint32_t;
using Seconds = uint64_t;

struct Settings
{
    bool enabled = false;
    uint32_t onlineTarget = 500;
    uint32_t reserveTarget = 2000;
    uint32_t seedCharacters = 40;
    uint32_t creationBatch = 5;
    uint32_t loginBatch = 10;
    uint32_t updateSeconds = 10;
    uint32_t creationSeconds = 30;
    uint32_t sessionSeconds = 3600;
    uint32_t restSeconds = 1800;
    uint32_t minimumResidence = 600;
    uint32_t offlineOnline = 20;
    uint32_t offlineAllowance = 900;
    uint32_t sightingInterval = 1800;
    uint32_t sightingSessionCap = 3;
    uint32_t interactionInterval = 300;
    uint32_t familiarityThreshold = 6;
    uint32_t regionalPercent = 70;
    uint32_t regionTarget = 50;
    uint32_t familiarPercent = 35;
    uint32_t freshPercent = 20;
    uint32_t maximumLevelDifference = 5;
    uint32_t rampSeconds = 60;        // a place fills in over this long after a player arrives
    uint32_t lingerSeconds = 600;     // and empties over this long after they leave
    uint32_t peerPercent = 40;        // seats near the player's level, when the player belongs there
    uint32_t knownLimit = 20000;      // introduced characters kept before the least valued are culled
    uint32_t retentionSeconds = 604800;  // per familiarity point (plus one) since last seen
    uint32_t residentPercent = 15;  // of RegionTarget kept in a player's capitals while they play
    uint32_t townPercent = 25;      // of a zone's arrivals placed at its towns and inns
    uint32_t inViewArrivalSeconds = 20;  // at most one arrival in a player's view per place this often
    uint32_t localCrowd = 4;  // arrivals avoid spots where this many characters already stand nearby
    uint32_t starterCapacity = 16;  // young characters (level 6 or below) a starting zone holds before arrivals are older
    float catchupMaximum = 1.5f;
};

struct Human
{
    Guid id = 0;
    uint32_t faction = 0;
    uint32_t map = 0;
    uint32_t zone = 0;
    uint32_t level = 1;
};

// A populated location derived from players' recent presence. Natural levels come from the
// zone's own level data; capitals (hubs) use a mixed spread up to the realm's level cap.
struct Place
{
    uint32_t faction = 0;
    uint32_t map = 0;
    uint32_t zone = 0;
    uint32_t minLevel = 1;
    uint32_t maxLevel = 80;
    std::vector<uint32_t> naturalLevels;  // weighted samples for newly planned residents
    uint32_t homeRaceMask = 0;            // races (bit race-1) that start here or whose capital it is
    uint32_t arrivalMinLevel = 0;         // lowest level for newly planned arrivals (0 = minLevel)
    bool hub = false;
    bool accepting = true;  // a player is here now; a lingering place only keeps its residents
    bool resident = false;  // kept only as a capital's standing population; served after players' places
    float weight = 1.0f;    // presence, 0..1
    std::vector<Human> humans;
};

struct Hub
{
    uint32_t zone = 0;
    uint32_t map = 0;
    uint32_t expansion = 0;  // first expansion in which the city exists
    float x = 0.0f;          // approximate centre, used to collect walkable city spots
    float y = 0.0f;
    float radius = 0.0f;
    uint32_t minLevel = 10;  // usual lowest level seen there
    uint32_t raceMask = 0;   // races whose capital it is (bit race-1); 0 for neutral cities
};

struct Retention
{
    Guid id = 0;
    Seconds lastSeen = 0;
    uint32_t score = 0;  // best familiarity with any player character
};

struct Character
{
    Guid id = 0;
    uint32_t faction = 0;
    uint32_t race = 0;
    uint32_t level = 1;
    uint32_t map = 0;
    uint32_t zone = 0;
    bool introduced = false;
    bool initialized = false;
    bool online = false;
    bool engaged = false;
    Seconds sessionStarted = 0;
    Seconds restUntil = 0;
    Seconds lastSeen = 0;
};

struct Familiarity
{
    uint32_t score = 0;
    uint32_t sightings = 0;
    uint32_t sessionSightings = 0;
    uint32_t interactions = 0;
    Seconds lastSeen = 0;
    Seconds lastSighting = 0;
    Seconds lastInteraction = 0;
    Seconds lastSession = 0;
};

struct WorldState
{
    Seconds offlineRemaining = 0;
    Seconds lastTick = 0;
    Seconds nextCreation = 0;
};

struct Assignment
{
    Guid bot = 0;
    Guid human = 0;  // Demand owner; this is not a gameplay master or knowledge grant.
    uint32_t map = 0;   // place served; 0/0 for baseline seats
    uint32_t zone = 0;
};

class Policy
{
public:
    static uint64_t Pair(Guid human, Guid bot);
    static uint32_t Mix(uint32_t value);
    static void Advance(WorldState& state, Settings const& settings, Seconds now, bool humansPresent);
    static bool Observe(Familiarity& value, Settings const& settings, Seconds now, Seconds humanSession,
                        bool meaningful);
    static float FamiliarityWeight(Familiarity const& value, Seconds now);
    static float ActivityFraction(Character const& bot, std::vector<Human> const& humans);
    static float Catchup(Character const& bot, Human const& human, Familiarity const& value, Settings const& settings);
    static uint32_t CreationCount(uint32_t total, bool humansPresent, WorldState const& state, Settings const& settings,
                                  Seconds now);
    static std::vector<Assignment> Select(std::vector<Character> const& bots, std::vector<Human> const& humans,
                                          std::vector<Place> const& places,
                                          std::unordered_map<uint64_t, Familiarity> const& familiarity,
                                          Settings const& settings, Seconds now);
    // Presence moves toward 1 while a player is in a place and toward 0 after they leave.
    static float AdvancePresence(float weight, bool present, Seconds elapsed, Settings const& settings);
    // Each character plays for its own length of session (half to one and a half times the
    // setting), so a wave of arrivals does not leave all at once.
    static Seconds SessionLength(Guid bot, Settings const& settings);
    // Online for a session that is not over, or offline and rested: someone who can be here now.
    static bool CanBePresent(Character const& bot, Settings const& settings, Seconds now);
    static uint32_t Quota(Place const& place, Settings const& settings);
    // Player levels that belong in this place; empty when every player is out of its range.
    static std::vector<uint32_t> Peers(Place const& place, Settings const& settings);
    static bool PeerSeat(Place const& place, uint32_t seat, Settings const& settings);
    static bool Fits(Place const& place, uint32_t level, bool peerSeat, Settings const& settings);
    // Below the starter level a character only belongs where its race starts or in its capital.
    static bool RaceFits(Place const& place, uint32_t race, uint32_t level);
    static constexpr uint32_t STARTER_LEVEL = 10;
    static uint32_t PlanLevel(Place const& place, bool peerSeat, uint32_t hash, uint32_t levelCap,
                              Settings const& settings);
    static std::vector<Hub> const& Hubs();
    static bool HubAvailable(Hub const& hub, uint32_t expansion, uint32_t levelCap);
    // Over the known limit, the earliest retention expiry goes first.
    static std::vector<Guid> CullOrder(std::vector<Retention> candidates, uint32_t count, Settings const& settings);
};
}  // namespace RealmPopulation
#endif
