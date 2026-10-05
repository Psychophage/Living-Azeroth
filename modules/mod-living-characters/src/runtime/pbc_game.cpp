// PBC Character System changes, 2026-09-30; see NOTICE.md for upstream attribution.
#include "pbc_game.h"
#include "pbc_json.h"
#include "pbc_group_helpers.h"
#include "CellImpl.h"
#include "Channel.h"
#include "ChannelMgr.h"
#include "Chat.h"
#include "Creature.h"
#include "DBCStores.h"
#include "GridNotifiersImpl.h"
#include "Group.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "ObjectMgr.h"
#include "SpellAuraEffects.h"
#include "MapMgr.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Playerbots.h"
#include "World.h"
#include "WorldSessionMgr.h"
#include "ScriptMgr.h"
#include "SocialMgr.h"
#include <algorithm>
#include <list>
#include <shared_mutex>

#include "pbc_native.h"

namespace PBC
{
namespace
{
bool Human(Player* player)
{
    return player && player->GetSession() && !player->GetSession()->IsBot();
}

bool GroupChat(uint32_t type)
{
    return type == CHAT_MSG_PARTY || type == CHAT_MSG_PARTY_LEADER || type == CHAT_MSG_RAID ||
           type == CHAT_MSG_RAID_LEADER || type == CHAT_MSG_RAID_WARNING;
}

bool PartyChat(uint32_t type)
{
    return type == CHAT_MSG_PARTY || type == CHAT_MSG_PARTY_LEADER;
}

float ListenRange(uint32_t type)
{
    if (type == CHAT_MSG_YELL)
        return sWorld->getFloatConfig(CONFIG_LISTEN_RANGE_YELL);
    if (type == CHAT_MSG_EMOTE)
        return sWorld->getFloatConfig(CONFIG_LISTEN_RANGE_TEXTEMOTE);
    return sWorld->getFloatConfig(CONFIG_LISTEN_RANGE_SAY);
}

std::string AreaName(uint32_t area)
{
    auto entry = sAreaTableStore.LookupEntry(area);
    return entry && entry->area_name[LOCALE_enUS] ? entry->area_name[LOCALE_enUS] : "Unknown place";
}

bool Nearby(WorldObject* source, Unit* unit, float range)
{
    return source && unit && unit->IsInWorld() && unit->IsAlive() && source->IsWithinDistInMap(unit, range) &&
           source->CanSeeOrDetect(unit) && source->IsWithinLOSInMap(unit);
}

bool Hears(Unit* source, Player* listener, float range)
{
    // Core local chat uses distance/phase/client visibility, not spell line of
    // sight. A wall must not erase dialogue that the player's client receives.
    return source && listener && listener->IsInWorld() && listener->IsAlive() && source->IsInMap(listener) &&
           listener->InSamePhase(source) && source->GetExactDist(listener) <= range &&
           (source == listener || listener->HaveAtClient(source));
}

struct CreatureCheck
{
    Player* anchor;
    Unit* source;
    float range;
    WorldObject const& GetFocusObject() const { return *source; }
    bool operator()(Creature* creature) const
    {
        return Nearby(source, creature, range) && EligibleNpc(anchor, creature, range * 2);
    }
};

Channel* ResolveChannel(GameAudience const& audience, Player* player)
{
    if (auto manager = ChannelMgr::forTeam(player->GetTeamId()))
        return manager->GetChannel(audience.channelName, player, false);
    return nullptr;
}
}  // namespace

bool HasHumanConnection(Player* player)
{
    return Human(player) && !player->GetSession()->IsSocketClosed() && !player->GetSession()->IsLoggingOut();
}

GameActor SnapshotActor(Unit* unit, NpcDefinitions const& definitions, std::string const& realmPhase)
{
    GameActor actor;
    actor.guid = unit->GetGUID();
    actor.mapId = unit->GetMapId();
    actor.instanceId = unit->GetInstanceId();
    actor.zoneId = unit->GetZoneId();
    actor.identity.name = unit->GetName();
    pbc_json facts = {{"level", unit->GetLevel()},
                      {"zone", AreaName(unit->GetZoneId())},
                      {"map_id", unit->GetMapId()}, {"zone_id", unit->GetZoneId()}, {"area_id", unit->GetAreaId()},
                      {"area", AreaName(unit->GetAreaId())},
                      {"realm_phase", realmPhase},
                      {"alive", unit->IsAlive()},
                      {"in_combat", unit->IsInCombat()},
                      {"achievements", pbc_json::array()}};
    if (auto player = unit->ToPlayer())
    {
        actor.human = Human(player);
        actor.identity.id = "player:" + std::to_string(player->GetGUID().GetCounter());
        actor.identity.kind = actor.human ? "player" : "bot";
        if (actor.human)
            actor.identity.ownerGuid = player->GetGUID().GetCounter();
        if (!actor.human)
            facts["money_copper"] = player->GetMoney();
        facts["spoken_language"] = player->GetTeamId() == TEAM_ALLIANCE ? LANG_COMMON : LANG_ORCISH;
        if (auto guild = sGuildMgr->GetGuildById(player->GetGuildId()); guild && guild->GetMember(player->GetGUID()))
        {
            facts["guild_id"] = guild->GetId();
            facts["guild_rank"] = player->GetRank();
            facts["guild_name"] = guild->GetName();
            if (guild->HasRankRight(player, GR_RIGHT_GCHATLISTEN))
                actor.informationGroups.push_back({"guild:" + std::to_string(guild->GetId()), guild->GetName(),
                    "A shared guild notice is reported information, not every member's personal experience.", 259200000, false});
        }
        facts["race_id"] = player->getRace();
        facts["class_id"] = player->getClass();
        facts["gender"] = player->getGender() == GENDER_FEMALE ? "female" : "male";
        facts["race"] = RaceName(player->getRace());
        facts["class"] = ClassName(player->getClass());
        if (auto ai = GET_PLAYERBOT_AI(player))
            if (auto master = ai->GetMaster(); Human(master))
                actor.identity.ownerGuid = master->GetGUID().GetCounter();
        if (auto group = player->GetGroup())
        {
            facts["group_id"] = group->GetGUID().GetRawValue();
            facts["subgroup"] = group->GetMemberGroup(player->GetGUID());
        }
        else
            facts["group_id"] = nullptr;
    }
    else if (auto creature = unit->ToCreature())
    {
        actor.spawnId = creature->GetSpawnId();
        auto definition = definitions.find({actor.spawnId, actor.mapId, actor.instanceId});
        actor.identity.kind =
            definition == definitions.end() || definition->second.actorId.empty() ? "generic_npc" : "named_npc";
        actor.identity.id = actor.identity.kind == "named_npc"
                                ? definition->second.actorId
                                : "spawn:" + std::to_string(actor.mapId) + ":" + std::to_string(actor.instanceId) +
                                      ":" + std::to_string(actor.spawnId);
        // Temporary summons have no database spawn. Do not merge every temporary
        // creature on the map into spawn zero or carry its context into another life.
        if (!actor.spawnId)
            actor.identity.id = "temporary:" + std::to_string(actor.guid.GetRawValue());
        if (definition != definitions.end())
        {
            actor.watchId = definition->second.watchId;
            facts["canon"] = definition->second.canon;
        }
        facts["entry"] = creature->GetEntry();
        facts["role"] = creature->GetCreatureTemplate()->SubName;
        facts["guard"] = creature->IsGuard();
        facts["creature_type"] = creature->GetCreatureTemplate()->type;
        facts["rank"] = creature->GetCreatureTemplate()->rank;
        facts["beast_or_critter"] = creature->GetCreatureTemplate()->type == CREATURE_TYPE_BEAST ||
                                    creature->GetCreatureTemplate()->type == CREATURE_TYPE_CRITTER;
        facts["world_boss"] = creature->GetCreatureTemplate()->rank == CREATURE_ELITE_WORLDBOSS;
        facts["group_id"] = nullptr;
    }
    actor.canSpeak = !actor.human && unit->IsAlive() && !unit->IsInCombat();
    actor.factsJson = facts.dump();
    return actor;
}

GameAudience CaptureAudience(Player* anchor, uint32_t chatType, Player* whisperTarget, Channel* channel,
                             NpcDefinitions const& definitions, std::string const& realmPhase, Unit* localSource,
                             bool /*sharedLanguage*/)
{
    GameAudience audience;
    if (!anchor || !anchor->IsInWorld() || !HasHumanConnection(anchor))
        return audience;
    audience.anchor = anchor->GetGUID();
    audience.chatType = chatType;
    auto add = [&](Unit* unit)
    {
        if (!unit || !unit->IsInWorld())
            return;
        if (auto player = unit->ToPlayer(); Human(player) && !HasHumanConnection(player))
            return;  // A disconnected player can remain in the world during logout grace.
        if (std::any_of(audience.actors.begin(), audience.actors.end(),
                        [&](auto const& actor) { return actor.guid == unit->GetGUID(); }))
            return;
        auto actor = SnapshotActor(unit, definitions, realmPhase);
        actor.addressed = unit == whisperTarget;
        audience.actors.push_back(std::move(actor));
    };
    add(anchor);
    if (chatType == CHAT_MSG_WHISPER)
    {
        audience.label = "whisper";
        if (!whisperTarget || Human(whisperTarget))
            return audience;
        audience.whisperTarget = whisperTarget->GetGUID();
        add(whisperTarget);
    }
    else if (GroupChat(chatType))
    {
        audience.label = PartyChat(chatType) ? "party" : "raid";
        auto group = anchor->GetOriginalGroup() ? anchor->GetOriginalGroup() : anchor->GetGroup();
        if (!group)
            return audience;
        audience.group = group->GetGUID();
        audience.subgroup = group->GetMemberGroup(anchor->GetGUID());
        for (auto reference = group->GetFirstMember(); reference; reference = reference->next())
            if (auto member = reference->GetSource())
                if (!PartyChat(chatType) || group->GetMemberGroup(member->GetGUID()) == audience.subgroup)
                    add(member);
    }
    else if (chatType == CHAT_MSG_GUILD)
    {
        auto guild = sGuildMgr->GetGuildById(anchor->GetGuildId());
        auto source = localSource ? localSource->ToPlayer() : anchor;
        if (!guild || !source || source->GetGuildId() != guild->GetId() ||
            !guild->HasRankRight(source, GR_RIGHT_GCHATSPEAK))
            return audience;
        audience.label = "guild";
        audience.guildId = guild->GetId();
        audience.actors.clear();
        std::shared_lock playersLock(*HashMapHolder<Player>::GetLock());
        for (auto const& [id, player] : ObjectAccessor::GetPlayers())
            if (player && player->IsInWorld() && player->GetGuildId() == audience.guildId &&
                guild->HasRankRight(player, GR_RIGHT_GCHATLISTEN) &&
                !player->GetSocial()->HasIgnore(source->GetGUID()))
                add(player);
    }
    else if (chatType == CHAT_MSG_CHANNEL)
    {
        audience.label = "general";
        if (!channel || channel->GetChannelId() != 1 || !anchor->IsInChannel(channel))
            return audience;
        audience.channelName = channel->GetName();
        auto source = localSource && localSource->IsPlayer() ? localSource->ToPlayer() : anchor;
        if (channel->GetMemberFlags(source->GetGUID()) & MEMBER_FLAG_MUTED)
        {
            audience.label.clear();
            return audience;
        }
        std::shared_lock playersLock(*HashMapHolder<Player>::GetLock());
        for (auto const& [id, player] : ObjectAccessor::GetPlayers())
            if (player && player->IsInWorld() && player->IsInChannel(channel) &&
                ((channel->GetMemberFlags(source->GetGUID()) & MEMBER_FLAG_MODERATOR) ||
                 !player->GetSocial()->HasIgnore(source->GetGUID())))
                add(player);
    }
    else if (chatType == CHAT_MSG_SAY || chatType == CHAT_MSG_YELL || chatType == CHAT_MSG_EMOTE)
    {
        audience.label = chatType == CHAT_MSG_SAY ? "say" : chatType == CHAT_MSG_YELL ? "yell" : "emote";
        float range = ListenRange(chatType);
        Unit* origin = localSource ? localSource : anchor;
        {
            // Playerbots have game Player objects but need not be in the human
            // WorldSessionMgr registry. The world registry includes both roles.
            std::shared_lock playersLock(*HashMapHolder<Player>::GetLock());
            for (auto const& [id, player] : ObjectAccessor::GetPlayers())
                if (Hears(origin, player, range))
                    add(player);
        }
        std::list<Creature*> creatures;
        CreatureCheck check{anchor, origin, range};
        Acore::CreatureListSearcher<CreatureCheck> searcher(origin, creatures, check);
        Cell::VisitObjects(origin, searcher, range);
        creatures.sort([&](auto left, auto right) { return origin->GetDistance(left) < origin->GetDistance(right); });
        for (auto creature : creatures)
            add(creature);
    }
    return audience;
}

Unit* ResolveActor(GameActor const& actor)
{
    if (actor.guid.IsPlayer())
        return ObjectAccessor::FindPlayer(actor.guid);
    if (auto map = sMapMgr->FindMap(actor.mapId, actor.instanceId))
        return map->GetCreature(actor.guid);
    return nullptr;
}

bool AudienceStillValid(GameAudience const& audience, GameActor const& speaker)
{
    if (audience.combat)
        return CombatAudienceValid(audience, speaker);
    auto anchor = ObjectAccessor::FindPlayer(audience.anchor);
    auto unit = ResolveActor(speaker);
    if (!anchor || !anchor->IsInWorld() || !HasHumanConnection(anchor) || !anchor->IsAlive() || !unit ||
        !unit->IsInWorld() || !unit->IsAlive() ||
        (!audience.duringCombat && (anchor->IsInCombat() || unit->IsInCombat())))
        return false;
    if (audience.chatType == CHAT_MSG_WHISPER)
        return unit->GetGUID() == audience.whisperTarget && unit->IsPlayer();
    if (GroupChat(audience.chatType))
    {
        auto player = unit->ToPlayer();
        auto group = player ? player->GetGroup() : nullptr;
        return group && group->GetGUID() == audience.group && group->IsMember(anchor->GetGUID()) &&
               (!PartyChat(audience.chatType) || (group->GetMemberGroup(player->GetGUID()) == audience.subgroup &&
                                                  group->GetMemberGroup(anchor->GetGUID()) == audience.subgroup));
    }
    if (audience.chatType == CHAT_MSG_GUILD)
    {
        auto player = unit->ToPlayer();
        auto guild = sGuildMgr->GetGuildById(audience.guildId);
        return player && guild && player->GetGuildId() == audience.guildId && anchor->GetGuildId() == audience.guildId &&
               guild->HasRankRight(player, GR_RIGHT_GCHATSPEAK) && guild->HasRankRight(anchor, GR_RIGHT_GCHATLISTEN);
    }
    if (audience.chatType == CHAT_MSG_CHANNEL)
    {
        auto player = unit->ToPlayer();
        auto channel = ResolveChannel(audience, anchor);
        return player && channel && player->IsInChannel(channel) && anchor->IsInChannel(channel);
    }
    if (auto creature = unit->ToCreature())
        return EligibleNpc(anchor, creature, std::min(ListenRange(audience.chatType), ListenRange(CHAT_MSG_SAY)));
    // Local player speech follows chat visibility/range, not spell line of sight.
    return Hears(unit, anchor, ListenRange(audience.chatType));
}

bool DeliverSegment(GameAudience const& audience, GameActor const& speaker, Segment const& segment)
{
    if (!AudienceStillValid(audience, speaker))
        return false;
    auto anchor = ObjectAccessor::FindPlayer(audience.anchor);
    auto unit = ResolveActor(speaker);
    auto player = unit->ToPlayer();
    // In private/group/channel conversations an action is a narrated line in that
    // channel. A physical emote would incorrectly reveal it to nearby outsiders.
    bool local =
        audience.chatType == CHAT_MSG_SAY || audience.chatType == CHAT_MSG_YELL || audience.chatType == CHAT_MSG_EMOTE;
    std::string text = segment.kind == Segment::Kind::Emote && !local ? "*" + segment.text + "*" : segment.text;
    // Send native chat packets to the SAME snapshot that becomes witnessed history.
    // Native Say/Whisper return void and can silently filter/reject a line; treating
    // their return as delivery would persist promises nobody received.
    if (player)
    {
        if (!player->CanSpeak() || Player::IsChatFiltered(text))
            return false;
        auto checked = text;
        bool allowed = false;
        if (audience.chatType == CHAT_MSG_WHISPER)
            allowed = !anchor->GetSocial()->HasIgnore(player->GetGUID()) &&
                      sScriptMgr->OnPlayerCanUseChat(player, CHAT_MSG_WHISPER, LANG_UNIVERSAL, checked, anchor);
        else if (GroupChat(audience.chatType))
            allowed =
                sScriptMgr->OnPlayerCanUseChat(player, audience.chatType, LANG_UNIVERSAL, checked, player->GetGroup());
        else if (audience.chatType == CHAT_MSG_GUILD)
        {
            auto guild = sGuildMgr->GetGuildById(audience.guildId);
            allowed = guild && guild->HasRankRight(player, GR_RIGHT_GCHATSPEAK) &&
                      sScriptMgr->OnPlayerCanUseChat(player, CHAT_MSG_GUILD, audience.language, checked, guild);
        }
        else if (audience.chatType == CHAT_MSG_CHANNEL)
        {
            auto channel = ResolveChannel(audience, player);
            allowed = channel && !(channel->GetMemberFlags(player->GetGUID()) & MEMBER_FLAG_MUTED) &&
                      sScriptMgr->OnPlayerCanUseChat(player, CHAT_MSG_CHANNEL, LANG_UNIVERSAL, checked, channel);
        }
        else
            allowed = sScriptMgr->OnPlayerCanUseChat(player, audience.chatType, LANG_UNIVERSAL, checked);
        if (!allowed || checked != text)
            return false;  // An external hook changed the prepared evidence; reassess on new input.
    }
    ChatMsg type = static_cast<ChatMsg>(audience.chatType);
    if (local)
    {
        if (segment.kind == Segment::Kind::Emote)
            type = player ? CHAT_MSG_EMOTE : CHAT_MSG_MONSTER_EMOTE;
        else
            type = player ? (audience.chatType == CHAT_MSG_YELL ? CHAT_MSG_YELL : CHAT_MSG_SAY) : CHAT_MSG_MONSTER_SAY;
    }
    // Player emotes receive a client-side name prefix; monster emotes display
    // their body verbatim. Keep the model's action text free of packet formatting.
    if (type == CHAT_MSG_MONSTER_EMOTE && text != unit->GetName() && !text.starts_with(unit->GetName() + " "))
        text = unit->GetName() + " " + text;
    WorldPacket packet;
    ChatHandler::BuildChatPacket(packet, type, Language(audience.language), unit, type == CHAT_MSG_WHISPER ? anchor : nullptr, text,
                                 0, audience.channelName);
    bool humanReached = false;
    for (auto const& recipient : audience.actors)
    {
        if (type == CHAT_MSG_WHISPER && recipient.guid != anchor->GetGUID())
            continue;  // The speaker knows its speech; do not feed its own whisper back into bot AI.
        if (auto target = ObjectAccessor::FindPlayer(recipient.guid); target && target->IsInWorld())
        {
            target->SendDirectMessage(&packet);
            humanReached |= HasHumanConnection(target);
        }
    }
    if (humanReached && local && segment.kind == Segment::Kind::Emote && !segment.animation.empty())
        PlayNativeAnimation(unit, segment.animation);
    return humanReached;
}

}  // namespace PBC
