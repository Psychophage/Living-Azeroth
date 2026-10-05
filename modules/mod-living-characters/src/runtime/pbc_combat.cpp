// PBC Character System changes, 2026-09-30; see NOTICE.md for upstream attribution.
// Living Azeroth: actual encounter participants share a short public exchange.
#include "pbc_game.h"
#include "pbc_json.h"
#include "Creature.h"
#include "Group.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotDialogue.h"
#include "Playerbots.h"
#include "World.h"
#include <algorithm>

namespace PBC
{
namespace
{
bool Present(Unit* left, Unit* right, float range = 40.0f)
{
    return left && right && left->IsInWorld() && right->IsInWorld() &&
        left->IsAlive() && right->IsAlive() && left->IsInMap(right) && left->InSamePhase(right) &&
        left->IsWithinDistInMap(right, range) && left->CanSeeOrDetect(right) && left->IsWithinLOSInMap(right);
}

bool Companion(Player* human, Player* player)
{
    auto ai = player ? GET_PLAYERBOT_AI(player) : nullptr;
    return ai && player->IsFriendlyTo(human) && (ai->GetMaster() == human ||
        (human->GetGroup() && human->GetGroup() == player->GetGroup()));
}

bool Encounter(Player* human, Unit* enemy)
{
    auto creature = enemy ? enemy->ToCreature() : nullptr;
    if (!HasHumanConnection(human) || !creature || creature->IsControlledByPlayer() ||
        !Present(human, enemy) || !enemy->IsHostileTo(human))
        return false;
    if (enemy->GetCombatManager().IsInCombatWith(human))
        return true;
    if (auto group = human->GetGroup())
        for (auto member = group->GetFirstMember(); member; member = member->next())
            if (auto player = member->GetSource(); Present(human, player) &&
                enemy->GetCombatManager().IsInCombatWith(player))
                return true;
    return false;
}
}

Unit* ResolveCombatEnemy(GameAudience const& audience)
{
    auto human = ObjectAccessor::FindPlayer(audience.anchor);
    auto enemy = human ? ObjectAccessor::GetUnit(*human, ObjectGuid(audience.combatEnemy.guid)) : nullptr;
    return enemy && PlayerbotDialogueBridge::Matches(enemy, audience.combatEnemy) ? enemy : nullptr;
}

GameAudience CaptureCombatAudience(Player* anchor, Unit* enemy, NpcDefinitions const& definitions,
    std::string const& realmPhase, Unit* localSource)
{
    if (!Encounter(anchor, enemy))
        return {};
    auto source = localSource ? localSource : anchor;
    auto audience = CaptureAudience(anchor, CHAT_MSG_SAY, nullptr, nullptr, definitions, realmPhase, source);
    audience.combat = true;
    audience.combatEnemy = PlayerbotDialogueBridge::Snapshot(enemy);
    float range = sWorld->getFloatConfig(CONFIG_LISTEN_RANGE_SAY);
    if (Present(source, enemy, range) && std::none_of(audience.actors.begin(), audience.actors.end(),
        [&](auto const& actor) { return actor.guid == enemy->GetGUID(); }))
        audience.actors.push_back(SnapshotActor(enemy, definitions, realmPhase));
    for (auto& actor : audience.actors)
    {
        auto unit = ResolveActor(actor);
        actor.canSpeak = !actor.human && Present(anchor, unit) &&
            (unit == enemy || Companion(anchor, unit ? unit->ToPlayer() : nullptr));
        auto facts = pbc_json::parse(actor.factsJson);
        facts["combat_dialogue"] = {{"enemy_id", SnapshotActor(enemy, definitions, realmPhase).identity.id},
            {"enemy_name", enemy->GetName()}, {"enemy_level", enemy->GetLevel()},
            {"speaker_is_enemy", unit == enemy}, {"encounter_ongoing", true}};
        actor.factsJson = facts.dump();
    }
    return audience;
}

bool CombatSpeechQuiet(GameAudience const& audience)
{
    for (auto const& actor : audience.actors)
        if (auto unit = ResolveActor(actor); unit && unit->HasRecentScriptedSpeech())
            return false;
    return true;
}

bool CombatAudienceValid(GameAudience const& audience, GameActor const& speaker)
{
    auto human = ObjectAccessor::FindPlayer(audience.anchor);
    auto enemy = ResolveCombatEnemy(audience);
    auto unit = ResolveActor(speaker);
    return Encounter(human, enemy) && Present(human, unit,
        sWorld->getFloatConfig(CONFIG_LISTEN_RANGE_SAY)) && CombatSpeechQuiet(audience) &&
        (unit == enemy || Companion(human, unit ? unit->ToPlayer() : nullptr));
}
}
