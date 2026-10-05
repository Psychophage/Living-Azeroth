/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */
// Living Azeroth: execute the production policy with synthetic time, no server or API required.
#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <unordered_set>

#include "PopulationPolicy.h"

using namespace RealmPopulation;

static unsigned checks = 0;

// Each player's own place, ranged around their level: the behaviour before zone levels existed.
static std::vector<Place> PlacesFor(std::vector<Human> const& humans)
{
    std::vector<Place> places;
    for (auto const& human : humans)
        if (human.zone)
        {
            Place place;
            place.faction = human.faction;
            place.map = human.map;
            place.zone = human.zone;
            place.minLevel = human.level > 5 ? human.level - 5 : 1;
            place.maxLevel = human.level + 5;
            place.naturalLevels = {human.level};
            place.humans = {human};
            places.push_back(place);
        }
    return places;
}
static void Check(bool condition, char const* message)
{
    ++checks;
    if (!condition)
        throw std::runtime_error(message);
}

int main()
{
    Settings settings;
    WorldState world;
    Check(Policy::CreationCount(0, false, world, settings, 1) == 5, "fresh creation must be bounded");
    Check(Policy::CreationCount(40, false, world, settings, 1) == 0, "empty realm does not fill reserve");
    Check(Policy::CreationCount(40, true, world, settings, 1) == 5, "human demand grows reserve gradually");
    Check(Policy::CreationCount(2500, true, world, settings, 1) == 0, "retained identities may exceed reserve");
    world.nextCreation = 100;
    Check(Policy::CreationCount(0, true, world, settings, 99) == 0, "restarts retain creation cooldown");
    for (Seconds now = 100; now <= 1200; now += 10)
        Policy::Advance(world, settings, now, true);
    Check(world.offlineRemaining == settings.offlineAllowance, "offline allowance is capped");
    for (Seconds now = 1210; now <= 1600; now += 10)
        Policy::Advance(world, settings, now, false);
    auto beforeRestart = world.offlineRemaining;
    world.lastTick = 1700;  // Same persisted restoration as the runtime.
    Policy::Advance(world, settings, 1710, false);
    Check(world.offlineRemaining == beforeRestart - 10, "restart cannot refill offline activity");
    for (Seconds now = 1720; now <= 2800; now += 10)
        Policy::Advance(world, settings, now, false);
    Check(world.offlineRemaining == 0, "offline activity eventually stops");

    Familiarity familiarity;
    Policy::Observe(familiarity, settings, 100, 0, false);
    for (Seconds now = 101; now < 1800; ++now)
        Policy::Observe(familiarity, settings, now, 0, false);
    Check(familiarity.score == 1, "continuous proximity cannot inflate score each tick");
    Policy::Observe(familiarity, settings, 2000, 0, false);
    Policy::Observe(familiarity, settings, 4000, 0, false);
    Policy::Observe(familiarity, settings, 6000, 0, false);
    Check(familiarity.score == 3, "one encounter window has a sighting cap");
    Policy::Observe(familiarity, settings, 21601, 1, false);
    Check(familiarity.score == 4, "separate encounter windows build recognition");
    Policy::Observe(familiarity, settings, 21602, 1, true);
    Check(familiarity.score == 8, "meaningful interaction increases familiarity");
    Policy::Observe(familiarity, settings, 21603, 1, true);
    Check(familiarity.score == 8, "interaction spam is bounded");
    float oldWeight = Policy::FamiliarityWeight(familiarity, 21603 + 86400 * 365);
    Check(oldWeight >= 4.0f && familiarity.score == 8, "absence affects availability, never history");

    std::vector<Human> humans;
    for (uint32_t i = 0; i < 8; ++i)
        humans.push_back({10000 + i, i % 2, i % 2, 100 + i, 5 + i * 10});
    std::vector<Character> bots;
    for (uint32_t i = 0; i < 2000; ++i)
    {
        Character bot;
        bot.id = i + 1;
        bot.faction = i % 2;
        bot.level = i < 1600 ? 5 + (i % 8) * 10 : 1 + (i / 2) % 80;
        bot.initialized = true;
        bot.map = i % 2;
        // Half live in the players' zones; the rest of the realm is elsewhere.
        bot.zone = (i < 1000 ? 100 : 200) + i % 8;
        bots.push_back(bot);
    }
    std::unordered_map<uint64_t, Familiarity> relations;
    relations[Policy::Pair(humans.front().id, bots.front().id)] = familiarity;
    auto selected = Policy::Select(bots, humans, PlacesFor(humans), relations, settings, 10000);
    Check(selected.size() == 500, "online target enforced with eight players");
    std::unordered_set<Guid> unique;
    uint32_t minimum = 500, maximum = 0;
    for (auto const& human : humans)
    {
        uint32_t count =
            std::count_if(selected.begin(), selected.end(), [&](Assignment const& a) { return a.human == human.id; });
        minimum = std::min(minimum, count);
        maximum = std::max(maximum, count);
    }
    Check(minimum >= 40 && maximum - minimum <= 1, "separate regions receive fair seats");
    for (auto const& assignment : selected)
        unique.insert(assignment.bot);
    Check(unique.size() == selected.size(), "one bot is never assigned twice");
    auto oneRegion = Policy::Select(bots, {humans.front()}, PlacesFor({humans.front()}), relations, settings, 10000);
    Check(std::count_if(oneRegion.begin(), oneRegion.end(), [](Assignment const& a) { return a.human != 0; }) <=
              settings.regionTarget,
          "one human cannot claim every regional seat");
    Character unfamiliar;
    unfamiliar.id = 9000;
    unfamiliar.level = 80;
    auto mismatched = Policy::Select({unfamiliar}, {humans.front()}, PlacesFor({humans.front()}), {}, settings, 10000);
    Check(mismatched.size() == 1 && mismatched.front().human == 0,
          "uninitialized veteran cannot count as a beginner before explicit reserve adaptation");
    auto repeat = Policy::Select(bots, humans, PlacesFor(humans), relations, settings, 10000);
    Check(std::equal(selected.begin(), selected.end(), repeat.begin(),
                     [](Assignment const& a, Assignment const& b) { return a.bot == b.bot && a.human == b.human; }),
          "unchanged input gives stable selection");

    Settings socialSettings = settings;
    socialSettings.onlineTarget = socialSettings.regionTarget = 20;
    socialSettings.regionalPercent = 100;
    std::vector<Character> neighbours;
    std::unordered_map<uint64_t, Familiarity> acquaintances;
    for (uint32_t i = 0; i < 100; ++i)
    {
        auto bot = bots.front();
        bot.id = i + 1;
        bot.introduced = i < 80;
        neighbours.push_back(bot);
        if (i < 60)
            acquaintances[Policy::Pair(humans.front().id, bot.id)] = familiarity;
    }
    auto reunion = Policy::Select(neighbours, {humans.front()}, PlacesFor({humans.front()}), acquaintances, socialSettings, 10000);
    uint32_t known = 0, strangers = 0, newcomers = 0;
    for (auto const& choice : reunion)
    {
        known += choice.bot <= 60;
        strangers += choice.bot > 60 && choice.bot <= 80;
        newcomers += choice.bot > 80;
    }
    Check(known && strangers && newcomers && known < reunion.size(),
          "reunions include acquaintances, existing strangers and new faces");
    auto alt = humans.front();
    ++alt.id;
    auto altWithHistory = Policy::Select(neighbours, {alt}, PlacesFor({alt}), acquaintances, socialSettings, 10000);
    auto altWithoutHistory = Policy::Select(neighbours, {alt}, PlacesFor({alt}), {}, socialSettings, 10000);
    Check(std::equal(altWithHistory.begin(), altWithHistory.end(), altWithoutHistory.begin(),
                     [](Assignment const& a, Assignment const& b) { return a.bot == b.bot; }),
          "another human character's history does not affect an alt's selection");

    auto instanceHuman = humans.front();
    instanceHuman.zone = 0;
    auto instancePopulation = Policy::Select(bots, {instanceHuman}, PlacesFor({instanceHuman}), relations, settings, 10000);
    Check(instancePopulation.size() == settings.onlineTarget &&
              std::all_of(instancePopulation.begin(), instancePopulation.end(),
                          [](Assignment const& a) { return a.human == 0; }),
          "humans in private instances keep the global population without spawning local strangers");

    auto protectedRoster = bots;
    for (auto& bot : protectedRoster)
        if (bot.zone == humans.front().zone)
        {
            bot.online = true;
            bot.engaged = true;
        }
    auto crowded = Policy::Select(protectedRoster, humans, PlacesFor(humans), relations, settings, 10000);
    Check(std::any_of(crowded.begin(), crowded.end(), [&](Assignment const& a) { return a.human == humans.back().id; }),
          "protected crowd in one region does not consume every other region's allocation");
    auto occupied = bots;
    uint32_t protectedPerRegion[8]{};
    for (auto& bot : occupied)
    {
        uint32_t region = bot.zone - 100;
        if (region < 7 && protectedPerRegion[region]++ < 50)
            bot.online = bot.engaged = true;
    }
    auto lateRegion = Policy::Select(occupied, humans, PlacesFor(humans), relations, settings, 10000);
    Check(std::count_if(lateRegion.begin(), lateRegion.end(),
                        [&](Assignment const& a) { return a.human == humans.back().id; }) >= 40,
          "seven protected regional crowds cannot consume the eighth region's fair allocation");
    auto distant = bots.front();
    distant.zone = 9999;
    auto remoteSelection = Policy::Select({distant}, {humans.front()}, PlacesFor({humans.front()}), relations, settings, 10000);
    Check(remoteSelection.size() == 1 && remoteSelection.front().human == 0,
          "distant established character cannot satisfy a local population seat");
    WorldState stalled{100, 100, 0};
    Policy::Advance(stalled, settings, 1000, false);
    Check(stalled.offlineRemaining == 0, "long stalls consume elapsed offline allowance");

    bots.front().online = true;
    bots.front().engaged = true;
    bots.front().restUntil = 999999;
    settings.onlineTarget = 1;
    selected = Policy::Select(bots, humans, PlacesFor(humans), relations, settings, 10000);
    Check(selected.size() == 1 && selected.front().bot == 1, "engaged character outranks quota and rest");
    bots[1].online = true;
    bots[1].engaged = true;
    selected = Policy::Select(bots, humans, PlacesFor(humans), relations, settings, 10000);
    Check(selected.size() == 2, "lowering capacity never interrupts existing commitments");
    selected = Policy::Select(bots, {}, PlacesFor({}), relations, settings, 10000);
    Check(selected.size() == settings.offlineOnline, "empty world has smaller active population");
    Check(Policy::ActivityFraction(bots[4], humans) > Policy::ActivityFraction(bots[4], {}),
          "nearby levels get more autonomous opportunities");
    Character companion;
    companion.level = 1;
    Human partner{1, 0, 0, 0, 30};
    Check(Policy::Catchup(companion, partner, familiarity, settings) == settings.catchupMaximum,
          "familiar companion catchup is capped");
    Check(Policy::Catchup(companion, partner, {}, settings) == 1.0f, "strangers receive no relationship catchup");
    partner.level = 1;
    Check(Policy::Catchup(companion, partner, familiarity, settings) == 1.0f,
          "catchup stops when companion catches up");

    settings.onlineTarget = 500;

    // Places are populated at their own level. A level-20 player in a 40-50 zone sees zone-level
    // characters and no peers; a level-45 player there also gets peers.
    Settings zoneSettings;
    Place tanaris;
    tanaris.faction = 1;
    tanaris.map = 1;
    tanaris.zone = 440;
    tanaris.minLevel = 40;
    tanaris.maxLevel = 50;
    tanaris.naturalLevels = {40, 44, 45, 45, 48, 50};
    Human visitor{20001, 1, 1, 440, 20};
    tanaris.humans = {visitor};
    std::vector<Character> mixed;
    for (uint32_t i = 0; i < 400; ++i)
    {
        Character bot;
        bot.id = 30000 + i;
        bot.faction = 1;
        bot.level = 1 + i % 80;
        mixed.push_back(bot);
    }
    auto inZone = Policy::Select(mixed, {visitor}, {tanaris}, {}, zoneSettings, 10000);
    uint32_t local = 0, outside = 0, peers = 0;
    for (auto const& a : inZone)
        if (a.zone == 440)
        {
            uint32_t level = 1 + (a.bot - 30000) % 80;
            ++local;
            outside += level < 40 || level > 50;
            peers += level >= 15 && level <= 25;
        }
    Check(local == zoneSettings.regionTarget && !outside && !peers, "a zone above the player keeps its own level");
    Check(Policy::Peers(tanaris, zoneSettings).empty() && !Policy::PeerSeat(tanaris, 0, zoneSettings),
          "an out-of-range player gets no peer seats");
    tanaris.humans.front().level = 45;
    auto belonging = Policy::Select(mixed, {tanaris.humans.front()}, {tanaris}, {}, zoneSettings, 10000);
    uint32_t near = 0;
    for (auto const& a : belonging)
        near += a.zone == 440 && std::abs(int(1 + (a.bot - 30000) % 80) - 45) <= 5;
    Check(near == zoneSettings.regionTarget, "a player who belongs in a zone gets zone-level and peer seats");
    for (uint32_t hash = 0; hash < 200; ++hash)
    {
        uint32_t level = Policy::PlanLevel(tanaris, false, hash, 80, zoneSettings);
        Check(level >= 40 && level <= 50, "planned residents stay within the zone's levels");
    }
    Check(Policy::PlanLevel(tanaris, false, 3, 42, zoneSettings) <= 42, "planned levels respect the realm's level cap");

    // A place its player left keeps only residents who are still there, and fades with presence.
    Place left = tanaris;
    left.accepting = false;
    left.weight = 0.5f;
    std::vector<Character> residents = mixed;
    for (uint32_t i = 0; i < 10; ++i)
    {
        residents[44 + i].online = true;  // levels 45..54 in the zone
        residents[44 + i].map = 1;
        residents[44 + i].zone = 440;
        residents[44 + i].initialized = true;
        residents[44 + i].sessionStarted = 9000;
    }
    auto lingering = Policy::Select(residents, {visitor}, {left}, {}, zoneSettings, 10000);
    uint32_t kept = 0, admitted = 0;
    for (auto const& a : lingering)
        if (a.zone == 440)
            (residents[a.bot - 30000].online ? kept : admitted)++;
    Check(kept && !admitted && kept <= Policy::Quota(left, zoneSettings), "a lingering place admits nobody new");
    float weight = 0.0f;
    for (int i = 0; i < 3; ++i)
        weight = Policy::AdvancePresence(weight, true, 10, zoneSettings);
    Check(weight > 0.0f && weight < 1.0f, "a place fills in gradually");
    weight = Policy::AdvancePresence(1.0f, false, zoneSettings.lingerSeconds / 2, zoneSettings);
    Check(weight > 0.4f && weight < 0.6f, "a place empties gradually after the player leaves");
    Check(Policy::AdvancePresence(weight, true, 600, zoneSettings) == 1.0f, "returning restores a place");

    // A full starting zone: arrivals start older, while people already there still belong.
    Place starter;
    starter.minLevel = 2;
    starter.maxLevel = 10;
    starter.naturalLevels = {5, 6, 8, 10};
    starter.arrivalMinLevel = 7;
    starter.humans = {Human{30001, 1, 530, 3430, 3}};
    for (uint32_t hash = 0; hash < 100; ++hash)
    {
        Check(Policy::PlanLevel(starter, false, hash, 80, zoneSettings) >= 7, "a full starting zone gets older arrivals");
        Check(Policy::PlanLevel(starter, true, hash, 80, zoneSettings) >= 7, "including its peer arrivals");
    }
    Check(Policy::Fits(starter, 4, false, zoneSettings), "young people already there still belong");

    // Sessions vary per character, so one wave of arrivals does not leave together.
    Seconds shortest = settings.sessionSeconds * 2, longest = 0;
    for (Guid id = 1; id <= 200; ++id)
    {
        shortest = std::min(shortest, Policy::SessionLength(id, settings));
        longest = std::max(longest, Policy::SessionLength(id, settings));
    }
    Check(shortest >= settings.sessionSeconds / 2 && longest <= settings.sessionSeconds * 3 / 2 &&
              longest - shortest > settings.sessionSeconds / 2,
          "session lengths spread between half and one and a half times the setting");
    Character resting;
    resting.restUntil = 20000;
    Check(!Policy::CanBePresent(resting, settings, 10000) && Policy::CanBePresent(resting, settings, 20000),
          "a resting character is not counted as present");

    // A place a player is in keeps its own size: baseline seats do not top it up.
    Settings capped = zoneSettings;
    capped.regionTarget = 5;
    std::vector<Character> crowd;
    for (uint32_t i = 0; i < 30; ++i)
    {
        Character bot;
        bot.id = 50000 + i;
        bot.faction = 1;
        bot.level = 45;
        bot.map = 1;
        bot.zone = 440;
        bot.initialized = true;
        crowd.push_back(bot);
    }
    Place busy = tanaris;
    busy.humans = {Human{20001, 1, 1, 440, 45}};
    auto topped = Policy::Select(crowd, busy.humans, {busy}, {}, capped, 10000);
    Check(topped.size() == capped.regionTarget, "baseline seats never add people to a place a player is in");

    // Capitals follow the realm phase.
    uint32_t vanillaHubs = 0, wrathHubs = 0;
    for (auto const& hub : Policy::Hubs())
    {
        vanillaHubs += Policy::HubAvailable(hub, 0, 60);
        wrathHubs += Policy::HubAvailable(hub, 2, 80);
    }
    uint32_t burningHubs = 0;
    for (auto const& hub : Policy::Hubs())
        burningHubs += Policy::HubAvailable(hub, 1, 70);
    Check(vanillaHubs == 6 && burningHubs == 9 && wrathHubs == 10,
          "the Exodar, Silvermoon and Shattrath open with The Burning Crusade, Dalaran with Wrath");

    // Below level 10 a character belongs where its race starts or in its capital.
    Place elwynn;
    elwynn.homeRaceMask = 1u << 0;  // Human
    Check(Policy::RaceFits(elwynn, 1, 3) && !Policy::RaceFits(elwynn, 4, 3) && Policy::RaceFits(elwynn, 4, 15),
          "a level-3 Night Elf is not in Elwynn, a level-15 one may be");

    // A capital's peers stay within its range: a level-1 visitor does not attract level-1 strangers.
    Place capital;
    capital.hub = true;
    capital.minLevel = 10;
    capital.maxLevel = 80;
    capital.humans = {Human{40001, 0, 0, 1519, 1}};
    for (uint32_t hash = 0; hash < 100; ++hash)
        Check(Policy::PlanLevel(capital, true, hash, 80, zoneSettings) >= 10, "capital peers respect its lowest level");
    capital.humans.front().level = 40;
    Check(!Policy::PeerSeat(capital, 0, zoneSettings), "capitals spread levels evenly instead of favouring peers");

    // Culling removes the least valued first: a talked-to character outlives a passing sighting.
    std::vector<Retention> retained = {{1, 1000, 1}, {2, 1000, 9}, {3, 500, 0}, {4, 400000, 1}};
    auto order = Policy::CullOrder(retained, 2, zoneSettings);
    Check(order.size() == 2 && order[0] == 3 && order[1] == 1, "culling starts with the least protected");

    auto start = std::chrono::steady_clock::now();
    for (unsigned i = 0; i < 20; ++i)
        Policy::Select(bots, humans, PlacesFor(humans), relations, settings, 10000 + i);
    auto micros =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
    std::cout << checks << " checks passed; 500/2000 selection with eight humans: " << micros / 20 << " us/selection\n";
}
