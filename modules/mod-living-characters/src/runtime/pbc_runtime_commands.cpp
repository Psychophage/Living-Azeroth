#include <algorithm>
#include <cctype>
#include <cstdio>
// SPDX-License-Identifier: GPL-2.0-or-later
// PBC Character System changes, 2026-09-30; upstream attribution in NOTICE.md.

#include "Chat.h"
#include "Creature.h"
#include "Group.h"
#include "Formations.h"
#include "Player.h"
#include "PlayerbotDialogue.h"
#include "RBAC.h"
#include "WorldSession.h"
#include "pbc_json.h"
#include "pbc_log.h"
#include "pbc_runtime.h"
#include "Playerbots.h"
#include "PlayerbotFactory.h"
#include "DatabaseEnv.h"
#include "pbc_runtime_internal.h"
#include <charconv>
#include <sstream>

namespace PBC::RuntimeDetail
{
bool Runtime::Command(Player* player, std::string const& command)
{
    if (!player || !player->GetSession() || player->GetSession()->IsBot())
        return false;
    std::istringstream input(command);
    std::string verb;
    input >> verb;
    bool watch = verb == "watch";
    bool groupCommand = verb == "group";
    std::string groupId;
    if (watch)
        input >> verb;
    else if (groupCommand)
        input >> groupId >> verb;
    auto tell = [&](std::string const& message) { ChatHandler(player->GetSession()).SendSysMessage(message); };
    if (verb == "help" || verb.empty())
    {
        tell(
            "Select a character, then use .chars info, history or notes. "
            "Owner edits: .chars fact <character-version> <text>; "
            ".chars edit <note-id>:<version> <text>; .chars resolve "
            "<note-id>:<version>. "
            "Source text: .chars episode <id:version>. GM: .chars usage, revise "
            "<id:version> <text>, "
            "exclude <id:version>, knowledge, watch notes/edit/resolve, or group <id> notes/edit/resolve. These commands do "
            "not spend API credit. What you hear from characters: .chars hear.");
        return true;
    }
    if (verb == "guild" && !groupCommand)
    {
        std::string field;
        input >> field;
        std::string text;
        std::getline(input, text);
        std::string json;
        std::string error;
        if (!field.empty())
        {
            pbc_json change;
            if (field == "fade")
            {
                auto digits = text.find_first_of("0123456789");
                change["report_hours"] =
                    digits == std::string::npos ? pbc_json(text) : pbc_json(std::stoul(text.substr(digits, 6)));
            }
            else
                change[field] = text;
            if (!ChangeGuildIdentity(player, change.dump(), json, error))
            {
                tell("Not changed: " + error + ".");
                return true;
            }
        }
        else if (!GuildIdentityFor(player, json, error))
        {
            tell("No guild identity: " + error + ".");
            return true;
        }
        auto guild = pbc_json::parse(json);
        auto const& identity = guild["identity"];
        auto line = [&](char const* label, char const* key)
        {
            auto value = identity[key].get<std::string>();
            tell(std::string(label) + (value.empty() ? "(not written)" : value));
        };
        tell("<" + guild["name"].get<std::string>() + ">");
        line("What the guild is for: ", "purpose");
        line("What members value: ", "values");
        line("Traditions: ", "traditions");
        line("Current ambitions: ", "ambitions");
        line("How members speak: ", "voice");
        tell("Rumours fade after " + std::to_string(identity["report_hours"].get<uint64_t>()) + " hours.");
        if (field.empty())
            tell(guild["may_edit"].get<bool>()
                     ? "Change with .chars guild purpose|values|traditions|ambitions|voice <text>, or .chars guild fade <hours>."
                     : "Only the guild master and officers can change it.");
        return true;
    }
    if (verb == "why")
    {
        uint64_t line = 0;
        input >> line;
        auto guid = player->GetGUID();
        Why(player, line, [guid, line](std::string json)
        {
            auto asker = ObjectAccessor::FindPlayer(guid);
            if (!asker)
                return;
            ChatHandler chat(asker->GetSession());
            auto answer = pbc_json::parse(json, nullptr, false);
            if (answer.is_discarded() || !answer.value("ok", false))
                return chat.SendSysMessage("Cannot explain that: " + answer.value("error", std::string("unavailable")) + ".");
            if (!line)
            {
                if (answer["lines"].empty())
                    return chat.SendSysMessage("No character has said anything to you yet.");
                std::size_t shown = 0;
                for (auto const& heard : answer["lines"])
                    if (shown++ < 6)
                        chat.SendSysMessage("#" + std::to_string(heard["line"].get<uint64_t>()) + " [" +
                                            heard["channel"].get<std::string>() + "] " + heard["text"].get<std::string>());
                return chat.SendSysMessage("Why one was said: .chars why <number>.");
            }
            chat.SendSysMessage("Why " + answer["speaker"].get<std::string>() + " said: " + answer["text"].get<std::string>());
            if (answer.contains("started_by"))
                chat.SendSysMessage("Started by " + answer["started_by"]["who"].get<std::string>() + ": " +
                                    answer["started_by"]["text"].get<std::string>());
            if (answer["remembers_you"].empty())
                chat.SendSysMessage("They remember nothing in particular about you yet.");
            for (auto const& note : answer["remembers_you"])
                chat.SendSysMessage("They remember (" + note["kind"].get<std::string>() + "): " + note["text"].get<std::string>());
            for (auto const& action : answer["actions"])
                chat.SendSysMessage("Action " + action["action"].get<std::string>() + ": " + action["status"].get<std::string>());
            char cost[64];
            std::snprintf(cost, sizeof(cost), "Cost: $%.4f", answer["cost_dollars"].get<double>());
            chat.SendSysMessage(cost);
        });
        return true;
    }
    if (verb == "rumours" || verb == "rumour")
    {
        auto guid = player->GetGUID();
        auto say = [guid](std::string const& message)
        {
            if (auto asker = ObjectAccessor::FindPlayer(guid))
                ChatHandler(asker->GetSession()).SendSysMessage(message);
        };
        if (verb == "rumours")
        {
            std::string group;
            input >> group;
            Rumours(player, group, [say](std::string json)
            {
                auto answer = pbc_json::parse(json, nullptr, false);
                if (answer.is_discarded() || !answer.value("ok", false))
                    return say("No rumours: " + answer.value("error", std::string("unavailable")) + ".");
                if (answer["rumours"].empty())
                    return say("No rumours in " + answer["group"].get<std::string>() + ".");
                for (auto const& rumour : answer["rumours"])
                {
                    std::string state = rumour["resolved"].get<bool>() ? "resolved"
                                      : rumour["fades_in_ms"].is_number()
                                          ? "fades in " + std::to_string(rumour["fades_in_ms"].get<uint64_t>() / 3600000 + 1) + " h"
                                          : "current";
                    std::string by;
                    for (auto const& name : rumour["by"])
                        by += (by.empty() ? "" : ", ") + name.get<std::string>();
                    say("#" + std::to_string(rumour["id"].get<uint64_t>()) + ":" +
                        std::to_string(rumour["version"].get<uint64_t>()) + " (" + state + (by.empty() ? "" : ", from " + by) +
                        (rumour["zone"].get<std::string>().empty() ? "" : ", " + rumour["zone"].get<std::string>()) +
                        "): " + rumour["text"].get<std::string>());
                }
                if (answer["may_change"].get<bool>())
                    say("Officers: .chars rumour <id>:<version> correct <text>, resolve, or forget.");
            });
            return true;
        }
        std::string reference;
        std::string action;
        input >> reference >> action;
        std::string text;
        std::getline(input, text);
        auto colon = reference.find(':');
        if (colon == std::string::npos || action.empty())
        {
            tell("Use .chars rumour <id>:<version> correct <text>, resolve, or forget (from .chars rumours).");
            return true;
        }
        pbc_json request{{"note", std::strtoull(reference.c_str(), nullptr, 10)},
                         {"version", std::strtoull(reference.c_str() + colon + 1, nullptr, 10)},
                         {"action", action}};
        if (auto start = text.find_first_not_of(' '); start != std::string::npos)
            request["text"] = text.substr(start);
        ChangeRumour(player, request.dump(), [say](std::string json)
        {
            auto answer = pbc_json::parse(json, nullptr, false);
            say(!answer.is_discarded() && answer.value("ok", false)
                    ? "Rumour changed."
                    : "Not changed: " + answer.value("error", std::string("unavailable")) + ".");
        });
        return true;
    }
    if (verb == "hear")
    {
        std::string key;
        std::string value;
        input >> key >> value;
        std::string json;
        std::string error;
        if (!key.empty())
        {
            pbc_json change;
            if (!value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isdigit(c); }))
                change[key] = std::stoul(value.substr(0, 6));
            else
                change[key] = value;
            if (!ChangeListener(player, change.dump(), json, error))
            {
                tell("Not changed: " + error + ".");
                return true;
            }
        }
        else
            json = ListenerSettingsJson(player);
        auto settings = pbc_json::parse(json);
        auto pace = [](uint64_t value, char const* unit)
        { return value ? std::to_string(value) + unit : std::string("the realm's"); };
        tell("You hear: party " + settings["party"].get<std::string>() + ", nearby " +
             settings["nearby"].get<std::string>() + ", guild " + settings["guild"].get<std::string>() +
             ", General " + settings["general"].get<std::string>() + "; remarks " +
             settings["remarks"].get<std::string>() + ", combat banter " + settings["banter"].get<std::string>() +
             "; reading speed " + pace(settings["reading"].get<uint64_t>(), " words a minute") +
             ", longest exchange " + pace(settings["turns"].get<uint64_t>(), " replies") + ".");
        if (key.empty())
            tell("Change with .chars hear party|nearby|guild|general chatty|spoken|silent, "
                 ".chars hear remarks often|sometimes|rarely|never, .chars hear banter off|sometimes|often, "
                 ".chars hear reading 0|60-400, .chars hear turns 0-6 (0 is the realm's).");
        return true;
    }
    bool administrator = player->GetSession()->GetSecurity() >= SEC_GAMEMASTER;
    if (verb == "test")
    {
        if (!administrator || !_recorded)
        {
            tell(
                "Test controls require an administrator and recorded, "
                "credential-free transport.");
            return true;
        }
        std::string action;
        input >> action;
        if (action == "advance")
        {
            uint32_t elapsed = 0;
            if (!(input >> elapsed) || elapsed > 86400000)
                tell("Use .chars test advance <milliseconds>, at most one day.");
            else
            {
                Update(elapsed);
                tell("Character scheduling clock advanced.");
            }
        }
        else if (action == "pacing")
        {
            // Same bounds as PBC.CharacterSystem.SpacingMs/ReadingWordsPerMinute; applies to new scenes.
            uint32_t spacing = 0, wordsPerMinute = 0;
            if (!(input >> spacing >> wordsPerMinute) || spacing < 250 || spacing > 10000 ||
                (wordsPerMinute && (wordsPerMinute < 120 || wordsPerMinute > 600)))
                tell("Use .chars test pacing <250-10000 ms> <0 or 120-600 words per minute>.");
            else
            {
                _spacing = spacing;
                _readingWordsPerMinute = wordsPerMinute;
                tell("Recorded playback pacing updated for new scenes.");
            }
        }
        else if (action == "formation")
        {
            unsigned rounds = 0;
            input >> rounds;
            tell(BenchmarkDialogueSpreadFormation(player, rounds));
        }
        else if (action == "ambient")
        {
            uint32_t delay = 0;
            input >> delay;
            _nextAmbient = _now + std::min(delay, 30000u);
            _remarksDueAt = _nextAmbient;
            _nextRemark.clear();
            _lastHumanActivity = 0;
            tell("The next eligible ambient scheduling tick is due.");
        }
        else if (action == "actions")
        {
            unsigned enabled = 2;
            if (!(input >> enabled) || enabled > 1 || !_actions)
                tell(
                    "Use .chars test actions <0|1> with the action coordinator "
                    "configured.");
            else
            {
                if (!enabled)
                    _actions->CancelAll("feature_disabled", _now);
                PlayerbotDialogueBridge::Configure(enabled != 0);
                tell("Recorded native action feature state updated.");
            }
        }
        else if (action == "combat")
        {
            uint32_t chance = 0;
            if (!(input >> chance) || chance > 100)
                tell(
                    "Use .chars test combat <0-100 percent>; only admission chance "
                    "changes.");
            else
            {
                _combatChance = chance;
                tell(
                    "Recorded combat admission chance updated; encounter and cooldown "
                    "checks remain active.");
            }
        }
        else if (action == "outfit")
        {
            auto selected = player->GetSelectedUnit();
            auto bot = selected ? selected->ToPlayer() : nullptr;
            auto ai = bot ? GET_PLAYERBOT_AI(bot) : nullptr;
            std::string operation;
            input >> operation;
            if (!ai || ai->GetMaster() != player || !bot->IsInMap(player))
                tell("Select your isolated companion for an outfit test.");
            else if (operation == "refresh")
            {
                PlayerbotFactory(bot, bot->GetLevel()).Refresh();
                tell("Native test factory refresh completed.");
            }
            else if (operation == "seed")
            {
                unsigned count = 0;
                input >> count;
                if (!count || count > MAX_EQUIPMENT_SET_INDEX)
                    tell("Use .chars test outfit seed <1-10>.");
                else
                {
                    for (unsigned index = 0; index < count; ++index)
                        if (!bot->GetEquipmentSets().contains(index))
                        {
                            EquipmentSet set;
                            set.Guid = 0;
                            set.Name = "PBC fixture set " + std::to_string(index);
                            set.IconName = "INV_Shirt_01";
                            for (uint8 slot = 0; slot < EQUIPMENT_SLOT_END; ++slot)
                                if (auto item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                                    set.Items[slot] = item->GetGUID();
                            bot->SetEquipmentSet(index, set);
                        }
                    auto transaction = CharacterDatabase.BeginTransaction();
                    bot->SaveInventoryAndGoldToDB(transaction);
                    CharacterDatabase.CommitTransaction(transaction);
                    tell("Native equipment set fixtures saved.");
                }
            }
            else
                tell("Use .chars test outfit seed <count> or refresh.");
        }
        else if (action == "wear")
        {
            uint32_t percent = 0;
            auto unit = player->GetSelectedUnit();
            auto target = unit ? unit->ToPlayer() : nullptr;
            if (!target || !target->IsInMap(player) || !(input >> percent) || !percent || percent > 100)
                tell("Select a test player and use .chars test wear <1-100 percent>.");
            else
            {
                target->DurabilityLossAll(percent / 100.0, true);
                tell("Native test equipment wear applied.");
            }
        }
        else if (action == "audience")
        {
            if (auto target = player->GetSelectedUnit())
            {
                std::ostringstream result;
                result << "Audience probe: " << target->GetName() << " guid=" << target->GetGUID().GetCounter()
                       << " in_world=" << target->IsInWorld() << " alive=" << target->IsAlive()
                       << " distance=" << player->GetDistance(target) << " visible=" << player->CanSeeOrDetect(target)
                       << " los=" << player->IsWithinLOSInMap(target) << " phase=" << player->InSamePhase(target)
                       << " target_xyz=" << target->GetPositionX() << "," << target->GetPositionY() << ","
                       << target->GetPositionZ();
                if (auto observedPlayer = target->ToPlayer())
                {
                    auto ai = GET_PLAYERBOT_AI(observedPlayer);
                    result << " gm=" << observedPlayer->IsGameMaster()
                           << " initiative=" << PlayerbotDialogueBridge::AllowsInitiative(observedPlayer, player)
                           << " invitation=" << PlayerbotDialogueBridge::AllowsInvitation(observedPlayer, player)
                           << " bot_group=" << (observedPlayer->GetGroup() != nullptr)
                           << " bot_invite=" << (observedPlayer->GetGroupInvite() != nullptr)
                           << " human_group=" << (player->GetGroup() != nullptr)
                           << " human_invite=" << (player->GetGroupInvite() != nullptr)
                           << " bot_combat=" << observedPlayer->IsInCombat() << " human_combat=" << player->IsInCombat()
                           << " master=" << (ai && ai->GetMaster() ? ai->GetMaster()->GetName() : "none");
                }
                if (auto observedCreature = target->ToCreature())
                    result << " evading=" << observedCreature->IsEvadingAttacks()
                           << " unreachable=" << observedCreature->CanNotReachTarget();
                tell(result.str());
            }
        }
        else
            tell("Recorded test controls: advance <milliseconds>, ambient, pacing <ms> <words per minute>.");
        return true;
    }
    if (verb == "usage")
    {
        if (!administrator)
        {
            tell("Only a realm administrator can inspect the API budget.");
            return true;
        }
        _commands.push_back({player->GetGUID(), _storage->Submit(
                                                    [this]
                                                    {
                                                        auto totals = _ledger.Totals();
                                                        if (!totals)
                                                            return std::string("API budget unavailable.");
                                                        std::ostringstream text;
                                                        text.setf(std::ios::fixed);
                                                        text.precision(6);
                                                        text << "API budget: $" << totals->spentNano / 1e9
                                                             << " charged, $" << totals->heldNano / 1e9
                                                             << " reserved, $" << totals->ceilingNano / 1e9
                                                             << " ceiling.";
                                                        return text.str();
                                                    })});
        return true;
    }
    auto target = player->GetSelectedUnit();
    if (!target)
    {
        tell("Select the character to inspect or edit first.");
        return true;
    }
    auto actor = SnapshotActor(target, _definitions, _realmPhase);
    GameAudience context;
    context.anchor = player->GetGUID();
    context.actors = {actor};
    Enrich(context);
    actor = context.actors.front();
    if (verb == "knowledge")
    {
        if (!administrator)
            tell("Knowledge inspection requires a realm administrator.");
        else
        {
            auto facts = pbc_json::parse(actor.factsJson);
            std::string records;
            for (auto const& record : facts["knowledge"])
                records += record.value("id", "") + " ";
            tell("Knowledge for " + actor.identity.name + ": " + std::to_string(facts.value("knowledge_bytes", 0u)) +
                 " bytes; records: " + records + "; groups: " + facts["information_groups"].dump() +
                 "; quests: " + facts.value("interaction_quests", pbc_json::object()).dump());
        }
        return true;
    }
    if (watch && actor.watchId.empty())
        for (auto const& group : actor.informationGroups)
            if (group.publicReports)
            {
                actor.watchId = group.id;
                break;
            }
    if (groupCommand)
    {
        auto found = std::find_if(actor.informationGroups.begin(), actor.informationGroups.end(),
                                  [&](auto const& group) { return group.id == groupId; });
        if (!administrator || found == actor.informationGroups.end())
        {
            tell("Select a current member of that group as a realm administrator.");
            return true;
        }
        actor.watchId = groupId;
        watch = true;
    }
    if (watch)
    {
        if (!administrator || actor.watchId.empty())
        {
            tell(
                "A realm administrator must select an NPC with a configured local "
                "watch.");
            return true;
        }
        actor.identity = ActorRecord{};
        actor.identity.id = actor.watchId;
        actor.identity.kind = "watch";
        actor.identity.name = "Local watch reports";
        actor.watchId.clear();
        actor.informationGroups.clear();
        actor.spawnId = 0;
    }
    uint64_t editor = player->GetGUID().GetCounter();
    if (!administrator && actor.identity.ownerGuid != editor)
    {
        tell(
            "Only this character's owner or a realm administrator can inspect or "
            "change private records.");
        return true;
    }
    std::string reference;
    input >> reference;
    std::string text;
    std::getline(input >> std::ws, text);
    if (verb != "info" && verb != "history" && verb != "notes" && verb != "fact" && verb != "edit" &&
        verb != "resolve" && verb != "episode" && verb != "revise" && verb != "exclude")
    {
        tell("Unknown character command. Use .chars help.");
        return true;
    }
    if ((verb == "revise" || verb == "exclude") && !administrator)
    {
        tell(
            "Source revision/exclusion requires a realm administrator; owner "
            "memory corrections use .chars edit.");
        return true;
    }
    bool editing = verb == "fact" || verb == "edit" || verb == "resolve" || verb == "revise" || verb == "exclude";
    if (editing)
        Cancel(actor.identity.id);
    auto operation = Identifier();
    _commands.push_back(
        {player->GetGUID(),
         _storage->Submit(
             [this, actor, editor, administrator, verb, reference, text, operation]() -> std::string
             {
                 GameAudience snapshot;
                 snapshot.actors.push_back(actor);
                 if (!StoreActors(snapshot))
                     return "Character storage is unavailable.";
                 auto saved = _store.Actor(actor.identity.id);
                 if (!saved)
                     return "Character not found.";
                 if (!administrator && saved->ownerGuid != editor)
                     return "Only this character's owner or a realm administrator can "
                            "inspect or change private records.";
                 if (verb == "info")
                 {
                     auto description = saved->foundation.empty() ? "No generated biography yet." : saved->foundation;
                     return saved->name + " (version " + std::to_string(saved->version) + "): " + description;
                 }
                 if (verb == "history")
                 {
                     auto sources = _store.Observations(saved->id, false, 10);
                     std::string result = "Recent witnessed history for " + saved->name + ":";
                     for (auto const& source : sources)
                         result += "\n" + source.source.Key() + " [" + source.channel + "] " + source.text;
                     return result;
                 }
                 if (verb == "notes")
                 {
                     auto notes = _store.Notes(saved->id, 10);
                     std::string result = "Recent notes for " + saved->name + ":";
                     for (auto const& note : notes)
                         result += "\n" + std::to_string(note.id) + ":" + std::to_string(note.version) + " [" +
                                   note.kind + (note.resolved ? ", resolved" : "") + "] " + note.text;
                     return result;
                 }
                 if (verb == "episode" || verb == "revise" || verb == "exclude")
                 {
                     auto referenceId = SourceVersion::Parse(reference);
                     if (!referenceId)
                         return "Use a source-id:version from .chars history.";
                     auto source = _store.Source(saved->id, referenceId->id);
                     if (!source)
                         return "That source is not in this character's witnessed history.";
                     if (verb == "episode")
                         return source->source.Key() + " [" + source->channel + "; " + source->evidence + "] " +
                                source->text.substr(0, 3000);
                     if (!_store.ReviseSource(*referenceId, verb == "exclude" ? source->text : text, verb == "exclude",
                                              administrator))
                         return "No source change confirmed; inspect its current version.";
                     return "Source updated; the original version is retained and "
                            "derived model recall invalidated.";
                 }
                 if (saved->kind == "generic_npc")
                     return "This is a generic NPC; lasting knowledge belongs to its "
                            "configured local watch.";
                 bool changed = false;
                 if (verb == "fact")
                 {
                     uint64_t version = 0;
                     auto parsed = std::from_chars(reference.data(), reference.data() + reference.size(), version);
                     if (parsed.ec != std::errc() || parsed.ptr != reference.data() + reference.size())
                         return "Use .chars fact <character-version> <text>; .chars info "
                                "shows the current version.";
                     changed =
                         _store.AddOwnerFact(saved->id, version, operation, text, editor, administrator, EpochMs());
                 }
                 else
                 {
                     auto note = SourceVersion::Parse(reference);
                     if (!note)
                         return "Use the note-id:version shown by .chars notes.";
                     changed = verb == "edit"
                                   ? _store.EditNote(saved->id, note->id, note->version, text, editor, administrator)
                                   : _store.ResolveNote(saved->id, note->id, note->version, editor, administrator);
                 }
                 return changed ? "Character record updated. Pending old replies were "
                                  "cancelled."
                                : "No change confirmed. Check the record version and "
                                  "your permission, then inspect it again.";
             })});
    return true;
}

}  // namespace PBC::RuntimeDetail
