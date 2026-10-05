// SPDX-License-Identifier: GPL-2.0-or-later
// PBC Character System changes, 2026-09-30; upstream attribution in NOTICE.md.

#include "pbc_native.h"
#include "Creature.h"
#include "DBCStores.h"
#include "Player.h"
#include "SmartScriptMgr.h"
#include <algorithm>
#include <array>
#include <limits>

namespace PBC
{
namespace
{
struct Animation
{
    char const* name;
    uint32_t textEmote;
};

constexpr std::array<Animation, 11> Animations = {{{"bow", TEXT_EMOTE_BOW},
                                                   {"cheer", TEXT_EMOTE_CHEER},
                                                   {"chuckle", TEXT_EMOTE_CHUCKLE},
                                                   {"cry", TEXT_EMOTE_CRY},
                                                   {"laugh", TEXT_EMOTE_LAUGH},
                                                   {"nod", TEXT_EMOTE_NOD},
                                                   {"point", TEXT_EMOTE_POINT},
                                                   {"salute", TEXT_EMOTE_SALUTE},
                                                   {"shrug", TEXT_EMOTE_SHRUG},
                                                   {"smile", TEXT_EMOTE_SMILE},
                                                   {"wave", TEXT_EMOTE_WAVE}}};

bool HasEmoteEvent(SmartAIEventList const& events, uint32_t textEmote)
{
    return std::any_of(
        events.begin(), events.end(), [textEmote](auto const& event)
        { return event.GetEventType() == SMART_EVENT_RECEIVE_EMOTE && event.event.emote.emote == textEmote; });
}
}  // namespace

bool EligibleNpc(Player* listener, Creature* creature, float range)
{
    if (!listener || !creature || !creature->IsInWorld() || !creature->IsAlive() || creature->IsInCombat() ||
        creature->IsPet() || creature->IsTotem() || creature->IsTrigger() || creature->IsControlledByPlayer() ||
        creature->IsHostileTo(listener) || !listener->IsWithinDistInMap(creature, range) ||
        !listener->CanSeeOrDetect(creature) || !listener->IsWithinLOSInMap(creature))
        return false;
    auto type = creature->GetCreatureTemplate()->type;
    return type != CREATURE_TYPE_BEAST && type != CREATURE_TYPE_CRITTER;
}

std::string RaceName(uint8_t race)
{
    auto entry = sChrRacesStore.LookupEntry(race);
    return entry && entry->name[LOCALE_enUS] ? entry->name[LOCALE_enUS] : "Unknown race";
}

std::string ClassName(uint8_t characterClass)
{
    auto entry = sChrClassesStore.LookupEntry(characterClass);
    return entry && entry->name[LOCALE_enUS] ? entry->name[LOCALE_enUS] : "Unknown class";
}

std::string NativeEmoteName(uint32_t textEmote)
{
    for (auto const& animation : Animations)
        if (animation.textEmote == textEmote)
            return animation.name;
    // Names here describe native client gestures, not an inferred game outcome.
    switch (textEmote)
    {
        case TEXT_EMOTE_DANCE:
            return "dance";
        case TEXT_EMOTE_KNEEL:
            return "kneel";
        case TEXT_EMOTE_KISS:
            return "kiss";
        case TEXT_EMOTE_RUDE:
            return "rude gesture";
        case TEXT_EMOTE_BEG:
            return "beg";
        case TEXT_EMOTE_APPLAUD:
            return "applaud";
        case TEXT_EMOTE_THANK:
            return "thank";
        default:
            return "makes a gesture";
    }
}

bool HasNativeEmoteScript(Creature* creature, uint32_t textEmote)
{
    if (!creature)
        return false;
    auto definition = creature->GetCreatureTemplate();
    // A C++ AI may handle arbitrary gestures. Keep its existing response
    // authoritative.
    if (definition->ScriptID)
        return true;
    if (definition->AIName != "SmartAI" && definition->AIName != "SmartGuardAI")
        return false;
    auto entryEvents =
        sSmartScriptMgr->GetScript(static_cast<int32_t>(creature->GetEntry()), SMART_SCRIPT_TYPE_CREATURE);
    auto spawn = creature->GetSpawnId();
    if (!spawn || spawn > static_cast<uint32_t>(std::numeric_limits<int32_t>::max()))
        return HasEmoteEvent(entryEvents, textEmote);
    auto spawnEvents = sSmartScriptMgr->GetScript(-static_cast<int32_t>(spawn), SMART_SCRIPT_TYPE_CREATURE);
    if (spawnEvents.empty())
        return HasEmoteEvent(entryEvents, textEmote);
    // Mirror the core's selection of spawn scripts and optional template
    // supplementation.
    return HasEmoteEvent(spawnEvents, textEmote) ||
           (definition->HasFlagsExtra(CREATURE_FLAG_EXTRA_DONT_OVERRIDE_ENTRY_SAI) &&
            HasEmoteEvent(entryEvents, textEmote));
}

void PlayNativeAnimation(Unit* unit, std::string const& animation)
{
    if (!unit || !unit->IsAlive())
        return;
    for (auto const& candidate : Animations)
        if (animation == candidate.name)
        {
            auto text = sEmotesTextStore.LookupEntry(candidate.textEmote);
            // Only one-shot gestures; never leave a persistent dance/sit state
            // behind.
            if (text && text->textid != EMOTE_ONESHOT_NONE)
                unit->HandleEmoteCommand(text->textid);
            return;
        }
}

std::set<std::string> NativeAnimations()
{
    std::set<std::string> names;
    for (auto const& candidate : Animations)
        names.insert(candidate.name);
    return names;
}
}  // namespace PBC
