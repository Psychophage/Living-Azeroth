// SPDX-License-Identifier: GPL-2.0-or-later
#include "pbc_runtime_internal.h"
#include "Creature.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QuestDef.h"
#include "SpellAuraEffects.h"
#include "World.h"
#include <algorithm>

namespace PBC
{
uint32_t SpokenLanguage(GameActor const& actor)
{
    auto facts = pbc_json::parse(actor.factsJson, nullptr, false);
    return facts.is_object() ? facts.value("spoken_language", uint32_t{LANG_UNIVERSAL}) : LANG_UNIVERSAL;
}

bool Understands(GameActor const& actor, uint32_t language)
{
    if (language == LANG_UNIVERSAL)
        return true;
    auto unit = ResolveActor(actor);
    if (!unit)
        return false;
    if (auto player = unit->ToPlayer())
    {
        if (player->IsGameMaster() || player->HasAuraType(SPELL_AURA_COMPREHEND_LANGUAGE))
            return true;
        auto descriptor = GetLanguageDescByID(language);
        if (descriptor && (!descriptor->skill_id || player->HasSkill(descriptor->skill_id)))
            return true;
        for (auto effect : player->GetAuraEffectsByType(SPELL_AURA_MOD_LANGUAGE))
            if (effect->GetMiscValue() == static_cast<int32_t>(language))
                return true;
        return false;
    }
    // Explicit catalogue languages constrain NPC knowledge. Without a researched
    // language record retain native universal monster-chat compatibility.
    auto spoken = SpokenLanguage(actor);
    return spoken == LANG_UNIVERSAL || spoken == language;
}

void FilterLanguage(GameAudience& audience, uint32_t language)
{
    if (sWorld->getBoolConfig(CONFIG_ALLOW_TWO_SIDE_INTERACTION_CHAT) ||
        (audience.label == "guild" && sWorld->getBoolConfig(CONFIG_ALLOW_TWO_SIDE_INTERACTION_GUILD)) ||
        ((audience.label == "party" || audience.label == "raid") &&
         sWorld->getBoolConfig(CONFIG_ALLOW_TWO_SIDE_INTERACTION_GROUP)))
        language = LANG_UNIVERSAL;
    audience.language = language;
    std::erase_if(audience.actors, [&](auto const& actor) { return !Understands(actor, language); });
}

namespace RuntimeDetail
{
void Runtime::Enrich(GameAudience& audience) const
{
    auto human = ObjectAccessor::FindPlayer(audience.anchor);
    for (auto& actor : audience.actors)
    {
        auto unit = ResolveActor(actor);
        if (!unit)
            continue;
        auto fresh = SnapshotActor(unit, _definitions, _realmPhase);
        auto facts = pbc_json::parse(actor.factsJson);
        for (auto key : {"guild_id", "guild_rank", "guild_name", "spoken_language"})
            facts.erase(key);
        facts.update(pbc_json::parse(fresh.factsJson));
        actor.informationGroups = fresh.informationGroups;
        auto configured = _knowledge.Groups(facts);
        actor.informationGroups.insert(actor.informationGroups.end(), configured.begin(), configured.end());
        if (!actor.watchId.empty() && std::none_of(actor.informationGroups.begin(), actor.informationGroups.end(),
            [&](auto const& group) { return group.id == actor.watchId; }))
            actor.informationGroups.push_back({actor.watchId, "Local watch", "Local guard reports.", 259200000, true});
        facts["information_groups"] = pbc_json::array();
        std::set<std::string> groups;
        for (auto const& group : actor.informationGroups)
        {
            groups.insert(group.id);
            facts["information_groups"].push_back({{"id", group.id}, {"name", group.name},
                                                  {"description", group.description}});
        }
        // Only the NPC currently interacting with this player sees this overlay.
        // It is never attached to another character's biography or public event.
        facts.erase("interaction_quests");
        facts.erase("interaction_player");
        if (auto creature = unit->ToCreature())
        {
            auto entry = creature->GetEntry();
            if (auto language = _knowledge.Language(entry); language && GetLanguageDescByID(*language))
                facts["spoken_language"] = *language;
            if (human && human->IsInWorld() && human->IsInMap(creature) &&
                human->InSamePhase(creature) && human->IsWithinDistInMap(creature, 45.0f))
            {
                for (auto quest : _knowledge.QuestIds(entry))
                {
                    auto status = human->GetQuestStatus(quest);
                    std::string state = human->GetQuestRewardStatus(quest) ? "rewarded" :
                        status == QUEST_STATUS_COMPLETE ? "complete" :
                        status == QUEST_STATUS_INCOMPLETE ? "incomplete" :
                        status == QUEST_STATUS_FAILED ? "failed" : "none";
                    facts["interaction_quests"][std::to_string(quest)] = state;
                }
                if (facts.contains("interaction_quests"))
                    facts["interaction_player"] = SnapshotActor(human, _definitions, _realmPhase).identity.id;
            }
        }
        auto knowledge = _knowledge.Select(facts, groups, _knowledgeBytes);
        facts["knowledge"] = std::move(knowledge.records);
        facts["knowledge_bytes"] = knowledge.bytes;
        facts["knowledge_omitted"] = knowledge.omitted.size();
        actor.factsJson = facts.dump();
    }
}

std::string Runtime::KnowledgeState(GameActor const& actor, ObjectGuid anchor) const
{
    GameAudience current;
    current.anchor = anchor;
    current.actors = {actor};
    Enrich(current);
    auto facts = pbc_json::parse(current.actors.front().factsJson);
    pbc_json state = pbc_json::object();
    for (auto key : {"map_id", "zone_id", "area_id", "guild_id", "guild_rank", "information_groups",
                     "interaction_player", "interaction_quests", "spoken_language"})
        state[key] = facts.value(key, pbc_json{});
    return state.dump();
}
}
}
