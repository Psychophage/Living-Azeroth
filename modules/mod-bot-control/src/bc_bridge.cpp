// SPDX-License-Identifier: GPL-2.0-or-later
#include "bc_bridge.h"

#include "Chat.h"
#include "Config.h"
#include "Group.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotMgr.h"
#include "PlayerbotSecurity.h"
#include "Playerbots.h"
#include "AiFactory.h"
#include "CharacterCache.h"
#include "ChangeStrategyAction.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "Formations.h"
#include "PopulationMgr.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "pbc_runtime.h"

#include <algorithm>
#include <set>

namespace BotControl
{
namespace
{
constexpr int Protocol = 1;
constexpr uint32 WatchIntervalMs = 1000;
constexpr std::size_t MaxWho = 40;

// Orders the addon can give, each one or more Playerbots chat commands. "guard" holds the
// player's current spot; "rest" eats and drinks when the bot needs it.
std::vector<std::string> const Actions = {"follow", "stay", "guard", "attack", "pull", "flee", "rest"};

// Tactic switches, each a Playerbots strategy in combat, out of combat, or both. A bot only
// offers the switches its class has (not every class has area attacks).
struct Switch
{
    char const* name;
    char const* strategy;
    bool combat;
    bool nonCombat;
};
std::vector<Switch> const Switches = {
    {"passive", "passive", true, true},     // stay out of fights
    {"loot", "loot", false, true},          // pick up loot after fights
    {"join", "join attack", false, true},   // join in when the master starts attacking
    {"aoe", "aoe", true, false},            // area attacks
    {"behind", "behind", true, false},      // melee keeps out of the frontal arc
    {"threat", "threat", true, false},      // hold damage while the tank builds threat
    {"avoid_aoe", "avoid aoe", true, false},
    {"potions", "potions", true, false},
    {"run", "flee", true, false},           // run when outmatched or nearly dead
    {"save_mana", "save mana", true, false},
    {"gather", "gather", false, true},      // herbs and ore
    {"food", "food", false, true},          // eat and drink after fights
    {"mount", "mount", false, true},        // mount when the master mounts
};

std::vector<std::string> const Formations = {"near", "far", "arrow", "queue", "circle", "line", "shield", "melee",
                                             "chaos"};

Switch const* FindSwitch(std::string const& name)
{
    for (Switch const& candidate : Switches)
        if (name == candidate.name)
            return &candidate;
    return nullptr;
}

bool IsBot(Player* player)
{
    return player && GET_PLAYERBOT_AI(player) && !IsSelfBot(player);
}

bool Commandable(Player* viewer, Player* bot)
{
    PlayerbotAI* ai = GET_PLAYERBOT_AI(bot);
    return ai && ai->GetSecurity()->LevelFor(viewer, nullptr, false) >= PLAYERBOT_SECURITY_ALLOW_ALL;
}

// A switch is on when every engine it belongs to has its strategy, off when none has.
bool SwitchOn(PlayerbotAI* ai, Switch const& switch_)
{
    return (!switch_.combat || ai->HasStrategy(switch_.strategy, BOT_STATE_COMBAT)) &&
           (!switch_.nonCombat || ai->HasStrategy(switch_.strategy, BOT_STATE_NON_COMBAT));
}

bool SwitchOff(PlayerbotAI* ai, Switch const& switch_)
{
    return (!switch_.combat || !ai->HasStrategy(switch_.strategy, BOT_STATE_COMBAT)) &&
           (!switch_.nonCombat || !ai->HasStrategy(switch_.strategy, BOT_STATE_NON_COMBAT));
}

// The strategies a class has do not change, so they are asked for once per class.
bool Supports(Player* bot, char const* strategy)
{
    static std::unordered_map<uint8, std::set<std::string>> byClass;
    auto found = byClass.find(bot->getClass());
    if (found == byClass.end())
        found = byClass.emplace(bot->getClass(),
                                GET_PLAYERBOT_AI(bot)->GetAiObjectContext()->GetSupportedStrategies()).first;
    return found->second.count(strategy) != 0;
}

std::string FormationOf(PlayerbotAI* ai)
{
    Formation* formation = ai->GetAiObjectContext()->GetValue<Formation*>("formation")->Get();
    return formation ? formation->getName() : "";
}

// Whether eating or drinking would do anything: below full health, or a mana user below full mana.
bool NeedsRest(Player* bot)
{
    return bot->GetHealthPct() < 100.0f ||
           (bot->getPowerType() == POWER_MANA && bot->GetPower(POWER_MANA) < bot->GetMaxPower(POWER_MANA));
}

std::string Standing(PlayerbotAI* ai)
{
    if (ai->HasStrategy("guard", BOT_STATE_NON_COMBAT))
        return "guard";
    if (ai->HasStrategy("stay", BOT_STATE_NON_COMBAT))
        return "stay";
    if (ai->HasStrategy("follow", BOT_STATE_NON_COMBAT))
        return "follow";
    return "free";
}

pbc_json Describe(Player* viewer, Player* bot)
{
    PlayerbotAI* ai = GET_PLAYERBOT_AI(bot);
    std::string role = PlayerbotAI::IsTank(bot) ? "tank" : PlayerbotAI::IsHeal(bot) ? "healer" : "damage";
    pbc_json switches = pbc_json::object();
    for (Switch const& switch_ : Switches)
        if (Supports(bot, switch_.strategy))
            switches[switch_.name] = SwitchOn(ai, switch_);
    return {
        {"guid", bot->GetGUID().GetCounter()},
        {"name", bot->GetName()},
        {"class", bot->getClass()},
        {"level", bot->GetLevel()},
        {"role", role},
        {"order", Standing(ai)},
        {"switches", switches},
        {"formation", FormationOf(ai)},
        {"resting", bot->IsSitState()},
        {"combat", bot->IsInCombat()},
        {"yours", ai->GetMaster() == viewer},
        {"commandable", Commandable(viewer, bot)},
    };
}

// Bots the player sees in their group, and their own bots wherever they are.
std::vector<Player*> BotsFor(Player* player)
{
    std::vector<Player*> bots;
    std::set<ObjectGuid> seen;
    auto add = [&](Player* candidate)
    {
        if (candidate && candidate != player && candidate->IsInWorld() && IsBot(candidate) &&
            seen.insert(candidate->GetGUID()).second)
            bots.push_back(candidate);
    };
    if (Group* group = player->GetGroup())
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            add(ref->GetSource());
    if (PlayerbotMgr* mgr = GET_PLAYERBOT_MGR(player))
        for (auto it = mgr->GetPlayerBotsBegin(); it != mgr->GetPlayerBotsEnd(); ++it)
            add(it->second);
    return bots;
}

// The Playerbots chat commands for an order (switches change strategies directly, see Order);
// "formation:<name>" sets a formation.
std::vector<std::string> Commands(Player* player, std::string const& order)
{
    if (order == "guard")
        return {"position guard " + std::to_string(int32(player->GetPositionX())) + "," +
                    std::to_string(int32(player->GetPositionY())) + "," + std::to_string(int32(player->GetPositionZ())),
                "nc +guard,-follow,-stay"};
    if (order.starts_with("formation:"))
        return {"formation " + order.substr(10)};
    if (order == "rest" || FindSwitch(order))
        return {};
    return {order};
}

// The bot has acted on the order once its own state shows it.
bool Done(Player* bot, std::string const& order, bool on)
{
    PlayerbotAI* ai = GET_PLAYERBOT_AI(bot);
    if (order == "follow" || order == "stay" || order == "guard")
        return Standing(ai) == order;
    if (order == "flee")
        return Standing(ai) == "follow" && SwitchOn(ai, *FindSwitch("passive"));
    if (order == "rest")
        return bot->IsSitState();
    if (order.starts_with("formation:"))
        return FormationOf(ai) == order.substr(10);
    if (Switch const* switch_ = FindSwitch(order))
        return on ? SwitchOn(ai, *switch_) : SwitchOff(ai, *switch_);
    return bot->IsInCombat() || bot->GetVictim();
}

std::string ZoneName(uint32 zone)
{
    auto area = sAreaTableStore.LookupEntry(zone);
    return area && area->area_name[LOCALE_enUS] ? area->area_name[LOCALE_enUS] : "";
}

bool SameGroup(Player* a, Player* b)
{
    return a->GetGroup() && a->GetGroup() == b->GetGroup();
}

// Playerbots' own bot-management command, as `.playerbots bot <command>` runs it.
std::vector<std::string> BotCommand(Player* player, std::string const& command)
{
    PlayerbotMgr* mgr = GET_PLAYERBOT_MGR(player);
    if (!mgr)
        return {"Playerbots is not available"};
    std::vector<char> text(command.begin(), command.end());
    text.push_back('\0');
    return mgr->HandlePlayerbotCommand(text.data(), player);
}

std::string Base36(uint32 value)
{
    static char const digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    std::string text;
    do
    {
        text.insert(text.begin(), digits[value % 36]);
        value /= 36;
    } while (value);
    return text;
}
}

Bridge& Bridge::Instance()
{
    static Bridge bridge;
    return bridge;
}

void Bridge::LoadConfig()
{
    _enabled = sConfigMgr->GetOption<bool>("BotControl.Enable", false);
    _timeoutMs = sConfigMgr->GetOption<uint32>("BotControl.OrderTimeoutMs", 10000);
    if (_enabled)
        LOG_INFO("module", "Bot control: addon bridge on (protocol {}).", Protocol);
}

bool Bridge::Receive(Player* player, std::string const& text)
{
    if (!_enabled || !player || text.size() <= Prefix.size() + 1 || text.compare(0, Prefix.size(), Prefix) != 0 ||
        text[Prefix.size()] != '\t')
        return false;
    auto frame = ParseFrame(std::string_view(text).substr(Prefix.size() + 1));
    if (!frame)
        return true;
    std::string message;
    auto result = _assemblers[player->GetGUID()].Add(*frame, message);
    if (result == Assembler::Result::Rejected)
        Fail(player, nullptr, "too_large");
    if (result != Assembler::Result::Complete)
        return true;
    pbc_json request = pbc_json::parse(message, nullptr, false);
    if (request.is_discarded() || !request.is_object())
    {
        Fail(player, nullptr, "bad_request");
        return true;
    }
    Handle(player, request);
    return true;
}

void Bridge::Handle(Player* player, pbc_json const& request)
{
    pbc_json id = request.contains("id") ? request["id"] : pbc_json();
    if (!request.contains("op") || !request["op"].is_string())
        return Fail(player, id, "bad_request");
    std::string const op = request["op"].get<std::string>();
    if (op == "hello")
        return Hello(player, id);
    if (op == "bots")
        return Bots(player, id);
    if (op == "who")
        return Who(player, request);
    if (op == "order")
        return Order(player, request);
    if (op == "hearing")
        return Hearing(player, request);
    if (op == "guild")
        return Guild(player, request);
    if (op == "rumours")
        return Rumours(player, request);
    if (op == "why")
        return Why(player, request);
    if (op == "budget")
        return Budget(player, request);
    if (op == "roster")
        return Roster(player, request);
    if (op == "inspect")
        return Inspect(player, request);
    if (op == "bring" || op == "dismiss")
        return Bring(player, request, op == "bring");
    Fail(player, id, "unknown_op");
}

void Bridge::Hello(Player* player, pbc_json const& id)
{
    // From now on this player hears about changes to their bots.
    _watching[player->GetGUID()].clear();
    pbc_json switches = pbc_json::array();
    for (Switch const& switch_ : Switches)
        switches.push_back(switch_.name);
    Reply(player, id, {{"protocol", Protocol}, {"orders", Actions}, {"switches", switches}, {"formations", Formations}});
}

void Bridge::Bots(Player* player, pbc_json const& id)
{
    pbc_json bots = pbc_json::array();
    for (Player* bot : BotsFor(player))
        bots.push_back(Describe(player, bot));
    Reply(player, id, {{"bots", bots}});
}

void Bridge::Who(Player* player, pbc_json const& request)
{
    pbc_json id = request.contains("id") ? request["id"] : pbc_json();
    if (!request.contains("guids") || !request["guids"].is_array() || request["guids"].size() > MaxWho)
        return Fail(player, id, "bad_request");
    pbc_json bots = pbc_json::array();
    for (auto const& value : request["guids"])
    {
        if (!value.is_number_unsigned())
            return Fail(player, id, "bad_request");
        Player* bot = ObjectAccessor::FindPlayerByLowGUID(value.get<ObjectGuid::LowType>());
        if (IsBot(bot))
            bots.push_back({{"guid", value},
                            {"yours", GET_PLAYERBOT_AI(bot)->GetMaster() == player},
                            {"commandable", Commandable(player, bot)}});
    }
    Reply(player, id, {{"bots", bots}});
}

void Bridge::Order(Player* player, pbc_json const& request)
{
    pbc_json id = request.contains("id") ? request["id"] : pbc_json();
    if (!request.contains("bot") || !request["bot"].is_number_unsigned() || !request.contains("order") ||
        !request["order"].is_string() || (request.contains("on") && !request["on"].is_boolean()))
        return Fail(player, id, "bad_request");
    std::string order = request["order"].get<std::string>();
    Switch const* switch_ = FindSwitch(order);
    if (order == "formation")
    {
        if (!request.contains("formation") || !request["formation"].is_string() ||
            std::find(Formations.begin(), Formations.end(), request["formation"].get<std::string>()) == Formations.end())
            return Fail(player, id, "unknown_formation");
        order += ":" + request["formation"].get<std::string>();
    }
    else if (!switch_ && std::find(Actions.begin(), Actions.end(), order) == Actions.end())
        return Fail(player, id, "unknown_order");
    Player* bot = ObjectAccessor::FindPlayerByLowGUID(request["bot"].get<ObjectGuid::LowType>());
    if (!IsBot(bot) || !bot->IsInWorld())
        return Fail(player, id, "unknown_bot");
    if (!Commandable(player, bot))
        return Fail(player, id, "not_yours");
    if (switch_ && !Supports(bot, switch_->strategy))
        return Fail(player, id, "unsupported");

    PlayerbotAI* ai = GET_PLAYERBOT_AI(bot);
    bool on = request.contains("on") ? request["on"].get<bool>() : switch_ ? !SwitchOn(ai, *switch_) : true;
    if (order == "rest")
    {
        if (bot->IsInCombat())
            return Send(player, {{"re", id}, {"ok", false}, {"error", "refused"}, {"reason", "in combat"}});
        if (!NeedsRest(bot))
            return Send(player, {{"re", id}, {"ok", false}, {"error", "refused"}, {"reason", "not needed"}});
        // Eating and drinking have no chat command of their own; they are the actions the food strategy uses.
        std::string why;
        bool started = false;
        auto act = [&](char const* name, char const* verb)
        {
            Action* action = ai->GetAiObjectContext()->GetAction(name);
            if (action && action->isUseful() && action->isPossible() && action->Execute(Event()))
                started = true;
            else
                why = std::string("couldn't ") + verb;
        };
        if (bot->GetHealthPct() < 100.0f)
            act("food", "eat");
        if (bot->getPowerType() == POWER_MANA && bot->GetPower(POWER_MANA) < bot->GetMaxPower(POWER_MANA))
            act("drink", "drink");
        if (!started)
            return Send(player, {{"re", id}, {"ok", false}, {"error", "refused"}, {"reason", why}});
    }
    if (switch_)
    {
        // Directly, as the co and nc commands would: queued as chat commands, a second change waiting behind
        // the first replaces it, and a tactic set sends many at once.
        std::string change = std::string(on ? "+" : "-") + switch_->strategy;
        std::string refusal;
        if ((switch_->combat && !ApplyStrategyChange(ai, change, BOT_STATE_COMBAT, refusal)) ||
            (switch_->nonCombat && !ApplyStrategyChange(ai, change, BOT_STATE_NON_COMBAT, refusal)))
            return Send(player, {{"re", id}, {"ok", false}, {"error", "refused"}, {"reason", refusal}});
    }
    for (auto const& command : Commands(player, order))
        ai->HandleCommand(CHAT_MSG_WHISPER, sPlayerbotAIConfig.commandPrefix + command, player);
    _pending.push_back({player->GetGUID(), bot->GetGUID(), id, order, on, 0});
}

// What the player hears from characters; the character system owns the settings and their meaning.
void Bridge::Hearing(Player* player, pbc_json const& request)
{
    pbc_json id = request.contains("id") ? request["id"] : pbc_json();
    std::string json;
    std::string error;
    if (request.contains("change"))
    {
        if (!request["change"].is_object())
            return Fail(player, id, "bad_request");
        if (!PBC::ChangeListenerSettings(player, request["change"].dump(), json, error))
            return Send(player, {{"re", id}, {"ok", false}, {"error", "refused"}, {"reason", error}});
    }
    else if (!PBC::ListenerSettingsFor(player, json))
        return Fail(player, id, "unavailable");
    Reply(player, id, {{"hearing", pbc_json::parse(json)}});
}

// The player's guild and its written identity; the character system owns both and who may edit.
void Bridge::Guild(Player* player, pbc_json const& request)
{
    pbc_json id = request.contains("id") ? request["id"] : pbc_json();
    std::string json;
    std::string error;
    bool done;
    if (request.contains("draft"))
    {
        if (!request["draft"].is_string())
            return Fail(player, id, "bad_request");
        // One model call; the reply carries the draft for the officer to edit, nothing saved.
        ObjectGuid guid = player->GetGUID();
        bool started = PBC::DraftGuildIdentity(player, request["draft"].get<std::string>(), [this, guid, id](std::string answer)
        {
            Player* asker = ObjectAccessor::FindConnectedPlayer(guid);
            if (!asker)
                return;
            auto body = pbc_json::parse(answer, nullptr, false);
            if (body.is_discarded() || !body.value("ok", false))
                return Send(asker, {{"re", id}, {"ok", false}, {"error", "refused"},
                                    {"reason", body.is_discarded() ? "" : body.value("error", "")}});
            Reply(asker, id, {{"draft", body["draft"]}});
        });
        if (!started)
            Fail(player, id, "unavailable");
        return;
    }
    if (request.contains("change"))
    {
        if (!request["change"].is_object())
            return Fail(player, id, "bad_request");
        done = PBC::ChangeGuildIdentity(player, request["change"].dump(), json, error);
    }
    else
        done = PBC::GuildIdentityFor(player, json, error);
    if (!done)
        return Send(player, {{"re", id}, {"ok", false}, {"error", "refused"}, {"reason", error}});
    Reply(player, id, pbc_json::parse(json));
}

// A guild's rumours: listed for members, corrected, resolved or forgotten by officers. The
// character system answers once its store has, so the reply comes a moment later.
void Bridge::Rumours(Player* player, pbc_json const& request)
{
    pbc_json id = request.contains("id") ? request["id"] : pbc_json();
    ObjectGuid guid = player->GetGUID();
    auto answer = [this, guid, id](std::string json)
    {
        Player* asker = ObjectAccessor::FindConnectedPlayer(guid);
        if (!asker)
            return;
        auto body = pbc_json::parse(json, nullptr, false);
        if (body.is_discarded() || !body.is_object())
            return Fail(asker, id, "unavailable");
        if (!body.value("ok", false))
            return Send(asker, {{"re", id}, {"ok", false}, {"error", "refused"}, {"reason", body.value("error", "")}});
        body.erase("ok");
        Reply(asker, id, body);
    };
    bool started;
    if (request.contains("change"))
    {
        if (!request["change"].is_object())
            return Fail(player, id, "bad_request");
        started = PBC::ChangeRumour(player, request["change"].dump(), answer);
    }
    else
    {
        if (request.contains("group") && !request["group"].is_string())
            return Fail(player, id, "bad_request");
        started = PBC::GroupRumours(player, request.value("group", std::string()), answer);
    }
    if (!started)
        Fail(player, id, "unavailable");
}

// The realm's dialogue spending, read-only; the character system decides who may see it.
void Bridge::Budget(Player* player, pbc_json const& request)
{
    pbc_json id = request.contains("id") ? request["id"] : pbc_json();
    ObjectGuid guid = player->GetGUID();
    bool started = PBC::DialogueBudget(player, [this, guid, id](std::string json)
    {
        Player* asker = ObjectAccessor::FindConnectedPlayer(guid);
        if (!asker)
            return;
        auto body = pbc_json::parse(json, nullptr, false);
        if (body.is_discarded() || !body.is_object())
            return Fail(asker, id, "unavailable");
        if (!body.value("ok", false))
            return Send(asker, {{"re", id}, {"ok", false}, {"error", "refused"}, {"reason", body.value("error", "")}});
        body.erase("ok");
        Reply(asker, id, body);
    });
    if (!started)
        Fail(player, id, "unavailable");
}

// The lines characters said to this player, and why one of them was said. The character
// system owns the records and answers once its store has.
void Bridge::Why(Player* player, pbc_json const& request)
{
    pbc_json id = request.contains("id") ? request["id"] : pbc_json();
    if (request.contains("line") && !request["line"].is_number_unsigned())
        return Fail(player, id, "bad_request");
    ObjectGuid guid = player->GetGUID();
    auto answer = [this, guid, id](std::string json)
    {
        Player* asker = ObjectAccessor::FindConnectedPlayer(guid);
        if (!asker)
            return;
        auto body = pbc_json::parse(json, nullptr, false);
        if (body.is_discarded() || !body.is_object())
            return Fail(asker, id, "unavailable");
        if (!body.value("ok", false))
            return Send(asker, {{"re", id}, {"ok", false}, {"error", "refused"}, {"reason", body.value("error", "")}});
        body.erase("ok");
        Reply(asker, id, body);
    };
    if (!PBC::WhyLine(player, request.value("line", uint64_t(0)), answer))
        Fail(player, id, "unavailable");
}

// The player's other characters, and the characters they have come to know best.
void Bridge::Roster(Player* player, pbc_json const& request)
{
    pbc_json id = request.contains("id") ? request["id"] : pbc_json();
    pbc_json characters = pbc_json::array();
    if (QueryResult rows = CharacterDatabase.Query(
            "SELECT guid, name, class, race, level, zone FROM characters WHERE account = {} AND guid <> {} "
            "ORDER BY level DESC LIMIT 50",
            player->GetSession()->GetAccountId(), player->GetGUID().GetCounter()))
        do
        {
            Field* row = rows->Fetch();
            Player* online = ObjectAccessor::FindPlayerByLowGUID(row[0].Get<uint32>());
            characters.push_back({{"guid", row[0].Get<uint32>()},
                                  {"name", row[1].Get<std::string>()},
                                  {"class", row[2].Get<uint8>()},
                                  {"race", row[3].Get<uint8>()},
                                  {"level", online ? online->GetLevel() : row[4].Get<uint8>()},
                                  {"zone", ZoneName(online ? online->GetZoneId() : row[5].Get<uint16>())},
                                  {"in_world", online != nullptr},
                                  {"in_party", online && SameGroup(online, player)}});
        } while (rows->NextRow());
    pbc_json companions = pbc_json::array();
    for (auto const& [guid, familiarity] : PlayerbotPopulationMgr::Instance().FamiliarTo(player->GetGUID().GetCounter(), 12))
    {
        CharacterCacheEntry const* entry = sCharacterCache->GetCharacterCacheByGuid(ObjectGuid::Create<HighGuid::Player>(guid));
        if (!entry)
            continue;
        Player* online = ObjectAccessor::FindPlayerByLowGUID(guid);
        companions.push_back({{"guid", guid},
                              {"name", entry->Name},
                              {"class", entry->Class},
                              {"level", online ? online->GetLevel() : entry->Level},
                              {"zone", online ? ZoneName(online->GetZoneId()) : ""},
                              {"online", online != nullptr},
                              {"in_party", online && SameGroup(online, player)},
                              {"familiarity", familiarity}});
    }
    Reply(player, id, {{"characters", characters}, {"companions", companions}});
}

// A read-only summary of a bot the player can see in their group or commands.
void Bridge::Inspect(Player* player, pbc_json const& request)
{
    pbc_json id = request.contains("id") ? request["id"] : pbc_json();
    if (!request.contains("bot") || !request["bot"].is_number_unsigned())
        return Fail(player, id, "bad_request");
    Player* bot = ObjectAccessor::FindPlayerByLowGUID(request["bot"].get<ObjectGuid::LowType>());
    if (!IsBot(bot) || !bot->IsInWorld())
        return Fail(player, id, "unknown_bot");
    if (!SameGroup(bot, player) && !Commandable(player, bot))
        return Fail(player, id, "not_yours");
    uint32 levels = 0;
    uint32 worn = 0;
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
        if (slot != EQUIPMENT_SLOT_BODY && slot != EQUIPMENT_SLOT_TABARD)
            if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            {
                levels += item->GetTemplate()->ItemLevel;
                ++worn;
            }
    Reply(player, id, {{"bot", Describe(player, bot)},
                       {"spec", AiFactory::GetPlayerSpecName(bot)},
                       {"item_level", worn ? levels / worn : 0},
                       {"free_slots", bot->GetFreeInventorySpace()},
                       {"money", bot->GetMoney()},
                       {"zone", ZoneName(bot->GetZoneId())}});
}

// Brings one of the player's own characters into the world as their bot, or sends it home,
// through Playerbots' own `.playerbots bot add/remove`, with its checks.
void Bridge::Bring(Player* player, pbc_json const& request, bool bring)
{
    pbc_json id = request.contains("id") ? request["id"] : pbc_json();
    if (!request.contains("name") || !request["name"].is_string())
        return Fail(player, id, "bad_request");
    std::string name = request["name"].get<std::string>();
    ObjectGuid guid = sCharacterCache->GetCharacterGuidByName(name);
    CharacterCacheEntry const* entry = guid ? sCharacterCache->GetCharacterCacheByGuid(guid) : nullptr;
    if (!entry || entry->AccountId != player->GetSession()->GetAccountId() || guid == player->GetGUID())
        return Fail(player, id, "not_yours");
    auto messages = BotCommand(player, std::string(bring ? "add " : "remove ") + entry->Name);
    bool done = std::any_of(messages.begin(), messages.end(),
                            [](std::string const& line) { return line.find(": ok") != std::string::npos; });
    pbc_json body = {{"messages", messages}};
    if (!done)
        return Send(player, {{"re", id}, {"ok", false}, {"error", "refused"}, {"reason", messages.empty() ? "" : messages.front()}});
    Reply(player, id, body);
}

void Bridge::Update(uint32 diff)
{
    if (!_enabled)
        return;
    for (std::size_t i = 0; i < _pending.size();)
    {
        Pending& pending = _pending[i];
        Player* player = ObjectAccessor::FindConnectedPlayer(pending.player);
        Player* bot = ObjectAccessor::FindConnectedPlayer(pending.bot);
        bool finished = true;
        if (!player)
        {
            // Logged out; nobody to tell.
        }
        else if (!IsBot(bot) || !bot->IsInWorld())
            Fail(player, pending.id, "unknown_bot");
        else if (Done(bot, pending.order, pending.on))
            Reply(player, pending.id, {{"bot", Describe(player, bot)}});
        else if ((pending.waited += diff) >= _timeoutMs)
            Send(player, {{"re", pending.id}, {"ok", false}, {"error", "timeout"}, {"bot", Describe(player, bot)}});
        else
            finished = false;
        if (finished)
            _pending.erase(_pending.begin() + i);
        else
            ++i;
    }
    if ((_sinceWatch += diff) >= WatchIntervalMs)
    {
        _sinceWatch = 0;
        Watch();
    }
}

void Bridge::Watch()
{
    for (auto it = _watching.begin(); it != _watching.end();)
    {
        Player* player = ObjectAccessor::FindConnectedPlayer(it->first);
        if (!player)
        {
            it = _watching.erase(it);
            continue;
        }
        auto& last = it->second;
        std::set<ObjectGuid> present;
        for (Player* bot : BotsFor(player))
        {
            present.insert(bot->GetGUID());
            pbc_json state = Describe(player, bot);
            std::string text = state.dump();
            if (last[bot->GetGUID()] != text)
            {
                last[bot->GetGUID()] = text;
                Send(player, {{"ev", "bot"}, {"bot", state}});
            }
        }
        for (auto known = last.begin(); known != last.end();)
        {
            if (present.count(known->first))
            {
                ++known;
                continue;
            }
            Send(player, {{"ev", "gone"}, {"guid", known->first.GetCounter()}});
            known = last.erase(known);
        }
        ++it;
    }
}

void Bridge::Forget(Player* player)
{
    if (!player)
        return;
    _assemblers.erase(player->GetGUID());
    _watching.erase(player->GetGUID());
    std::erase_if(_pending, [&](Pending const& pending) { return pending.player == player->GetGUID(); });
}

void Bridge::Send(Player* player, pbc_json const& message)
{
    std::string text = message.dump(-1, ' ', true, pbc_json::error_handler_t::replace);
    // The client treats "|" as an escape character, so it never travels raw.
    for (std::size_t at = text.find('|'); at != std::string::npos; at = text.find('|', at))
        text.replace(at, 1, "\\u007c");
    _messages = _messages % (36u * 36u * 36u * 36u - 1) + 1;
    for (auto const& frame : EncodeFrames(Base36(_messages), text))
    {
        // Built from GUIDs so it is never marked as a game master's speech: it is the server's.
        WorldPacket data;
        ChatHandler::BuildChatPacket(data, CHAT_MSG_WHISPER, LANG_ADDON, player->GetGUID(), player->GetGUID(),
                                     std::string(Prefix) + '\t' + frame, CHAT_TAG_NONE);
        player->SendDirectMessage(&data);
    }
}

void Bridge::Reply(Player* player, pbc_json const& id, pbc_json body)
{
    body["re"] = id;
    body["ok"] = true;
    Send(player, body);
}

void Bridge::Fail(Player* player, pbc_json const& id, std::string const& error)
{
    Send(player, {{"re", id}, {"ok", false}, {"error", error}});
}
}
