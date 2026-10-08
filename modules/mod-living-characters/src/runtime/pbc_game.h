// PBC Character System changes, 2026-09-30; see NOTICE.md for upstream attribution.
// Living Azeroth adapter. Every function runs on the world thread.
#ifndef PBC_GAME_H
#define PBC_GAME_H

#include "pbc_store.h"
#include "pbc_knowledge.h"
#include "pbc_dialogue.h"
#include "ObjectGuid.h"
#include "PlayerbotDialogueTypes.h"
#include <map>
#include <set>
#include <tuple>

class Channel;
class Player;
class Unit;

namespace PBC
{
struct GameActor
{
    ActorRecord identity;
    ObjectGuid guid;
    uint32_t spawnId = 0;
    uint32_t mapId = 0;
    uint32_t instanceId = 0;
    uint32_t zoneId = 0;
    std::string watchId;
    std::string factsJson;
    std::vector<InformationGroup> informationGroups;
    bool human = false;
    bool canSpeak = false;
    bool addressed = false;
};

struct GameAudience
{
    ObjectGuid anchor;
    ObjectGuid whisperTarget;
    ObjectGuid group;
    uint8_t subgroup = 0;
    uint32_t guildId = 0;
    uint32_t language = 0;
    uint32_t chatType = 0;
    std::string channelName;
    std::string label;
    std::vector<GameActor> actors;
    bool combat = false;
    bool duringCombat = false; // Explicit human conversation may include fighting companions.
    PlayerbotDialogue::Entity combatEnemy;
};

struct NpcDefinition
{
    std::string actorId;
    std::string canon;
    std::string watchId;
};

// Explicit spawn mappings; no inference that identical display names are one person.
using NpcDefinitions = std::map<std::tuple<uint32_t, uint32_t, uint32_t>, NpcDefinition>;

bool HasHumanConnection(Player* player);
// The English name of a zone or area, or "Unknown place".
std::string ZoneName(uint32_t zone);
bool Understands(GameActor const& actor, uint32_t language);
uint32_t SpokenLanguage(GameActor const& actor);
void FilterLanguage(GameAudience& audience, uint32_t language);
GameActor SnapshotActor(Unit* unit, NpcDefinitions const& definitions, std::string const& realmPhase);
GameAudience CaptureAudience(Player* anchor, uint32_t chatType, Player* whisperTarget,
    Channel* channel, NpcDefinitions const& definitions, std::string const& realmPhase, Unit* localSource = nullptr, bool sharedLanguage = false);
// Combat has its own eligibility: an actual hostile creature and nearby companions.
GameAudience CaptureCombatAudience(Player* anchor, Unit* enemy, NpcDefinitions const& definitions,
    std::string const& realmPhase, Unit* localSource = nullptr);
bool CombatAudienceValid(GameAudience const& audience, GameActor const& speaker);
Unit* ResolveCombatEnemy(GameAudience const& audience);
bool CombatSpeechQuiet(GameAudience const& audience);
Unit* ResolveActor(GameActor const& actor);
bool AudienceStillValid(GameAudience const& audience, GameActor const& speaker);
bool DeliverSegment(GameAudience const& audience, GameActor const& speaker, Segment const& segment);
std::set<std::string> NativeAnimations();
}

#endif
