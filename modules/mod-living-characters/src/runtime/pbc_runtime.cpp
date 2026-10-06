// PBC Character System changes, 2026-09-30; see NOTICE.md for upstream attribution.
#include "PopulationMgr.h"
#include "pbc_runtime.h"
#include "pbc_game.h"
#include "pbc_action_input.h"
#include "pbc_action_store.h"
#include "pbc_initiative.h"
#include "PlayerbotDialogue.h"
#include "Playerbots.h"
#include "pbc_conversation.h"
#include "pbc_service.h"
#include "pbc_context.h"
#include "pbc_recorded.h"
#include "pbc_worker.h"
#include "pbc_json.h"
#include "pbc_log.h"
#include "Channel.h"
#include "ChannelMgr.h"
#include "Chat.h"
#include "Creature.h"
#include "Config.h"
#include "CryptoRandom.h"
#include "DBCStores.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "RBAC.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "WorldSession.h"
#include "WorldSessionMgr.h"
#include "Group.h"
#include "pbc_native.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <charconv>
#include <fstream>
#include <memory>
#include <limits>
#include <mutex>
#include <random>
#include <sstream>

#include "pbc_runtime_internal.h"

namespace PBC
{
namespace RuntimeDetail
{
bool Runtime::StoreActors(GameAudience const& audience)
{
    for (auto const& actor : audience.actors)
    {
        if (!_store.EnsureActor(actor.identity))
            return false;
        if ((actor.identity.kind == "bot" || actor.identity.kind == "player") && actor.identity.ownerGuid &&
            !_store.AssignOwner(actor.identity.id, actor.identity.ownerGuid))
            return false;
        for (auto const& group : actor.informationGroups)
        {
            // Keep the legacy storage discriminator so existing watches and
            // their evidence need no destructive migration. All use one API.
            if (!_store.EnsureActor({group.id, "watch", 0, group.name}))
                return false;
        }
        if (!actor.watchId.empty() && !_store.EnsureActor({actor.watchId, "watch", 0, "Local watch reports"}))
            return false;
        if (actor.spawnId && (actor.identity.kind == "named_npc" || !actor.watchId.empty()) &&
            !_store.MapNpc({actor.spawnId, actor.mapId, actor.instanceId, actor.identity.id, actor.watchId}))
            return false;
    }
    return true;
}

std::vector<std::string> Runtime::Witnesses(GameAudience const& audience) const
{
    std::set<std::string> result;
    for (auto const& actor : audience.actors)
    {
        result.insert(actor.identity.id);
        for (auto const& group : actor.informationGroups)
            if ((group.publicReports && (audience.label == "say" || audience.label == "yell" || audience.label == "emote")) ||
                (audience.label == "guild" && group.id == "guild:" + std::to_string(audience.guildId)))
                result.insert(group.id);
    }
    return {result.begin(), result.end()};
}

GameActor* Runtime::Speaker(Scene& scene, std::string const& id)
{
    for (auto& actor : scene.audience.actors)
        if (actor.identity.id == id)
            return &actor;
    return nullptr;
}

void Runtime::Abort(Scene& scene, std::string const& reason)
{
    if (!scene.cancelled->exchange(true))
        PBC_Log(PBC_LogLevel::PBC_DEFAULT, "Character scene {}: stopped={}, actor={}, delivered={}", scene.id, reason,
                scene.chosen, scene.delivered.size());
    scene.conversation.Abort();
}

void Runtime::Cancel(std::string const& actor)
{
    for (auto& scene : _scenes)
        if (Speaker(*scene, actor) ||
            std::any_of(scene->audience.actors.begin(), scene->audience.actors.end(),
                        [&](auto const& participant)
                        {
                            return participant.watchId == actor ||
                                std::any_of(participant.informationGroups.begin(), participant.informationGroups.end(),
                                    [&](auto const& group) { return group.id == actor; });
                        }))
            Abort(*scene, "participant_changed:" + actor);
}

void Runtime::Chat(Player* sender, uint32_t type, uint32_t language, std::string const& text, Player* receiver,
                   Channel* channel, bool ambient, ObjectGuid addressed, bool nativeEmote)
{
    if (_delivering || !sender || !sender->GetSession() || sender->GetSession()->IsBot() || language == LANG_ADDON ||
        text.empty() || text.size() > 4096 || text.front() == '@' || text.front() == '!' || text.front() == '.' ||
        text.front() == '/')
        return;
    // Opposing factions receive empty custom-emote text unless the sender has
    // cross-faction chat permission. Native gestures are independently readable.
    bool sharedLanguage =
        type == CHAT_MSG_EMOTE
            ? nativeEmote || sender->GetSession()->HasPermission(rbac::RBAC_PERM_TWO_SIDE_INTERACTION_CHAT)
            : language == LANG_UNIVERSAL;
    auto audience =
        CaptureAudience(sender, type, receiver, channel, _definitions, _realmPhase, nullptr, sharedLanguage);
    if (audience.label.empty())
        return;
    Enrich(audience);
    FilterLanguage(audience, sharedLanguage ? LANG_UNIVERSAL : language);
    if (!ambient)
        for (auto& actor : audience.actors)
            if (auto unit = ResolveActor(actor); unit && unit->IsPlayer() && unit->IsAlive())
            {
                audience.duringCombat |= unit->IsInCombat();
                actor.canSpeak = !actor.human;
            }
    if (!addressed.IsEmpty())
        for (auto& actor : audience.actors)
            actor.addressed = actor.guid == addressed;
    if (!ambient)
        _lastHumanActivity = _now;
    std::set<std::string> eligible;
    for (auto const& actor : audience.actors)
        if (actor.canSpeak && eligible.size() < 254)
            eligible.insert(actor.identity.id);
    if (std::none_of(audience.actors.begin(), audience.actors.end(), [](auto const& actor) { return !actor.human; }))
        return;
    for (auto const& actor : audience.actors)
    {
        Cancel(actor.identity.id);
        if (!ambient && _memoryRetryAfter[actor.identity.id] == std::numeric_limits<uint64_t>::max())
            _memoryRetryAfter[actor.identity.id] = 0;
        if (!ambient && !actor.watchId.empty() &&
            _memoryRetryAfter[actor.watchId] == std::numeric_limits<uint64_t>::max())
            _memoryRetryAfter[actor.watchId] = 0;
    }
    auto scene = std::make_unique<Scene>();
    scene->audience = std::move(audience);
    scene->contribution = text;
    scene->background = ambient;
    if (!ambient && _actions)
    {
        scene->actions = CaptureActionFrame(scene->audience, text, scene->id, ++_inputSequence, _now);
        auto pending = _pendingActions.find(sender->GetGUID().GetRawValue());
        if (pending != _pendingActions.end())
        {
            if (pending->second.Valid(scene->actions.actors, scene->audience.label, _now) &&
                pending->second.TargetsValid(scene->actions.targets))
            {
                scene->pendingAction = pending->second;
                scene->actions = CaptureActionFrame(scene->audience,
                                                    pending->second.contribution + "\nClarification answer: " + text,
                                                    scene->id, _inputSequence, _now);
                auto facts = pbc_json::parse(scene->actions.factsJson);
                facts["pending_request"] = {{"contribution", pending->second.contribution},
                                            {"decisions", pending->second.decisions},
                                            {"missing_fields", pending->second.missing}};
                scene->actions.factsJson = facts.dump();
                scene->actions.questions.push_back(
                    {"pending_answer",
                     "Does the current contribution directly fill a listed missing field in pending_request? "
                     "YES only for a compatible answer such as an item name, quantity, target or wait duration. "
                     "A new command, changing an already-known argument or actor, cancellation, greeting, "
                     "unrelated remark or vague agreement "
                     "is NO and replaces the pending request. Never infer an authorization or amount from 'yes'.",
                     {{"YES", "A compatible answer supplies a missing argument of the still-pending request."},
                      {"NO", "A new request, cancellation, unrelated message, or no definite missing argument."}}});
            }
            _pendingActions.erase(pending);
        }
        auto recent = _recentCombatOrders.find(sender->GetGUID().GetRawValue());
        if (recent != _recentCombatOrders.end())
        {
            if (recent->second.Valid(scene->actions.actors, scene->audience.label, _now))
            {
                auto facts = pbc_json::parse(scene->actions.factsJson);
                facts["recent_combat_order"] = {{"contribution", recent->second.contribution},
                                                {"orders", pbc_json::parse(recent->second.factsJson)}};
                scene->actions.factsJson = facts.dump();
                BindCombatReference(scene->actions, recent->second, text);
            }
            else
                _recentCombatOrders.erase(recent);
        }
        scene->actionsPending = !scene->actions.questions.empty();
    }
    if (!ambient)
        scene->transcript.push_back(sender->GetName() + ": " + text);
    scene->conversation.SetSpacing(_spacing);
    scene->conversation.SetReadingWordsPerMinute(_readingWordsPerMinute);
    PBC_Log(PBC_LogLevel::PBC_DEFAULT, "Character scene {}: channel={}, eligible={}, allowance={}.", scene->id,
            scene->audience.label, eligible.size(), _maxTurns);
    scene->conversation.Begin(ambient ? Contribution::Ambient : Contribution::Human, true, std::move(eligible),
                              scene->audience.duringCombat ? std::min<uint8_t>(_maxTurns, 2) : _maxTurns, _now);
    ObservationRecord event;
    event.eventKey = scene->id + ":incoming";
    event.sceneId = scene->id;
    event.authorId = ambient ? "world" : "player:" + std::to_string(sender->GetGUID().GetCounter());
    event.channel = ambient ? "event" : scene->audience.label;
    event.mapId = sender->GetMapId();
    event.instanceId = sender->GetInstanceId();
    event.zoneId = sender->GetZoneId();
    event.createdMs = _epochStart + _now;
    event.text = ambient ? text : sender->GetName() + ": " + text;
    auto observed = scene->audience;
    if (scene->conversation.Phase() == ConversationPhase::Done && !scene->actionsPending)
    {
        _observations.push_back(_storage->Submit(
            [this, observed = std::move(observed), event]
            { return StoreActors(observed) && _store.Observe(event, Witnesses(observed)).has_value(); }));
        return;
    }
    scene->admitted =
        _storage->Submit([this, observed = std::move(observed), event]
                         { return StoreActors(observed) && _store.Observe(event, Witnesses(observed)).has_value(); });
    _scenes.push_back(std::move(scene));
}

CharacterInput Runtime::Input(Scene const& scene, GameActor const& actor) const
{
    CharacterInput input;
    input.actorId = actor.identity.id;
    input.watchId = actor.watchId;
    GameAudience current;
    current.anchor = scene.audience.anchor;
    current.actors = {actor};
    Enrich(current);
    input.informationGroups = current.actors.front().informationGroups;
    auto facts = pbc_json::parse(current.actors.front().factsJson);
    facts["channel"] = scene.audience.label;
    facts["gameplay_actions_available"] = actor.guid.IsPlayer() && _actions && PlayerbotDialogueBridge::Enabled();
    if (auto unit = ResolveActor(actor); unit && unit->IsPlayer())
        if (auto ai = GET_PLAYERBOT_AI(unit->ToPlayer()))
            facts["current_movement"] = {{"holding_position", ai->HasStrategy("stay", BOT_STATE_NON_COMBAT)},
                                         {"following_requester", ai->HasStrategy("follow", BOT_STATE_NON_COMBAT)}};
    if (scene.audience.combat || scene.audience.duringCombat)
    {
        input.maxSegments = 1;
        facts["delivery_instruction"] =
            "An actual fight is ongoing. At most one brief sentence or emote, "
            "or remain silent. Hostility is not friendship; do not claim victory, invented mechanics, "
            "successful attacks or knowledge you do not have. Beast-like creatures may use a natural "
            "nonverbal emote rather than human speech. This public exchange has at most two turns.";
    }
    if (!scene.actions.actors.empty())
        facts["action_contract"] =
            "Actions are coordinated separately. Express willingness or uncertainty, "
            "never claim a cast, arrival, transfer or other result without a corresponding observed outcome. "
            "A selected intention or started action is not a completed result.";
    if (scene.actionFeedback.actors.contains(actor.identity.id) ||
        scene.commandedActors.contains(actor.guid.GetRawValue()))
    {
        auto actionFacts = pbc_json::parse(scene.actions.factsJson);
        // The human's action snapshot may include creatures the companion
        // cannot see. Describe only currently visible shared references.
        facts["nearby_action_targets"] = pbc_json::array();
        auto observer = ResolveActor(actor);
        if (observer && actionFacts.contains("targets"))
            for (auto const& target : actionFacts["targets"])
            {
                auto reference = scene.actions.targets.find(target.value("id", ""));
                if (reference == scene.actions.targets.end())
                    continue;
                auto unit = ObjectAccessor::GetUnit(*observer, ObjectGuid(reference->second.guid));
                if (unit && observer->IsInMap(unit) && observer->InSamePhase(unit) &&
                    observer->IsWithinDistInMap(unit, 60.0f) && observer->CanSeeOrDetect(unit))
                    facts["nearby_action_targets"].push_back(target);
            }
    }
    if (!scene.actionClarification.empty() && scene.actionFeedback.actors.contains(actor.identity.id))
        facts["action_clarification"] = scene.actionClarification;
    if (!scene.interpretedActions.empty())
    {
        pbc_json disposition = {
            {"status", scene.actionClarification.empty() ? "unchanged" : scene.actionFeedback.status},
            {"your_order_submitted", scene.commandedActors.contains(actor.guid.GetRawValue())},
            {"completion_confirmed", false},
            {"orders", pbc_json::array()}};
        if (auto outcomes = scene.actionOutcomes.find(actor.guid.GetRawValue()); outcomes != scene.actionOutcomes.end())
            for (auto const& [id, pair] : outcomes->second)
            {
                auto const& [request, outcome] = pair;
                auto status = outcome.status == ActionStatus::Completed    ? "completed"
                              : outcome.status == ActionStatus::Started    ? "started"
                              : outcome.status == ActionStatus::Failed     ? "failed"
                              : outcome.status == ActionStatus::Cancelled  ? "cancelled"
                              : outcome.status == ActionStatus::Unresolved ? "unresolved"
                                                                           : "pending";
                disposition["orders"].push_back({{"kind", ActionName(request.kind)},
                                                 {"status", status},
                                                 {"detail", outcome.detail},
                                                 {"item_entry", request.objectId},
                                                 {"quantity", request.quantity},
                                                 {"trade_quantities", request.tradeQuantities}});
                disposition["status"] = status;
                disposition["completion_confirmed"] = outcome.status == ActionStatus::Completed;
            }
        if (scene.commandedActors.contains(actor.guid.GetRawValue()) && disposition["orders"].empty())
            disposition["status"] = "accepted_pending_execution";
        if (auto pending = scene.requestedPerformances.find(actor.guid.GetRawValue());
            pending != scene.requestedPerformances.end())
        {
            disposition["status"] = "awaiting_your_performance_decision";
            disposition["requested_steps"] = pbc_json::array();
            for (auto const& request : pending->second)
            {
                auto items = pbc_json::array();
                for (auto const& item : request.items)
                    items.push_back({{"name", item.name}, {"quantity", item.count}});
                disposition["requested_steps"].push_back(
                    {{"kind", ActionName(request.kind)}, {"duration_ms", request.durationMs}, {"items", items}});
            }
        }
        facts["request_interpretation"] = std::move(disposition);
    }
    if (auto inventory = scene.actions.inventory.find(actor.guid.GetRawValue());
        inventory != scene.actions.inventory.end())
    {
        facts["own_carried_items"] = pbc_json::array();
        std::map<uint32_t, std::pair<std::string, uint32_t>> counts;
        for (auto const& item : inventory->second)
        {
            auto& count = counts[item.entry];
            count.first = item.name;
            count.second += item.count;
        }
        for (auto const& [entry, item] : counts)
            facts["own_carried_items"].push_back({{"name", item.first}, {"quantity", item.second}});
    }
    // Only the speaking character receives this compact capability list. Keep
    // private spellbooks out of other characters' contexts and the raid selector.
    if (auto player = ObjectAccessor::FindPlayer(actor.guid))
    {
        std::set<std::string> spells;
        for (auto const& [spellId, known] : player->GetSpellMap())
        {
            if (known->State == PLAYERSPELL_REMOVED || !known->Active ||
                !(known->specMask & player->GetActiveSpecMask()))
                continue;
            auto spell = sSpellMgr->GetSpellInfo(spellId);
            if (spell && !spell->IsPassive() && spell->SpellName[LOCALE_enUS][0])
                spells.insert(spell->SpellName[LOCALE_enUS]);
        }
        facts["known_active_spells_complete"] = spells.size() <= 64;
        facts["known_active_spells"] = pbc_json::array();
        for (auto const& name : spells)
        {
            if (facts["known_active_spells"].size() == 64)
                break;
            facts["known_active_spells"].push_back(name);
        }
    }
    facts["participants"] = pbc_json::array();
    for (auto const& other : scene.audience.actors)
    {
        input.subjects.insert(other.identity.id);
        auto otherFacts = pbc_json::parse(other.factsJson);
        facts["participants"].push_back(
            {{"id", other.identity.id},
             {"display_label", other.identity.name},
             {"kind", other.identity.kind},
             {"group_id", otherFacts.value("group_id", pbc_json{})},
             {"physically_present",
              scene.audience.label == "say" || scene.audience.label == "emote" || scene.audience.label == "yell"}});
    }
    input.gameFactsJson = facts.dump();
    input.contribution = scene.contribution;
    if (!scene.audience.combat && !scene.audience.duringCombat)
        input.animations = NativeAnimations();
    input.actionOptions = scene.initiative.descriptions;
    input.remainingTurns = scene.conversation.Remaining();
    input.nowMs = _epochStart + _now;
    input.request.reason = scene.audience.combat ? "combat_banter" : scene.background ? "ambient" : "direct";
    input.request.background = scene.background;
    input.request.sceneId = scene.id;
    input.request.mapId = actor.mapId;
    input.request.instanceId = actor.instanceId;
    input.request.zoneId = actor.zoneId;
    return input;
}

GameAudience Runtime::DeliveryAudience(Scene const& scene, GameActor const& actor, Segment const& segment) const
{
    auto anchor = ObjectAccessor::FindPlayer(scene.audience.anchor);
    if (scene.audience.combat)
    {
        auto audience = CaptureCombatAudience(anchor, ResolveCombatEnemy(scene.audience), _definitions, _realmPhase,
                                             ResolveActor(actor));
        Enrich(audience);
        FilterLanguage(audience, segment.kind == Segment::Kind::Emote ? LANG_UNIVERSAL : SpokenLanguage(actor));
        return audience;
    }
    Channel* channel = nullptr;
    if (anchor && !scene.audience.channelName.empty())
        if (auto manager = ChannelMgr::forTeam(anchor->GetTeamId()))
            channel = manager->GetChannel(scene.audience.channelName, anchor, false);
    auto receiver = ObjectAccessor::FindPlayer(scene.audience.whisperTarget);
    uint32_t type = scene.audience.chatType;
    if (type == CHAT_MSG_SAY || type == CHAT_MSG_YELL || type == CHAT_MSG_EMOTE)
        type = segment.kind == Segment::Kind::Emote
                   ? CHAT_MSG_EMOTE
                   : (type == CHAT_MSG_YELL && actor.guid.IsPlayer() ? CHAT_MSG_YELL : CHAT_MSG_SAY);
    auto audience = CaptureAudience(anchor, type, receiver, channel, _definitions, _realmPhase, ResolveActor(actor));
    audience.duringCombat = scene.audience.duringCombat;
    Enrich(audience);
    FilterLanguage(audience, segment.kind == Segment::Kind::Emote ? LANG_UNIVERSAL : SpokenLanguage(actor));
    return audience;
}

void Runtime::Tick(Scene& scene)
{
    auto generateReply = [&](GameActor const& actor)
    {
        auto input = Input(scene, actor);
        scene.generationKnowledgeState = KnowledgeState(actor, scene.audience.anchor);
        auto cancelled = scene.cancelled;
        scene.generationActorGuid = actor.guid.GetRawValue();
        scene.generationActionVersion = scene.actionVersions[scene.generationActorGuid];
        scene.generation = _inference->Submit(
            [this, input, cancelled]
            {
                if (cancelled->load())
                    return CharacterTurn{};
                return _characters->Reply(input);
            });
    };
    if (!scene.generationKnowledgeState.empty() && !scene.cancelled->load())
        if (auto actor = Speaker(scene, scene.chosen); actor &&
            KnowledgeState(*actor, scene.audience.anchor) != scene.generationKnowledgeState)
            Abort(scene, "knowledge_or_membership_changed");
    // Never deliver remaining instructions written before a native action
    // changed. Already delivered lines and their source-bound notes survive.
    if (scene.turn && scene.actionVersions[scene.generationActorGuid] != scene.generationActionVersion &&
        !scene.cancelled->load())
        Abort(scene, "action_changed_during_playback");

    if (scene.audience.combat && !scene.cancelled->load() &&
        (_now >= scene.expiresMs || !ResolveCombatEnemy(scene.audience) || !CombatSpeechQuiet(scene.audience)))
        Abort(scene, "combat_ended_or_scripted_speech");
    if (!scene.admittedOk)
    {
        if (!Ready(scene.admitted))
            return;
        scene.admittedOk = scene.admitted.get();
        if (!scene.admittedOk)
        {
            scene.actionsPending = false;
            scene.actions.questions.clear();
            Abort(scene);
            return;
        }
    }
    if (Ready(scene.selection))
    {
        auto selection = scene.selection.get();
        PBC_Log(PBC_LogLevel::PBC_DEFAULT, "Character scene {}: selection={}, error={}.", scene.id, selection.text,
                selection.error);
        if (!selection.success && !scene.cancelled->load() && !scene.actions.questions.empty())
            if (auto human = ObjectAccessor::FindPlayer(scene.audience.anchor); HasHumanConnection(human))
                ChatHandler(human->GetSession())
                    .SendSysMessage(selection.error == "selector_context_limit"
                                        ? "There are too many details to interpret that message. Please name the "
                                          "companion and target, or split the request. No action was taken."
                                        : "I couldn't process that message right now. No new action was taken; "
                                          "explicit @ commands are still available.");
        if (_actions && !scene.actions.questions.empty() && !selection.decisions.empty())
        {
            std::string clarification;
            if (scene.pendingAction && scene.pendingAction->Valid(scene.actions.actors, scene.audience.label, _now) &&
                scene.pendingAction->TargetsValid(scene.actions.targets))
                selection = scene.pendingAction->Merge(std::move(selection));
            auto requests = ResolveActionDecisions(scene.actions, selection, clarification, &scene.actionFeedback);
            scene.interpretedActions = selection.decisions;
            scene.actionClarification = clarification;
            auto choice = [&](char const* key)
            {
                auto found = selection.decisions.find(key);
                return found == selection.decisions.end() ? std::string("missing") : found->second;
            };
            PBC_Log(PBC_LogLevel::PBC_DEFAULT,
                    "Character scene {}: second_action={}, actions={}/{}, recipients={}/{}, durations={}/{}, "
                    "targets={}/{}, sequences={}, clarification={}",
                    scene.id, choice("second_action"), choice("intent1"), choice("intent2"), choice("recipient1"),
                    choice("recipient2"), choice("duration1"), choice("duration2"), choice("target1"),
                    choice("target2"), requests.size(), clarification);
            RecentCombatOrder recent;
            recent.channel = scene.audience.label;
            recent.contribution = scene.contribution;
            recent.expiresMs = _now + 120000;
            auto combatOrders = pbc_json::array();
            for (auto& sequence : requests)
            {
                auto actor = sequence.front().actor.guid;
                _actionAudiences[scene.id] = {scene.audience, _now + 3720000};
                if (std::any_of(sequence.begin(), sequence.end(), [](auto const& request)
                                { return request.kind == ActionKind::Unequip || request.kind == ActionKind::Dance; }))
                {
                    scene.requestedPerformances.emplace(actor, std::move(sequence));
                    continue;
                }
                auto submitted = sequence;
                if (_actions->Submit(std::move(sequence), _now))
                {
                    scene.commandedActors.insert(actor);
                    for (auto const& order : submitted)
                        if (order.kind == ActionKind::Attack || order.kind == ActionKind::Pull ||
                            order.kind == ActionKind::PetAttack)
                        {
                            auto human = ObjectAccessor::FindPlayer(scene.audience.anchor);
                            auto target =
                                human ? ObjectAccessor::GetUnit(*human, ObjectGuid(order.target.guid)) : nullptr;
                            if (!target)
                                continue;
                            recent.actors.push_back(order);
                            combatOrders.push_back(
                                {{"intent", ActionName(order.kind)},
                                 {"recipient", "player:" + std::to_string(ObjectGuid(actor).GetCounter())},
                                 {"previous_target_guid", order.target.guid},
                                 {"target_entry", target->GetEntry()},
                                 {"target_name", target->GetName()}});
                        }
                }
                else
                {
                    scene.actionClarification =
                        "I couldn't accept that order; an unresolved earlier action "
                        "or a changed request may prevent it. Nothing new was started.";
                    scene.actionFeedback.status = "submission_rejected";
                    clarification = scene.actionClarification;
                }
            }
            if (!recent.actors.empty() && _recentCombatOrders.size() < 128)
            {
                recent.factsJson = combatOrders.dump();
                _recentCombatOrders[scene.audience.anchor.GetRawValue()] = std::move(recent);
            }
            else if (!scene.commandedActors.empty())
                _recentCombatOrders.erase(scene.audience.anchor.GetRawValue());
            if (!clarification.empty() && !scene.cancelled->load())
            {
                std::string respondent;
                for (auto const& id : scene.actionFeedback.actors)
                    if (auto actor = Speaker(scene, id);
                        actor && actor->canSpeak && AudienceStillValid(scene.audience, *actor))
                    {
                        if (respondent.empty() || selection.text == id)
                            respondent = id;
                    }
                if (!respondent.empty())
                {
                    // A known companion must be able to ask even when the speaker
                    // classifier chose STOP. This is one answer, not another exchange.
                    selection.text = respondent;
                    scene.conversation.Begin(Contribution::Human, true, {respondent}, 1, _now);
                    if (!scene.actionFeedback.missing.empty() && _pendingActions.size() < 128)
                    {
                        PendingAction pending;
                        auto isAnswer = selection.decisions.find("pending_answer");
                        pending.contribution =
                            scene.pendingAction && isAnswer != selection.decisions.end() && isAnswer->second == "YES"
                                ? scene.pendingAction->contribution + "\n" + scene.contribution
                                : scene.contribution;
                        pending.channel = scene.audience.label;
                        pending.decisions = selection.decisions;
                        pending.decisions.erase("pending_answer");
                        pending.missing = scene.actionFeedback.missing;
                        for (auto const& [field, value] : pending.decisions)
                            if (field.starts_with("target") && !pending.missing.contains(field))
                                if (auto target = scene.actions.targets.find(value);
                                    target != scene.actions.targets.end())
                                    pending.targets.emplace(value, target->second);
                        pending.expiresMs = _now + 120000;
                        for (auto const& actor : scene.actions.actors)
                            if (scene.actionFeedback.actors.contains(
                                    "player:" + std::to_string(ObjectGuid(actor.actor.guid).GetCounter())))
                                pending.actors.push_back(actor);
                        _pendingActions[scene.audience.anchor.GetRawValue()] = std::move(pending);
                    }
                }
                else if (auto human = ObjectAccessor::FindPlayer(scene.audience.anchor); HasHumanConnection(human))
                    ChatHandler(human->GetSession()).SendSysMessage(clarification);
            }
            scene.actions.questions.clear();
        }
        if (selection.success && selection.text == "STOP" && !scene.requestedPerformances.empty())
            for (auto const& [guid, requests] : scene.requestedPerformances)
            {
                auto id = "player:" + std::to_string(ObjectGuid(guid).GetCounter());
                if (auto actor = Speaker(scene, id);
                    actor && actor->canSpeak && AudienceStillValid(scene.audience, *actor))
                {
                    selection.text = id;
                    scene.conversation.Begin(Contribution::Human, true, {id}, 1, _now);
                    break;
                }
            }
        if (!selection.success || selection.text == "STOP" || scene.cancelled->load())
            Abort(scene);
        else if (auto actor = Speaker(scene, selection.text);
                 actor && selection.text != scene.previousSpeaker && AudienceStillValid(scene.audience, *actor))
        {
            scene.chosen = selection.text;
            scene.initiative = {};
            auto guid = actor->guid.GetRawValue();
            scene.requestedPerformance = scene.requestedPerformances.contains(guid);
            if (scene.requestedPerformance)
            {
                auto const& requests = scene.requestedPerformances.at(guid);
                for (unsigned i = 0; i < requests.size(); ++i)
                {
                    auto option = "requested_performance:" + std::to_string(i + 1);
                    scene.initiative.requests.emplace(option, requests[i]);
                    scene.initiative.descriptions.emplace(
                        option, "The human requests " + ActionName(requests[i].kind) + " from you, as step " +
                                    std::to_string(i + 1) +
                                    ". You may agree, negotiate or decline. "
                                    "Choosing this submits only this exact native step, after your reply; nothing has "
                                    "happened yet.");
                }
            }
            else if (_actions && scene.actionClarification.empty() && !scene.audience.combat &&
                     !scene.audience.duringCombat && !scene.initiativeUsed && !scene.commandedActors.contains(guid) &&
                     !_actions->Active(guid) && _now >= _nextInitiative[guid])
                scene.initiative = CaptureInitiative(scene.audience, *actor, scene.id, ++_inputSequence, _now);
            generateReply(*actor);
        }
        else
            Abort(scene);
    }
    if (Ready(scene.generation))
    {
        auto turn = scene.generation.get();
        if (!turn.success)
            Abort(scene, "generation:" + turn.error);
        else if (!scene.cancelled->load() &&
                 scene.actionVersions[scene.generationActorGuid] != scene.generationActionVersion)
        {
            auto id = turn.requestId;
            _storage->Submit([this, id] { return _ledger.RecordDelivery(id, "discarded"); });
            // At most one refresh, charged normally. Repeated state changes
            // cancel rather than creating an inference loop.
            if (auto actor = Speaker(scene, scene.chosen);
                actor && scene.actionRefreshes++ == 0 && AudienceStillValid(scene.audience, *actor))
                generateReply(*actor);
            else
                Abort(scene, "action_changed_during_generation");
        }
        else
        {
            scene.turn = std::move(turn);
            scene.delivered.clear();
            if (scene.turn->dialogue.repeatedSegments)
                PBC_Log(PBC_LogLevel::PBC_WARNING, "Character scene {}: dropped {} repeated reply segments from {}.",
                        scene.id, scene.turn->dialogue.repeatedSegments, scene.turn->actor.id);
            if (!scene.cancelled->load())
            {
                if (!scene.conversation.Choose(scene.conversation.Version(), scene.turn->actor.id,
                                               scene.turn->actor.version) ||
                    !scene.conversation.Accept(scene.conversation.Version(), scene.turn->actor.version,
                                               scene.turn->dialogue, scene.turn->requestId, _now))
                    Abort(scene, "turn_state_changed");
            }
        }
    }
    if (scene.delivery)
    {
        auto& delivery = *scene.delivery;
        if (Ready(delivery.prepared))
        {
            bool prepared = delivery.prepared.get();
            auto actor = Speaker(scene, scene.conversation.Actor());
            if (!prepared || scene.cancelled->load() || !actor || !AudienceStillValid(scene.audience, *actor) ||
                Witnesses(DeliveryAudience(scene, *actor, delivery.segment.segment)) != delivery.witnesses)
            {
                if (prepared)
                {
                    auto key = delivery.observation.eventKey;
                    _storage->Submit([this, key] { return _store.CloseDelivery(key, std::nullopt); });
                }
                scene.delivery.reset();
                Abort(scene, !prepared ? "delivery_prepare_failed" : "delivery_audience_changed");
            }
            else
            {
                _delivering = true;
                bool sent = DeliverSegment(DeliveryAudience(scene, *actor, delivery.segment.segment), *actor,
                                           delivery.segment.segment);
                _delivering = false;
                if (!sent)
                {
                    auto key = delivery.observation.eventKey;
                    _storage->Submit([this, key] { return _store.CloseDelivery(key, std::nullopt); });
                    scene.delivery.reset();
                    Abort(scene, "game_chat_rejected");
                }
                else
                {
                    if (actor->guid.IsPlayer() && !actor->human && !scene.background)
                        PlayerbotPopulationMgr::Instance().Interaction(scene.audience.anchor.GetCounter(), actor->guid.GetCounter());
                    delivery.sent = true;
                    scene.conversation.Delivered(delivery.segment.version, delivery.segment.index, _now);
                    scene.transcript.push_back(actor->identity.name + ": " + delivery.segment.segment.text);
                    auto event = delivery.observation;
                    auto witnesses = delivery.witnesses;
                    delivery.recorded = _storage->Submit(
                        [this, event, witnesses]
                        {
                            auto source = _store.Observe(event, witnesses);
                            if (source)
                                _store.CloseDelivery(event.eventKey, source);
                            return source;
                        });
                }
            }
        }
        if (scene.delivery && Ready(scene.delivery->recorded))
        {
            auto source = scene.delivery->recorded.get();
            if (source)
                scene.delivered.emplace(scene.delivery->segment.index, *source);
            else
                Abort(scene);  // Intent survives for recovery; never retransmit an uncertain send.
            scene.delivery.reset();
        }
    }
    if (scene.turn && !scene.cancelled->load() && (!scene.initiativeUsed || scene.requestedPerformance) &&
        !scene.turn->dialogue.actions.empty() && _actions)
    {
        bool ready = true;
        std::vector<ActionRequest> requests;
        for (auto const& proposal : scene.turn->dialogue.actions)
        {
            auto found = scene.initiative.requests.find(proposal.option);
            if (found == scene.initiative.requests.end() ||
                (proposal.afterSegment >= 0 && !scene.delivered.contains(proposal.afterSegment)))
            {
                ready = false;
                break;
            }
            requests.push_back(found->second);
        }
        if (ready && !requests.empty())
        {
            auto guid = requests.front().actor.guid;
            if (scene.requestedPerformance)
            {
                auto const& original = scene.requestedPerformances.at(guid);
                std::sort(requests.begin(), requests.end(),
                          [&](auto const& first, auto const& second)
                          {
                              auto index = [&](auto const& request)
                              {
                                  return std::find_if(original.begin(), original.end(),
                                                      [&](auto const& step) { return step.id == request.id; });
                              };
                              return index(first) < index(second);
                          });
            }
            scene.initiativeUsed = true;
            // Personal initiative cannot supersede a pending human order or race
            // another scene's successful initiative during model latency.
            if (!_actions->Active(guid) && (scene.requestedPerformance || _now >= _nextInitiative[guid]) &&
                _actions->Submit(std::move(requests), _now))
            {
                _actionAudiences[scene.id] = {scene.audience, _now + 3720000};
                if (scene.requestedPerformance)
                    scene.commandedActors.insert(guid);
                else
                    _nextInitiative[guid] = _now + 120000;
            }
            if (scene.requestedPerformance)
            {
                scene.requestedPerformances.erase(guid);
                scene.requestedPerformance = false;
            }
        }
    }
    if (scene.turn && !scene.delivery && !scene.persistence.valid() &&
        (scene.cancelled->load() || scene.conversation.Phase() == ConversationPhase::Persisting))
    {
        // An empty proposal list is a decision to decline this performance.
        // Do not offer the same undecided steps again in a later scene turn.
        if (scene.requestedPerformance)
        {
            if (auto actor = Speaker(scene, scene.chosen))
                scene.requestedPerformances.erase(actor->guid.GetRawValue());
            scene.requestedPerformance = false;
        }
        auto turn = *scene.turn;
        if (scene.cancelled->load())
            std::erase_if(turn.dialogue.notes, [](auto const& note) { return note.scope != "personal"; });
        auto delivered = scene.delivered;
        auto outcome =
            scene.cancelled->load() && delivered.empty() ? "cancelled" : scene.conversation.DeliveryOutcome();
        scene.persistence = _storage->Submit([this, turn, delivered, outcome]
                                             { return _characters->Persist(turn, delivered, outcome, EpochMs()); });
    }
    if (Ready(scene.persistence))
    {
        bool saved = scene.persistence.get();
        scene.previousSpeaker = scene.turn->actor.id;
        // Preserve the initiating message in witnessed history, but never present it as new input again.
        scene.contribution.clear();
        scene.turn.reset();
        scene.chosen.clear();
        if (!saved)
        {
            PBC_Log(PBC_LogLevel::PBC_WARNING, "Character scene {}: could not persist turn by {}.", scene.id,
                    scene.previousSpeaker);
            Abort(scene);
        }
        else
        {
            scene.conversation.Persisted(scene.conversation.Version());
            PBC_Log(PBC_LogLevel::PBC_DEFAULT, "Character scene {}: completed turn by {}, remaining={}, done={}.",
                    scene.id, scene.previousSpeaker, scene.conversation.Remaining(),
                    scene.conversation.Phase() == ConversationPhase::Done);
        }
    }
    if (scene.cancelled->load() && !scene.actionsPending)
        return;
    if (!scene.delivery)
        if (auto segment = scene.conversation.Due(_now))
        {
            auto actor = Speaker(scene, scene.conversation.Actor());
            if (!actor || !AudienceStillValid(scene.audience, *actor))
            {
                Abort(scene, "speaker_or_anchor_unavailable");
                return;
            }
            auto actualAudience = DeliveryAudience(scene, *actor, segment->segment);
            PendingDelivery pending;
            pending.segment = *segment;
            pending.observation.eventKey = scene.turn->requestId + ":segment:" + std::to_string(segment->index);
            pending.observation.sceneId = scene.id;
            pending.observation.authorId = actor->identity.id;
            pending.observation.channel = actualAudience.label;
            pending.observation.evidence = "delivered";
            pending.observation.mapId = actor->mapId;
            pending.observation.instanceId = actor->instanceId;
            pending.observation.zoneId = actor->zoneId;
            pending.observation.createdMs = _epochStart + _now;
            pending.observation.text = actor->identity.name + ": " + segment->segment.text;
            pending.witnesses = Witnesses(actualAudience);
            auto event = pending.observation;
            auto witnesses = pending.witnesses;
            auto version = scene.turn->actor.version;
            pending.prepared = _storage->Submit(
                [this, actualAudience, event, witnesses, version]
                {
                    return StoreActors(actualAudience) &&
                           _store.PrepareDelivery(event.authorId, version, event, witnesses);
                });
            scene.delivery = std::move(pending);
        }
    if ((scene.conversation.Phase() == ConversationPhase::Selecting || scene.actionsPending) &&
        !scene.selection.valid() && !scene.generation.valid() && scene.chosen.empty())
    {
        SelectionState state;
        state.contribution = scene.contribution;
        state.ambient = scene.background;
        state.transcript = scene.transcript;
        state.previousSpeaker = scene.previousSpeaker;
        state.remainingTurns = scene.conversation.Remaining();
        if (scene.actionsPending)
        {
            auto facts = pbc_json::parse(scene.actions.factsJson);
            for (auto& actor : facts["controlled_companions"])
                for (auto const& request : scene.actions.actors)
                    if (actor.value("id", "") ==
                        "player:" + std::to_string(ObjectGuid(request.actor.guid).GetCounter()))
                        actor["pending_gameplay"] = _actions->Active(request.actor.guid);
            state.actionFactsJson = facts.dump();
            state.questions = scene.actions.questions;
            scene.actionsPending = false;
        }
        for (auto const& actor : scene.audience.actors)
            if (actor.canSpeak && AudienceStillValid(scene.audience, actor) && state.candidates.size() < 254)
                state.candidates.push_back({actor.identity.id, actor.identity.name + " (" + actor.identity.kind + ")",
                                            scene.previousSpeaker.empty() && actor.addressed});
        ApiReservation request;
        request.reason = "selector";
        request.sceneId = scene.id;
        request.background = scene.background;
        auto cancelled = scene.cancelled;
        auto anchor = scene.audience.actors.front();
        auto channel = scene.audience.label;
        auto referenceTime = _epochStart + _now;
        scene.selection = _inference->Submit(
            [this, state, request, cancelled, anchor, channel, referenceTime]() mutable
            {
                if (cancelled->load() && state.questions.empty())
                    return ModelReply{};
                auto history = _store.Observations(anchor.identity.id, false, 256);
                if (channel == "say" || channel == "yell" || channel == "emote" || channel == "general")
                    std::erase_if(history,
                                  [&](auto const& observation)
                                  {
                                      return observation.mapId != anchor.mapId ||
                                             observation.instanceId != anchor.instanceId ||
                                             observation.zoneId != anchor.zoneId;
                                  });
                std::map<std::string, std::vector<ObservationRecord>> witnessed;
                for (auto& candidate : state.candidates)
                {
                    if (auto actor = _store.Actor(candidate.id))
                        candidate.privateSelectionCues = BuildSelectionCues(
                            *actor, _store.Notes(candidate.id, 128, true), state.candidates.size() > 5 ? 1200 : 2400);
                    witnessed[candidate.id] = _store.Observations(candidate.id, false, 256);
                }
                state.recentContext = BuildSelectionHistory(history, witnessed, channel, request.sceneId);
                if (!state.questions.empty())
                {
                    auto facts = pbc_json::parse(state.actionFactsJson);
                    facts["recent_references"] = pbc_json::array();
                    for (auto const& reference :
                         BuildActionReferences(history, witnessed, channel, request.sceneId, referenceTime))
                        facts["recent_references"].push_back(
                            {{"text", reference.text}, {"heard_by", reference.heardBy}});
                    state.actionFactsJson = facts.dump();
                }
                if (!_store.Healthy() || (cancelled->load() && state.questions.empty()))
                    return ModelReply{};
                PBC_Log(PBC_LogLevel::PBC_DEFAULT, "Character scene {}: selector history={}, candidates={}.",
                        request.sceneId, state.recentContext.size(), state.candidates.size());
                return _model->Select(state, request);
            });
    }
}

void Runtime::ActionResult(ActionRequest const& request, ActionOutcome const& outcome)
{
    if (request.returnToParty && outcome.status == ActionStatus::Completed)
        PlayerbotDialogueBridge::CompletePreparedPull(request);
    _actionPreparations.erase(request.id);
    for (auto& scene : _scenes)
        if (scene->id == request.groupId)
        {
            scene->actionOutcomes[request.actor.guid][request.id] = {request, outcome};
            if (scene->commandedActors.contains(request.actor.guid) && PlayerbotDialogue::Terminal(outcome.status))
                ++scene->actionVersions[request.actor.guid];
        }
    auto epoch = _epochStart + outcome.occurredMs;
    _observations.push_back(_storage->Submit([this, outcome, epoch] { return _actionStore.Record(outcome, epoch); }));
    auto bot = ObjectAccessor::FindPlayer(ObjectGuid(request.actor.guid));
    auto human = ObjectAccessor::FindPlayer(ObjectGuid(request.requester.guid));
    if (!bot)
        return;
    auto actor = SnapshotActor(bot, _definitions, _realmPhase);
    auto status = outcome.status == ActionStatus::Completed    ? "completed"
                  : outcome.status == ActionStatus::Started    ? "started"
                  : outcome.status == ActionStatus::Cancelled  ? "cancelled"
                  : outcome.status == ActionStatus::Unresolved ? "unresolved"
                                                               : "failed";
    PBC_Log(PBC_LogLevel::PBC_DEFAULT, "Character action {}: actor={}, kind={}, outcome={}, detail={}, at={}.",
            request.id, actor.identity.id, ActionName(request.kind), status, outcome.detail, outcome.occurredMs);
    ObservationRecord event;
    event.eventKey = request.id + ":" + status;
    event.sceneId = request.groupId;
    event.authorId = actor.identity.id;
    event.channel = "action";
    event.evidence = "observed";
    event.mapId = actor.mapId;
    event.instanceId = actor.instanceId;
    event.zoneId = actor.zoneId;
    event.createdMs = _epochStart + outcome.occurredMs;
    event.text = actor.identity.name + " native action " + ActionName(request.kind) + ": " + status + " (" +
                 outcome.detail + "). An established movement order describes behaviour, not arrival.";
    if (outcome.status == ActionStatus::Completed &&
        (request.kind == ActionKind::Attack || request.kind == ActionKind::Pull || request.kind == ActionKind::Assist ||
         request.kind == ActionKind::Protect || request.kind == ActionKind::PetAttack))
        event.text += " Combat engagement began; this does not mean the opponent died or the fight ended.";
    if (outcome.status == ActionStatus::Completed &&
        (request.kind == ActionKind::ShareQuest || request.kind == ActionKind::Invite))
        event.text += " Only an offer was sent. Acceptance and membership/progress require separate native evidence.";
    if (outcome.status == ActionStatus::Completed && request.kind == ActionKind::OfferSupplies)
        event.text += " Only " + std::to_string(request.quantity) + " x " + request.items.front().name +
                      " were offered to the requester. Nothing was transferred; acceptance is still required.";
    if (outcome.status == ActionStatus::Completed && request.kind == ActionKind::Duel)
        event.text += " The human accepted a native duel. No victory or defeat is established.";
    if (outcome.status == ActionStatus::Completed && request.kind == ActionKind::QuestHelp)
        event.text += " Native assistance was enabled; no quest objective or reward has been granted.";
    if (outcome.status == ActionStatus::Completed && request.kind == ActionKind::AcceptQuest)
        event.text +=
            " A quest was accepted. This does not establish that any objective was fulfilled or reward earned.";
    if (outcome.status == ActionStatus::Completed && request.kind == ActionKind::Use)
        event.text += " The native item spell was released; downstream effects are separate observations.";
    if (!request.moneyBudget.empty())
        event.text +=
            outcome.status == ActionStatus::Unresolved
                ? " The currency reservation remains unresolved; no refund or completed purchase is established."
                : " Observed native expense: " + std::to_string(outcome.spentCopper) + " copper.";
    std::vector<std::string> witnesses{actor.identity.id};
    if (HasHumanConnection(human) && human->IsInMap(bot))
    {
        witnesses.push_back("player:" + std::to_string(human->GetGUID().GetCounter()));
        if (PlayerbotDialogue::Terminal(outcome.status))
        {
            bool conversationalFailure = false;
            if (outcome.status == ActionStatus::Failed || outcome.status == ActionStatus::Unresolved)
                if (auto origin = _actionAudiences.find(request.groupId); origin != _actionAudiences.end())
                {
                    _actionNotices.emplace_back(
                        origin->second.first, actor.identity.id,
                        "The requested " + ActionName(request.kind) + " action " + status + ": " + outcome.detail +
                            ". Explain the limitation briefly in ordinary language. Ask a useful question only if "
                            "something the player can clarify would help. Do not claim success or quote internal "
                            "codes. "
                            "An unresolved transfer or expense might have happened; do not offer to retry it blindly.");
                    conversationalFailure = true;
                }
            if (request.kind == ActionKind::OfferSupplies && outcome.status == ActionStatus::Completed)
                ChatHandler(human->GetSession())
                    .PSendSysMessage(
                        "{} offers {} x {}. You can accept or decline in conversation; nothing has been traded.",
                        bot->GetName(), request.quantity, request.items.front().name);
            if (!conversationalFailure)
                ChatHandler(human->GetSession())
                    .PSendSysMessage("{}: {} — {}.", bot->GetName(), ActionName(request.kind), status);
            if (!request.moneyBudget.empty() && outcome.status != ActionStatus::Unresolved)
                ChatHandler(human->GetSession())
                    .PSendSysMessage("{} spent {} gold, {} silver, {} copper.", bot->GetName(),
                                     outcome.spentCopper / 10000, (outcome.spentCopper / 100) % 100,
                                     outcome.spentCopper % 100);
        }
    }
    _observations.push_back(
        _storage->Submit([this, event, witnesses] { return _store.Observe(event, witnesses).has_value(); }));
    bool visibleCompletion =
        outcome.status == ActionStatus::Completed &&
        (request.kind == ActionKind::Approach || request.kind == ActionKind::Regroup ||
         request.kind == ActionKind::Retreat || request.kind == ActionKind::Attack ||
         request.kind == ActionKind::Pull || request.kind == ActionKind::Assist ||
         request.kind == ActionKind::Protect || request.kind == ActionKind::Buff || request.kind == ActionKind::Heal ||
         request.kind == ActionKind::Cleanse || request.kind == ActionKind::Resurrect ||
         request.kind == ActionKind::Equip || request.kind == ActionKind::Use || request.kind == ActionKind::Trade ||
         request.kind == ActionKind::Duel || request.kind == ActionKind::Unequip ||
         request.kind == ActionKind::RestoreEquipment || request.kind == ActionKind::Dance ||
         request.kind == ActionKind::StopDance || request.kind == ActionKind::Formation);
    if (visibleCompletion && HasHumanConnection(human) && human->IsInMap(bot))
    {
        auto observed = CaptureAudience(human, CHAT_MSG_SAY, nullptr, nullptr, _definitions, _realmPhase, bot);
        Enrich(observed);
        // Action visibility is stricter than hearing chat. A distant party member,
        // wall, phase boundary or hidden actor cannot confer personal experience.
        std::erase_if(observed.actors,
                      [&](auto const& observer)
                      {
                          auto unit = ResolveActor(observer);
                          return !unit || !unit->IsAlive() || !unit->IsInMap(bot) || !unit->InSamePhase(bot) ||
                                 !unit->CanSeeOrDetect(bot) || !unit->IsWithinLOSInMap(bot) ||
                                 !unit->IsWithinDistInMap(bot, 25.0f) ||
                                 std::find(witnesses.begin(), witnesses.end(), observer.identity.id) != witnesses.end();
                      });
        auto publicEvent = event;
        publicEvent.eventKey += ":visible";
        publicEvent.channel = "event";
        publicEvent.text = request.kind == ActionKind::Trade
                               ? actor.identity.name +
                                     " completed a trade. Bystanders cannot see its inventory contents or exact terms."
                               : event.text;
        observed.label = "event";  // Individual visual memory; no automatic institutional rumour.
        if (!observed.actors.empty())
            _observations.push_back(_storage->Submit(
                [this, observed, publicEvent]
                { return StoreActors(observed) && _store.Observe(publicEvent, Witnesses(observed)).has_value(); }));
    }
}

ActionStatus Runtime::PrepareAction(ActionRequest const& request)
{
    if (request.returnToParty)
        for (auto const& companion : request.preparedParty)
        {
            auto actor = ObjectAccessor::FindPlayer(ObjectGuid(companion.actor.guid));
            auto human = ObjectAccessor::FindPlayer(ObjectGuid(request.requester.guid));
            if (!PlayerbotDialogueBridge::Matches(actor, companion.actor) || !actor->IsAlive() || !human ||
                !human->GetGroup() || actor->GetGroup() != human->GetGroup())
                return ActionStatus::Failed;
            auto status = PlayerbotDialogueBridge::PreparationStatus(companion.actor.guid, request.groupId,
                                                                     companion.authorityVersion);
            if (status != ActionStatus::Completed)
                return status;
        }
    auto pending = _actionPreparations.find(request.id);
    if (pending == _actionPreparations.end())
    {
        auto epoch = _epochStart + _now;
        _actionPreparations.emplace(
            request.id,
            _storage->Submit([this, request, epoch] { return _actionStore.Prepare(request, _runId, epoch); }));
        return ActionStatus::Pending;
    }
    if (!Ready(pending->second))
        return ActionStatus::Pending;
    bool prepared = false;
    try
    {
        prepared = pending->second.get();
    }
    catch (std::exception const&)
    {
    }
    _actionPreparations.erase(pending);
    return prepared ? ActionStatus::Completed : ActionStatus::Failed;
}

void Runtime::FailureDialogue(GameAudience audience, std::string actorId, std::string detail)
{
    auto human = ObjectAccessor::FindPlayer(audience.anchor);
    if (!HasHumanConnection(human))
        return;
    auto found = std::find_if(audience.actors.begin(), audience.actors.end(),
                              [&](auto const& actor) { return actor.identity.id == actorId; });
    if (found == audience.actors.end() || !AudienceStillValid(audience, *found))
    {
        ChatHandler(human->GetSession())
            .SendSysMessage(
                "The companion couldn't complete that action and "
                "is no longer available to answer. No successful result was confirmed.");
        return;
    }
    auto unit = ResolveActor(*found);
    if (!unit)
        return;
    *found = SnapshotActor(unit, _definitions, _realmPhase);
    found->canSpeak = true;
    Cancel(actorId);
    auto scene = std::make_unique<Scene>();
    scene->audience = std::move(audience);
    scene->actionClarification = std::move(detail);
    scene->actionFeedback.actors.insert(actorId);
    scene->actionFeedback.status = "native_failure";
    scene->conversation.SetSpacing(_spacing);
    scene->conversation.SetReadingWordsPerMinute(_readingWordsPerMinute);
    scene->conversation.Begin(Contribution::Human, true, {actorId}, 1, _now);
    auto observed = scene->audience;
    scene->admitted = _storage->Submit([this, observed] { return StoreActors(observed); });
    ModelReply selection;
    selection.success = true;
    selection.text = actorId;
    std::promise<ModelReply> ready;
    scene->selection = ready.get_future();
    ready.set_value(std::move(selection));
    _scenes.push_back(std::move(scene));
}

void Runtime::Update(uint32_t diff)
{
    _now += diff;
    std::erase_if(_pendingActions,
                  [&](auto const& entry)
                  {
                      return _now >= entry.second.expiresMs ||
                             !HasHumanConnection(ObjectAccessor::FindPlayer(ObjectGuid(entry.first)));
                  });
    std::erase_if(_recentCombatOrders,
                  [&](auto const& entry)
                  {
                      return _now >= entry.second.expiresMs ||
                             !HasHumanConnection(ObjectAccessor::FindPlayer(ObjectGuid(entry.first)));
                  });
    PlayerbotDialogueBridge::SetTime(_now);
    if (_actions)
    {
        for (auto const& outcome : PlayerbotDialogueBridge::Drain())
            _actions->Receive(outcome);
        _actions->Update(_now);
    }
    std::erase_if(_actionAudiences, [&](auto const& entry) { return _now >= entry.second.second; });
    auto notices = std::move(_actionNotices);
    _actionNotices.clear();
    for (auto& [audience, actor, detail] : notices)
        FailureDialogue(std::move(audience), std::move(actor), std::move(detail));
    std::vector<WorldEventInput> events;
    {
        std::lock_guard lock(_worldEventsMutex);
        events.swap(_worldEvents);
    }
    for (auto const& event : events)
        if (auto subject = ObjectAccessor::FindPlayer(event.subject))
        {
            RecordWorldEvent(subject, event.text, event.partyOnly, event.interruptDialogue);
            if (event.enemy.guid)
                CombatOpening(subject, event.enemy);
        }
    std::erase_if(_combatOpened,
                  [](auto const& guid)
                  {
                      auto human = ObjectAccessor::FindPlayer(guid);
                      if (!HasHumanConnection(human) || !human->IsInWorld() || !human->IsAlive())
                          return true;
                      if (human->IsInCombat())
                          return false;
                      if (auto group = human->GetGroup())
                          for (auto member = group->GetFirstMember(); member; member = member->next())
                              if (auto player = member->GetSource(); player && player->IsInCombat() &&
                                                                     human->InSamePhase(player) &&
                                                                     human->IsWithinDistInMap(player, 40.0f))
                                  return false;
                      return true;
                  });
    std::erase_if(_combatAttempted,
                  [](auto const& entry)
                  {
                      auto const& bound = entry.second;
                      GameActor actor;
                      actor.guid = ObjectGuid(bound.guid);
                      actor.mapId = bound.map;
                      actor.instanceId = bound.instance;
                      auto enemy = ResolveActor(actor);
                      return !enemy || !enemy->IsInCombat() || !PlayerbotDialogueBridge::Matches(enemy, bound);
                  });
    std::erase_if(_observations,
                  [](auto& pending)
                  {
                      if (!Ready(pending))
                          return false;
                      try
                      {
                          if (!pending.get())
                              PBC_Log(PBC_LogLevel::PBC_ERROR, "World observation could not be stored.");
                      }
                      catch (std::exception const&)
                      {
                          PBC_Log(PBC_LogLevel::PBC_ERROR, "World observation storage failed.");
                      }
                      return true;
                  });
    for (auto it = _commands.begin(); it != _commands.end();)
    {
        if (!Ready(it->text))
        {
            ++it;
            continue;
        }
        std::string message;
        try
        {
            message = it->text.get();
        }
        catch (std::exception const&)
        {
            message = "Character command failed; no successful change was confirmed.";
        }
        if (auto player = ObjectAccessor::FindPlayer(it->player))
            ChatHandler(player->GetSession()).SendSysMessage(message);
        it = _commands.erase(it);
    }
    for (auto& scene : _scenes)
    {
        try
        {
            Tick(*scene);
        }
        catch (std::exception const&)
        {
            scene->actionsPending = false;
            scene->actions.questions.clear();
            Abort(*scene);
            PBC_Log(PBC_LogLevel::PBC_ERROR, "Character scene failed; outstanding billing holds remain reserved.");
        }
    }
    std::erase_if(_scenes,
                  [](auto const& scene)
                  {
                      return scene->conversation.Phase() == ConversationPhase::Done && !scene->actionsPending &&
                             !scene->admitted.valid() && !scene->selection.valid() && !scene->generation.valid() &&
                             !scene->persistence.valid() && !scene->turn && !scene->delivery;
                  });
    try
    {
        Background();
    }
    catch (std::exception const&)
    {
        PBC_Log(PBC_LogLevel::PBC_ERROR, "Character background processing failed; retained evidence will be retried.");
    }
}

void Runtime::WorldEvent(Player* subject, std::string const& text, bool partyOnly, bool interruptDialogue, Unit* enemy)
{
    if (!subject || text.empty())
        return;
    // Combat hooks may run in map update threads. Only immutable input crosses
    // into the coordinator; resolve the live object on the next world update.
    std::lock_guard lock(_worldEventsMutex);
    _worldEvents.push_back({subject->GetGUID(), text, partyOnly, interruptDialogue,
                            enemy ? PlayerbotDialogueBridge::Snapshot(enemy) : PlayerbotDialogue::Entity{}});
}

void Runtime::RecordWorldEvent(Player* subject, std::string const& text, bool partyOnly, bool interruptDialogue)
{
    if (!subject || !subject->IsInWorld() || text.empty())
        return;
    if (interruptDialogue)
    {
        auto actor = "player:" + std::to_string(subject->GetGUID().GetCounter());
        // Witnesses can fight nearby without cancelling someone else's reply.
        // A selector still in flight rechecks the chosen speaker before generation.
        for (auto& scene : _scenes)
            if (!scene->audience.combat && !scene->audience.duringCombat &&
                (scene->audience.anchor == subject->GetGUID() || scene->chosen == actor))
                Abort(*scene, "participant_combat:" + actor);
    }
    Player* anchor = nullptr;
    for (auto const& [id, session] : sWorldSessionMgr->GetAllSessions())
        if (auto human = session->GetPlayer(); HasHumanConnection(human) && human->IsInWorld() &&
                                               human->IsWithinDistInMap(subject, 25.0f) &&
                                               human->CanSeeOrDetect(subject) && human->IsWithinLOSInMap(subject))
        {
            anchor = human;
            break;
        }
    if (!anchor)
        return;
    auto audience = CaptureAudience(anchor, CHAT_MSG_SAY, nullptr, nullptr, _definitions, _realmPhase, subject);
    Enrich(audience);
    if (partyOnly)
        std::erase_if(audience.actors,
                      [&](auto const& actor)
                      {
                          if (actor.guid == subject->GetGUID())
                              return false;
                          auto group = subject->GetGroup();
                          return !group || !actor.guid.IsPlayer() || !group->IsMember(actor.guid);
                      });
    if (audience.actors.empty())
        return;
    // Observations never force a model call. Nearby individuals can remember them
    // on their next reply or at the existing five-minute/token extraction trigger.
    ObservationRecord event;
    event.eventKey = Identifier();
    event.sceneId = event.eventKey;
    event.authorId = "player:" + std::to_string(subject->GetGUID().GetCounter());
    event.channel = "event";
    event.mapId = subject->GetMapId();
    event.instanceId = subject->GetInstanceId();
    event.zoneId = subject->GetZoneId();
    event.createdMs = _epochStart + _now;
    event.text = text;
    // Quest details are party knowledge, never a bystander's institutional report.
    if (partyOnly)
        audience.label = "party";
    _observations.push_back(
        _storage->Submit([this, audience, event]
                         { return StoreActors(audience) && _store.Observe(event, Witnesses(audience)).has_value(); }));
}

void Runtime::CombatOpening(Player* subject, PlayerbotDialogue::Entity const& bound)
{
    if (!_combatEnabled || !_combatChance || _now < _nextCombat || _combatAttempted.contains(bound.guid))
        return;
    auto enemy = ObjectAccessor::GetUnit(*subject, ObjectGuid(bound.guid));
    if (!enemy || !PlayerbotDialogueBridge::Matches(enemy, bound))
        return;
    GameAudience audience;
    for (auto const& [id, session] : sWorldSessionMgr->GetAllSessions())
        if (auto human = session->GetPlayer(); HasHumanConnection(human) && human->IsInWorld() &&
                                               human->IsWithinDistInMap(subject, 40.0f) &&
                                               !_combatOpened.contains(human->GetGUID()))
        {
            audience = CaptureCombatAudience(human, enemy, _definitions, _realmPhase);
            if (!audience.actors.empty())
                break;
        }
    Enrich(audience);
    if (audience.actors.empty())
        return;                            // An unobserved simulation fight never starts inference.
    _combatAttempted[bound.guid] = bound;  // One chance per encounter, not per entrant/swing.
    if (!CombatSpeechQuiet(audience) || std::uniform_int_distribution<uint32_t>(1, 100)(_random) > _combatChance)
        return;
    for (auto const& existing : _scenes)
        if (!existing->cancelled->load() && existing->audience.anchor == audience.anchor)
            return;  // Do not interrupt a player's active exchange for background chatter.
    std::set<std::string> eligible;
    for (auto const& actor : audience.actors)
        if (actor.canSpeak)
            eligible.insert(actor.identity.id);
    if (eligible.empty())
        return;
    auto scene = std::make_unique<Scene>();
    scene->audience = std::move(audience);
    scene->background = true;
    scene->expiresMs = _now + 30000;
    scene->contribution = "Combat encounter: " + enemy->GetName() +
                          " is fighting the nearby adventurers. An optional short reaction may fit. "
                          "The outcome is unknown. No more than two public turns; silence is valid.";
    scene->conversation.SetSpacing(_spacing);
    scene->conversation.SetReadingWordsPerMinute(_readingWordsPerMinute);
    scene->conversation.Begin(Contribution::Ambient, true, std::move(eligible), 2, _now);
    ObservationRecord event;
    event.eventKey = scene->id + ":combat";
    event.sceneId = scene->id;
    event.authorId = "world";
    event.channel = "event";
    event.mapId = subject->GetMapId();
    event.instanceId = subject->GetInstanceId();
    event.zoneId = subject->GetZoneId();
    event.createdMs = _epochStart + _now;
    event.text = enemy->GetName() + " is fighting the nearby adventurers. The encounter outcome is unknown.";
    auto observed = scene->audience;
    scene->admitted =
        _storage->Submit([this, observed, event]
                         { return StoreActors(observed) && _store.Observe(event, Witnesses(observed)).has_value(); });
    PBC_Log(PBC_LogLevel::PBC_DEFAULT, "Character combat scene {}: enemy={}, allowance=2.", scene->id, bound.entry);
    for (auto const& actor : scene->audience.actors)
        if (actor.human)
            _combatOpened.insert(actor.guid);
    _nextCombat = _now + _combatCooldown;  // Shared across companions/NPCs and human observers.
    _scenes.push_back(std::move(scene));
}

void Runtime::Background()
{
    if (Ready(_memory))
    {
        bool success = false;
        try
        {
            success = _memory.get();
        }
        catch (std::exception const&)
        {
            PBC_Log(PBC_LogLevel::PBC_ERROR, "Deferred character memory failed; source records remain pending.");
        }
        if (!success)
            _memoryRetryAfter[_activeMemoryActor] = std::numeric_limits<uint64_t>::max();
        // A failed unchanged extraction is not retried indefinitely while nobody
        // is playing. New human evidence permits a new attempt; original rows stay.
        _activeMemoryActor.clear();
    }
    if (Ready(_pendingMemories))
        _memoryActors = _pendingMemories.get();
    if (!_pendingMemories.valid() && _now >= _nextMemoryScan)
    {
        _nextMemoryScan = _now + 30000;
        auto epoch = _epochStart + _now;
        _pendingMemories =
            _storage->Submit([this, epoch] { return _store.PendingActors(epoch, _memoryInactiveMs, _memoryTokens); });
    }
    if (!_memory.valid())
        for (auto it = _memoryActors.begin(); it != _memoryActors.end(); ++it)
        {
            auto actor = *it;
            if (_memoryRetryAfter[actor] > _now)
                continue;
            bool active =
                std::any_of(_scenes.begin(), _scenes.end(),
                            [&](auto const& scene)
                            {
                                auto witnesses = Witnesses(scene->audience);
                                return std::find(witnesses.begin(), witnesses.end(), actor) != witnesses.end();
                            });
            if (active)
                continue;
            _memoryActors.erase(it);
            _memoryRetryAfter[actor] = _now + 300000;
            _activeMemoryActor = actor;
            _memory = _inference->Submit(
                [this, actor]
                {
                    CharacterInput input;
                    input.actorId = actor;
                    input.gameFactsJson =
                        pbc_json{{"realm_phase", _realmPhase}, {"current_presence", "unknown"}}.dump();
                    input.request.reason = "memory";
                    input.request.background = true;
                    return _characters->Remember(input, EpochMs()) && _characters->Compact(actor, input.request);
                });
            break;
        }
    if (_ambientEnabled && _now >= _nextAmbient)
    {
        _nextAmbient = _now + std::uniform_int_distribution<uint32_t>(_ambientMinimum, _ambientMaximum)(_random);
        if (!_scenes.empty() || _now < _lastHumanActivity + _ambientQuiet)
            return;
        std::vector<Player*> humans;
        for (auto const& [id, session] : sWorldSessionMgr->GetAllSessions())
            if (auto player = session->GetPlayer(); HasHumanConnection(player) && player->IsInWorld() &&
                                                    player->IsAlive() && !player->IsInCombat() && !player->IsFlying())
                humans.push_back(player);
        if (!humans.empty())
        {
            auto anchor = humans[std::uniform_int_distribution<std::size_t>(0, humans.size() - 1)(_random)];
            Chat(anchor, CHAT_MSG_SAY, LANG_UNIVERSAL,
                 "A quiet moment with nearby travellers. An optional brief public opening may address "
                 "a present traveller or companion. No one has said anything yet.",
                 nullptr, nullptr, true);
        }
    }
}

void Runtime::Stop()
{
    if (_actions)
        _actions->CancelAll("runtime_stopped", _now);
    PlayerbotDialogueBridge::Suspend();
    for (auto& scene : _scenes)
        Abort(*scene);
    _inference.reset();  // Cancelled queued jobs skip HTTP; active calls finish their billing reconciliation.
    for (auto& scene : _scenes)
    {
        if (Ready(scene->generation))
        {
            auto turn = scene->generation.get();
            if (turn.success)
                _storage->Submit([this, turn] { return _characters->Persist(turn, {}, "cancelled", EpochMs()); });
        }
        if (scene->turn && !scene->persistence.valid())
        {
            auto turn = *scene->turn;
            std::erase_if(turn.dialogue.notes, [](auto const& note) { return note.scope != "personal"; });
            auto delivered = scene->delivered;
            _storage->Submit(
                [this, turn, delivered]
                {
                    return _characters->Persist(turn, delivered, delivered.empty() ? "cancelled" : "partial",
                                                EpochMs());
                });
        }
    }
    _storage.reset();
    _scenes.clear();
}

std::unique_ptr<Runtime> runtime;
}  // namespace RuntimeDetail

using RuntimeDetail::Runtime;
using RuntimeDetail::runtime;

bool RuntimeConfigured()
{
    return sConfigMgr->GetOption<bool>("PBC.CharacterSystem.Enable", false, false);
}

bool StartRuntime()
{
    auto candidate = std::make_unique<Runtime>();
    if (!candidate->Start())
        return false;
    runtime = std::move(candidate);
    return true;
}

void StopRuntime()
{
    if (runtime)
        runtime->Stop();
    runtime.reset();
}

void UpdateRuntime(uint32_t diff)
{
    if (runtime)
        runtime->Update(diff);
}

void ObserveChat(Player* sender, uint32_t type, uint32_t language, std::string const& text, Player* receiver,
                 Channel* channel)
{
    if (runtime)
        runtime->Chat(sender, type, language, text, receiver, channel);
}

void ObserveEmote(Player* sender, uint32_t textEmote, ObjectGuid target)
{
    if (!runtime || !sender || !sEmotesTextStore.LookupEntry(textEmote))
        return;
    std::string text;
    text = NativeEmoteName(textEmote);
    if (auto unit = ObjectAccessor::GetUnit(*sender, target))
        if (auto creature = unit->ToCreature(); creature && HasNativeEmoteScript(creature, textEmote))
            return;  // Preserve native quest/emote scripting; do not add a competing reaction.
    if (text.empty())
        text = "makes a gesture";
    if (auto unit = ObjectAccessor::GetUnit(*sender, target))
        text += " toward " + unit->GetName();
    runtime->Chat(sender, CHAT_MSG_EMOTE, LANG_UNIVERSAL, text, nullptr, nullptr, false, target, true);
}

void CancelActor(std::string const& actorId)
{
    if (runtime)
        runtime->Cancel(actorId);
}

void ObserveWorldEvent(Player* subject, std::string const& text, bool partyOnly, bool interruptDialogue, Unit* enemy)
{
    // A bot being created gains quests and levels instantly; that is not something anyone saw happen.
    if (runtime && !PlayerbotDialogueBridge::IsSettingUp(subject))
        runtime->WorldEvent(subject, text, partyOnly, interruptDialogue, enemy);
}

bool CharacterCommand(Player* player, std::string const& command)
{
    return runtime && runtime->Command(player, command);
}
}  // namespace PBC
