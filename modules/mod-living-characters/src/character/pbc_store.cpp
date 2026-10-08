// PBC Character System changes, 2026-09-30; see NOTICE.md for upstream attribution.
#include "pbc_store.h"
#include "pbc_sql.h"
#include "pbc_json.h"
#include <algorithm>
#include <charconv>
#include <mutex>
#include <unordered_map>
#include <set>

namespace PBC
{
namespace
{
enum Statement : uint32
{
    ActorInsert, ActorSelect, FoundationUpdate, EventSelect, EventInsert, LastId, WitnessInsert,
    ActorObserved, RecentSelect, PendingSelect, SourceRead, SourceCheck, NoteSelect, NoteByOperation, NoteInsert,
    NoteSourceInsert, WitnessProcessed, RecallUpdate, NoteCompacted, RecallNoteInsert, RecallCopy,
    NoteById, NoteEdit, ActorEdited, RecallInvalidate, NotesReopen, DueActors,
    OwnerUpdate, NpcInsert, NpcSelect, OwnerFactInsert, NoteResolve, NoteSources, ActiveNoteSelect, ActiveNoteSources,
    DeliveryInsert, DeliveryClose, SourceById, SourceArchive, SourceRevise, SourceNotes,
    NoteInvalidate, WitnessActorsEdited, PlayerRoleUpdate, ReportSelect, GroupSourceCheck, ReportDuplicate,
    ListenerSelect, ListenerUpsert, GuildIdentitySelect, GuildIdentityUpsert, GroupReportList, HeardLines, SceneOpening, NotesAbout, SceneActions, Count
};

class Connection final : public SqlConnection
{
public:
    explicit Connection(MySQLConnectionInfo& info) : SqlConnection(info) { }

private:
    void DoPrepareStatements() override
    {
        m_stmts.resize(Count);
        PrepareStatement(ActorInsert, "INSERT IGNORE INTO pbc_actor "
            "(actor_id,kind,owner_guid,display_name,foundation,summary,recall) VALUES (?,?,?,?,'','','')",
            CONNECTION_SYNCH);
        PrepareStatement(ActorSelect, "SELECT actor_id,kind,owner_guid,display_name,version,"
            "foundation,summary,recall,recall_version,last_observed_ms FROM pbc_actor WHERE actor_id=? FOR UPDATE",
            CONNECTION_SYNCH);
        PrepareStatement(FoundationUpdate, "UPDATE pbc_actor SET foundation=?,summary=?,version=version+1 "
            "WHERE actor_id=? AND version=? AND foundation=''", CONNECTION_SYNCH);
        PrepareStatement(EventSelect, "SELECT id,version,payload,author_id,channel FROM pbc_observation "
            "WHERE event_key=?", CONNECTION_SYNCH);
        PrepareStatement(EventInsert, "INSERT INTO pbc_observation "
            "(event_key,scene_id,author_id,channel,map_id,instance_id,zone_id,created_ms,evidence,payload) "
            "VALUES (?,?,?,?,?,?,?,?,?,?)", CONNECTION_SYNCH);
        PrepareStatement(LastId, "SELECT LAST_INSERT_ID()", CONNECTION_SYNCH);
        PrepareStatement(WitnessInsert, "INSERT INTO pbc_witness (observation_id,actor_id) VALUES (?,?)",
            CONNECTION_SYNCH);
        PrepareStatement(ActorObserved, "UPDATE pbc_actor SET last_observed_ms=GREATEST(last_observed_ms,?) "
            "WHERE actor_id=?", CONNECTION_SYNCH);
        std::string const sourceColumns = "SELECT o.id,o.version,o.event_key,o.scene_id,o.author_id,o.channel,"
            "o.evidence,o.map_id,o.instance_id,o.zone_id,o.created_ms,o.payload FROM pbc_observation o "
            "JOIN pbc_witness w ON w.observation_id=o.id WHERE w.actor_id=? AND o.excluded=0 ";
        PrepareStatement(RecentSelect, sourceColumns + "ORDER BY o.id DESC LIMIT ?", CONNECTION_SYNCH);
        PrepareStatement(PendingSelect, sourceColumns + "AND w.processed_version<o.version ORDER BY o.id LIMIT ?",
            CONNECTION_SYNCH);
        PrepareStatement(SourceRead, "SELECT o.id,o.version,o.event_key,o.scene_id,o.author_id,o.channel,"
            "o.evidence,o.map_id,o.instance_id,o.zone_id,o.created_ms,o.payload FROM pbc_observation o "
            "JOIN pbc_witness w ON w.observation_id=o.id WHERE w.actor_id=? AND o.id=?", CONNECTION_SYNCH);
        PrepareStatement(SourceCheck, "SELECT o.version FROM pbc_observation o JOIN pbc_witness w "
            "ON w.observation_id=o.id WHERE w.actor_id=? AND o.id=? AND o.version=? AND o.excluded=0 FOR UPDATE",
            CONNECTION_SYNCH);
        PrepareStatement(NoteSelect, "SELECT id,version,operation_id,actor_id,subject_id,kind,content,authority,"
            "resolved,compacted_version,created_ms FROM pbc_note WHERE actor_id=? AND valid=1 "
            "ORDER BY id DESC LIMIT ?", CONNECTION_SYNCH);
        PrepareStatement(ReportSelect, "SELECT id,version,operation_id,actor_id,subject_id,kind,content,authority,"
            "resolved,compacted_version,created_ms,COALESCE((SELECT GROUP_CONCAT(DISTINCT o.author_id ORDER BY o.author_id SEPARATOR ', ') "
            "FROM pbc_note_source s JOIN pbc_observation o ON o.id=s.observation_id WHERE s.note_id=pbc_note.id),'') "
            "FROM pbc_note WHERE actor_id=? AND kind='report' AND valid=1 "
            "AND resolved=0 AND created_ms>=? AND created_ms<=? "
            "AND (subject_id='' OR JSON_CONTAINS(?,JSON_QUOTE(subject_id))) ORDER BY id DESC LIMIT 16",
            CONNECTION_SYNCH);
        PrepareStatement(ActiveNoteSelect, "SELECT id,version,operation_id,actor_id,subject_id,kind,content,authority,"
            "resolved,compacted_version,created_ms FROM pbc_note WHERE actor_id=? AND valid=1 "
            "AND (compacted_version IS NULL OR authority<>'model' OR kind='fact' OR (kind='commitment' AND resolved=0)) "
            "ORDER BY (authority<>'model' OR kind='fact' OR (kind='commitment' AND resolved=0)) DESC,id DESC LIMIT ?",
            CONNECTION_SYNCH);
        PrepareStatement(ActiveNoteSources, "SELECT s.note_id,s.observation_id,s.source_version FROM pbc_note_source s "
            "JOIN (SELECT id FROM pbc_note WHERE actor_id=? AND valid=1 "
            "AND (compacted_version IS NULL OR authority<>'model' OR kind='fact' OR (kind='commitment' AND resolved=0)) "
            "ORDER BY (authority<>'model' OR kind='fact' OR (kind='commitment' AND resolved=0)) DESC,id DESC LIMIT ?) n "
            "ON n.id=s.note_id ORDER BY s.note_id,s.observation_id", CONNECTION_SYNCH);
        PrepareStatement(NoteByOperation, "SELECT id FROM pbc_note WHERE operation_id=? AND actor_id=?",
            CONNECTION_SYNCH);
        PrepareStatement(NoteInsert, "INSERT INTO pbc_note "
            "(operation_id,actor_id,subject_id,kind,content,authority,created_ms) VALUES (?,?,?,?,?,'model',?)",
            CONNECTION_SYNCH);
        PrepareStatement(NoteSourceInsert, "INSERT IGNORE INTO pbc_note_source "
            "(note_id,observation_id,source_version) VALUES (?,?,?)", CONNECTION_SYNCH);
        PrepareStatement(WitnessProcessed, "UPDATE pbc_witness SET processed_version=? "
            "WHERE actor_id=? AND observation_id=? AND processed_version<?", CONNECTION_SYNCH);
        PrepareStatement(RecallUpdate, "UPDATE pbc_actor SET recall=?,recall_version=recall_version+1 "
            "WHERE actor_id=? AND version=? AND recall_version=?", CONNECTION_SYNCH);
        PrepareStatement(NoteCompacted, "UPDATE pbc_note SET compacted_version=? WHERE id=? AND actor_id=?",
            CONNECTION_SYNCH);
        PrepareStatement(RecallNoteInsert, "INSERT IGNORE INTO pbc_recall_note "
            "(actor_id,recall_version,note_id,note_version) VALUES (?,?,?,?)", CONNECTION_SYNCH);
        PrepareStatement(RecallCopy, "INSERT IGNORE INTO pbc_recall_note "
            "(actor_id,recall_version,note_id,note_version) "
            "SELECT actor_id,?,note_id,note_version FROM pbc_recall_note WHERE actor_id=? AND recall_version=?",
            CONNECTION_SYNCH);
        PrepareStatement(NoteById, "SELECT version,actor_id,valid,authority,kind FROM pbc_note WHERE id=? FOR UPDATE",
            CONNECTION_SYNCH);
        PrepareStatement(NoteEdit, "UPDATE pbc_note SET content=?,version=version+1,authority='owner',"
            "valid=1,compacted_version=NULL WHERE id=?", CONNECTION_SYNCH);
        PrepareStatement(ActorEdited, "UPDATE pbc_actor SET version=version+1 WHERE actor_id=?", CONNECTION_SYNCH);
        PrepareStatement(RecallInvalidate, "UPDATE pbc_actor SET recall='',recall_version=recall_version+1 "
            "WHERE actor_id IN (SELECT actor_id FROM pbc_recall_note WHERE note_id=?)", CONNECTION_SYNCH);
        PrepareStatement(NotesReopen, "UPDATE pbc_note SET compacted_version=NULL "
            "WHERE actor_id IN (SELECT actor_id FROM pbc_recall_note WHERE note_id=?)", CONNECTION_SYNCH);
        // A memory pass costs a model call, so only something a player said earns one. Witnessed
        // events alone stay pending until that character next replies to someone.
        PrepareStatement(DueActors, "SELECT a.actor_id FROM pbc_actor a JOIN pbc_witness w ON w.actor_id=a.actor_id "
            "JOIN pbc_observation o ON o.id=w.observation_id LEFT JOIN pbc_actor h ON h.actor_id=o.author_id "
            "WHERE w.processed_version<o.version AND o.excluded=0 "
            "AND a.kind IN ('bot','named_npc','watch') GROUP BY a.actor_id,a.last_observed_ms "
            "HAVING ((a.last_observed_ms<=? AND ?>=?) OR SUM(OCTET_LENGTH(o.payload))>=?) "
            "AND SUM(h.kind='player' AND o.channel<>'event')>0 "
            "ORDER BY a.last_observed_ms LIMIT ?", CONNECTION_SYNCH);
        PrepareStatement(GroupSourceCheck, "SELECT o.created_ms FROM pbc_observation o JOIN pbc_witness w "
            "ON w.observation_id=o.id WHERE w.actor_id=? AND o.id=? AND o.version=? AND o.excluded=0 "
            "AND o.channel IN ('say','yell','emote','event','guild') AND EXISTS "
            "(SELECT 1 FROM pbc_witness g WHERE g.observation_id=o.id AND g.actor_id=?) FOR UPDATE",
            CONNECTION_SYNCH);
        PrepareStatement(ListenerSelect, "SELECT settings FROM pbc_listener WHERE player_guid=?", CONNECTION_SYNCH);
        PrepareStatement(ListenerUpsert, "INSERT INTO pbc_listener (player_guid,settings,updated_ms) VALUES (?,?,?) "
            "ON DUPLICATE KEY UPDATE settings=VALUES(settings),updated_ms=VALUES(updated_ms)", CONNECTION_SYNCH);
        PrepareStatement(GuildIdentitySelect, "SELECT guild_id,identity FROM pbc_guild_identity", CONNECTION_SYNCH);
        PrepareStatement(GuildIdentityUpsert, "INSERT INTO pbc_guild_identity (guild_id,identity,updated_by,updated_ms) "
            "VALUES (?,?,?,?) ON DUPLICATE KEY UPDATE identity=VALUES(identity),updated_by=VALUES(updated_by),"
            "updated_ms=VALUES(updated_ms)", CONNECTION_SYNCH);
        // Every report a group holds, resolved ones included, for its officers to manage.
        PrepareStatement(GroupReportList, "SELECT id,version,operation_id,actor_id,subject_id,kind,content,authority,"
            "resolved,compacted_version,created_ms,COALESCE((SELECT GROUP_CONCAT(DISTINCT o.author_id ORDER BY o.author_id SEPARATOR ', ') "
            "FROM pbc_note_source s JOIN pbc_observation o ON o.id=s.observation_id WHERE s.note_id=pbc_note.id),''),"
            "COALESCE((SELECT o.zone_id FROM pbc_note_source s JOIN pbc_observation o ON o.id=s.observation_id "
            "WHERE s.note_id=pbc_note.id ORDER BY o.id LIMIT 1),0) "
            "FROM pbc_note WHERE actor_id=? AND kind='report' AND valid=1 AND created_ms>=? ORDER BY id DESC LIMIT 50",
            CONNECTION_SYNCH);
        // "Why did they say that?": only what this witness was actually sent.
        PrepareStatement(HeardLines, "SELECT o.id,o.scene_id,o.author_id,o.channel,o.created_ms,o.payload "
            "FROM pbc_observation o JOIN pbc_witness w ON w.observation_id=o.id WHERE w.actor_id=? "
            "AND o.evidence='delivered' AND o.excluded=0 AND o.author_id<>? ORDER BY o.id DESC LIMIT ?", CONNECTION_SYNCH);
        PrepareStatement(SceneOpening, "SELECT author_id,payload FROM pbc_observation WHERE scene_id=? "
            "AND evidence='observed' ORDER BY id LIMIT 1", CONNECTION_SYNCH);
        PrepareStatement(NotesAbout, "SELECT kind,content FROM pbc_note WHERE actor_id=? AND subject_id=? AND valid=1 "
            "AND resolved=0 ORDER BY id DESC LIMIT 5", CONNECTION_SYNCH);
        PrepareStatement(SceneActions, "SELECT intent_json,status,detail FROM pbc_action WHERE scene_id=? "
            "ORDER BY created_ms LIMIT 8", CONNECTION_SYNCH);
        PrepareStatement(ReportDuplicate, "SELECT n.id FROM pbc_note n JOIN pbc_note_source s ON s.note_id=n.id "
            "WHERE n.actor_id=? AND n.kind='report' AND n.subject_id=? AND n.valid=1 "
            "AND s.observation_id=? AND s.source_version=? LIMIT 1", CONNECTION_SYNCH);
        PrepareStatement(OwnerUpdate, "UPDATE pbc_actor SET owner_guid=?,version=version+1 "
            "WHERE actor_id=? AND COALESCE(owner_guid,0)<>?", CONNECTION_SYNCH);
        PrepareStatement(NpcInsert, "INSERT IGNORE INTO pbc_npc_identity "
            "(spawn_id,map_id,instance_id,actor_id,watch_id) VALUES (?,?,?,?,?)", CONNECTION_SYNCH);
        PrepareStatement(NpcSelect, "SELECT actor_id,COALESCE(watch_id,'') FROM pbc_npc_identity "
            "WHERE spawn_id=? AND map_id=? AND instance_id=?", CONNECTION_SYNCH);
        PrepareStatement(OwnerFactInsert, "INSERT INTO pbc_note "
            "(operation_id,actor_id,kind,content,authority,created_ms) VALUES (?,?,'fact',?,'owner',?)",
            CONNECTION_SYNCH);
        PrepareStatement(NoteResolve, "UPDATE pbc_note SET resolved=1,version=version+1,authority='owner',"
            "compacted_version=NULL WHERE id=?", CONNECTION_SYNCH);
        PrepareStatement(NoteSources, "SELECT s.note_id,s.observation_id,s.source_version FROM pbc_note_source s "
            "JOIN (SELECT id FROM pbc_note WHERE actor_id=? AND valid=1 ORDER BY id DESC LIMIT ?) n "
            "ON n.id=s.note_id ORDER BY s.note_id,s.observation_id", CONNECTION_SYNCH);
        PrepareStatement(DeliveryInsert, "INSERT INTO pbc_delivery "
            "(event_key,actor_id,actor_version,observation_json,witnesses_json) VALUES (?,?,?,?,?)", CONNECTION_SYNCH);
        PrepareStatement(DeliveryClose, "UPDATE pbc_delivery SET state=?,source_id=NULLIF(?,0) "
            "WHERE event_key=? AND state='prepared'", CONNECTION_SYNCH);
        PrepareStatement(SourceById, "SELECT version FROM pbc_observation WHERE id=? FOR UPDATE", CONNECTION_SYNCH);
        PrepareStatement(SourceArchive, "INSERT INTO pbc_observation_revision "
            "(observation_id,version,payload,excluded) SELECT id,version,payload,excluded "
            "FROM pbc_observation WHERE id=?", CONNECTION_SYNCH);
        PrepareStatement(SourceRevise, "UPDATE pbc_observation SET payload=?,excluded=?,version=version+1 "
            "WHERE id=?", CONNECTION_SYNCH);
        PrepareStatement(SourceNotes, "SELECT n.id FROM pbc_note n JOIN pbc_note_source s ON s.note_id=n.id "
            "WHERE s.observation_id=? AND n.authority='model' FOR UPDATE", CONNECTION_SYNCH);
        PrepareStatement(NoteInvalidate, "UPDATE pbc_note SET valid=0,version=version+1 WHERE id=?", CONNECTION_SYNCH);
        PrepareStatement(WitnessActorsEdited, "UPDATE pbc_actor SET version=version+1 WHERE actor_id IN "
            "(SELECT actor_id FROM pbc_witness WHERE observation_id=?)", CONNECTION_SYNCH);
        PrepareStatement(PlayerRoleUpdate, "UPDATE pbc_actor SET kind=?,version=version+1 WHERE actor_id=?",
            CONNECTION_SYNCH);
    }
};

ActorRecord ReadActor(PreparedResultSet const& row)
{
    return {row[0].Get<std::string>(), row[1].Get<std::string>(), row[2].Get<uint64>(),
        row[3].Get<std::string>(), row[4].Get<uint64>(), row[5].Get<std::string>(), row[6].Get<std::string>(),
        row[7].Get<std::string>(), row[8].Get<uint64>(), row[9].Get<uint64>()};
}
}

std::string SourceVersion::Key() const
{
    return std::to_string(id) + ":" + std::to_string(version);
}

std::optional<SourceVersion> SourceVersion::Parse(std::string const& key)
{
    auto separator = key.find(':');
    if (separator == std::string::npos)
        return std::nullopt;
    SourceVersion result;
    auto first = std::from_chars(key.data(), key.data() + separator, result.id);
    auto second = std::from_chars(key.data() + separator + 1, key.data() + key.size(), result.version);
    if (first.ec != std::errc() || first.ptr != key.data() + separator ||
        second.ec != std::errc() || second.ptr != key.data() + key.size() || !result.id || !result.version)
        return std::nullopt;
    return result;
}

class CharacterStore::Impl
{
public:
    mutable std::mutex mutex;
    std::unique_ptr<MySQLConnectionInfo> info;
    std::unique_ptr<Connection> connection;
    bool Ready()
    {
        if (!info)
            return false;
        if (!connection || connection->failed)
        {
            connection.reset();
            connection = std::make_unique<Connection>(*info);
            if (connection->Open() != 0 || !connection->PrepareStatements())
            {
                connection.reset();
                return false;
            }
        }
        return true;
    }
};

CharacterStore::CharacterStore() : _impl(std::make_unique<Impl>()) { }
CharacterStore::~CharacterStore() = default;

bool CharacterStore::Open(std::string const& connectionInfo)
{
    std::lock_guard lock(_impl->mutex);
    _impl->connection.reset();
    _impl->info = std::make_unique<MySQLConnectionInfo>(connectionInfo);
    return _impl->Ready();
}

bool CharacterStore::Healthy() const
{
    std::lock_guard lock(_impl->mutex);
    return _impl->connection && !_impl->connection->failed;
}

bool CharacterStore::EnsureActor(ActorRecord const& actor)
{
    std::lock_guard lock(_impl->mutex);
    if (!_impl->Ready())
        return false;
    auto& db = *_impl->connection;
    SqlTransaction transaction(db);
    if (!transaction || !db.Write(ActorInsert, actor.id, actor.kind, actor.ownerGuid, actor.name))
        return false;
    auto row = db.Read(ActorSelect, actor.id);
    if (!row)
        return false;
    auto kind = (*row)[1].Get<std::string>();
    if (kind != actor.kind)
    {
        if ((kind != "player" && kind != "bot") || (actor.kind != "player" && actor.kind != "bot") ||
            !db.Write(PlayerRoleUpdate, actor.kind, actor.id))
            return false;
    }
    return transaction.Commit();
}

bool CharacterStore::AssignOwner(std::string const& actor, uint64_t ownerGuid)
{
    std::lock_guard lock(_impl->mutex);
    if (!_impl->Ready())
        return false;
    // Only the game adapter calls this, after resolving the actual Playerbots master.
    // Passing players and party leadership alone do not establish ownership.
    return _impl->connection->Write(OwnerUpdate, ownerGuid, actor, ownerGuid);
}

bool CharacterStore::MapNpc(NpcIdentity const& identity)
{
    if (!identity.spawnId || identity.actorId.empty())
        return false;
    std::lock_guard lock(_impl->mutex);
    if (!_impl->Ready())
        return false;
    auto& db = *_impl->connection;
    SqlTransaction transaction(db);
    if (!transaction || !db.Read(ActorSelect, identity.actorId) ||
        (!identity.watchId.empty() && !db.Read(ActorSelect, identity.watchId)) ||
        !db.Write(NpcInsert, identity.spawnId, identity.mapId, identity.instanceId, identity.actorId, identity.watchId))
        return false;
    auto existing = db.Read(NpcSelect, identity.spawnId, identity.mapId, identity.instanceId);
    return existing && (*existing)[0].Get<std::string>() == identity.actorId &&
        (*existing)[1].Get<std::string>() == identity.watchId && transaction.Commit();
}

std::optional<NpcIdentity> CharacterStore::Npc(uint32_t spawnId, uint32_t mapId, uint32_t instanceId)
{
    std::lock_guard lock(_impl->mutex);
    if (!_impl->Ready())
        return std::nullopt;
    auto row = _impl->connection->Read(NpcSelect, spawnId, mapId, instanceId);
    if (!row)
        return std::nullopt;
    return NpcIdentity{spawnId, mapId, instanceId, (*row)[0].Get<std::string>(), (*row)[1].Get<std::string>()};
}

std::optional<std::string> CharacterStore::Listener(uint32_t playerGuid)
{
    std::lock_guard lock(_impl->mutex);
    if (!_impl->Ready())
        return std::nullopt;
    auto row = _impl->connection->Read(ListenerSelect, playerGuid);
    return row ? std::optional<std::string>((*row)[0].Get<std::string>()) : std::nullopt;
}

bool CharacterStore::SaveListener(uint32_t playerGuid, std::string const& settings, uint64_t nowMs)
{
    if (settings.empty() || settings.size() > 1024)
        return false;
    std::lock_guard lock(_impl->mutex);
    return _impl->Ready() && _impl->connection->Write(ListenerUpsert, playerGuid, settings, nowMs);
}

std::vector<std::pair<uint32_t, std::string>> CharacterStore::GuildIdentities()
{
    std::vector<std::pair<uint32_t, std::string>> identities;
    std::lock_guard lock(_impl->mutex);
    if (!_impl->Ready())
        return identities;
    if (auto rows = _impl->connection->Read(GuildIdentitySelect))
        do
        {
            identities.emplace_back((*rows)[0].Get<uint32>(), (*rows)[1].Get<std::string>());
        } while (rows->NextRow());
    return identities;
}

bool CharacterStore::SaveGuildIdentity(uint32_t guildId, std::string const& identity, uint32_t editorGuid,
    uint64_t nowMs)
{
    if (identity.size() > 4096)
        return false;
    std::lock_guard lock(_impl->mutex);
    return _impl->Ready() && _impl->connection->Write(GuildIdentityUpsert, guildId, identity, editorGuid, nowMs);
}

std::optional<ActorRecord> CharacterStore::Actor(std::string const& id)
{
    std::lock_guard lock(_impl->mutex);
    if (!_impl->Ready())
        return std::nullopt;
    auto row = _impl->connection->Read(ActorSelect, id);
    return row ? std::optional<ActorRecord>(ReadActor(*row)) : std::nullopt;
}

bool CharacterStore::Foundation(std::string const& actor, uint64_t version, std::string const& text,
    std::string const& summary)
{
    if (text.empty() || text.size() > 16000 || summary.size() > 1600)
        return false;
    std::lock_guard lock(_impl->mutex);
    if (!_impl->Ready())
        return false;
    auto& db = *_impl->connection;
    SqlTransaction transaction(db);
    if (!transaction)
        return false;
    auto row = db.Read(ActorSelect, actor);
    if (!row || (*row)[4].Get<uint64>() != version || !(*row)[5].Get<std::string>().empty())
        return false;
    return db.Write(FoundationUpdate, text, summary, actor, version) && transaction.Commit();
}

std::optional<SourceVersion> CharacterStore::Observe(ObservationRecord const& observation,
    std::vector<std::string> const& witnesses)
{
    if (observation.eventKey.empty() || witnesses.empty() || observation.text.empty())
        return std::nullopt;
    std::lock_guard lock(_impl->mutex);
    if (!_impl->Ready())
        return std::nullopt;
    auto& db = *_impl->connection;
    SqlTransaction transaction(db);
    if (!transaction)
        return std::nullopt;
    auto existing = db.Read(EventSelect, observation.eventKey);
    if (existing)
    {
        if ((*existing)[2].Get<std::string>() != observation.text ||
            (*existing)[3].Get<std::string>() != observation.authorId ||
            (*existing)[4].Get<std::string>() != observation.channel)
            return std::nullopt;
        return SourceVersion{(*existing)[0].Get<uint64>(), (*existing)[1].Get<uint32>()};
    }
    if (!db.Write(EventInsert, observation.eventKey, observation.sceneId, observation.authorId,
        observation.channel, observation.mapId, observation.instanceId, observation.zoneId,
        observation.createdMs, observation.evidence, observation.text))
        return std::nullopt;
    auto id = db.Read(LastId);
    if (!id)
        return std::nullopt;
    SourceVersion source{(*id)[0].Get<uint64>(), 1};
    std::set<std::string> unique(witnesses.begin(), witnesses.end());
    for (auto const& witness : unique)
        if (!db.Write(WitnessInsert, source.id, witness) ||
            !db.Write(ActorObserved, observation.createdMs, witness))
            return std::nullopt;
    if (!transaction.Commit())
        return std::nullopt;
    return source;
}

bool CharacterStore::PrepareDelivery(std::string const& actor, uint64_t version,
    ObservationRecord const& observation, std::vector<std::string> const& witnesses)
{
    if (witnesses.empty() || observation.authorId != actor || observation.evidence != "delivered")
        return false;
    std::lock_guard lock(_impl->mutex);
    if (!_impl->Ready())
        return false;
    auto& db = *_impl->connection;
    SqlTransaction transaction(db);
    if (!transaction)
        return false;
    auto owner = db.Read(ActorSelect, actor);
    if (!owner || (*owner)[4].Get<uint64>() != version)
        return false;
    pbc_json event = {{"event_key", observation.eventKey}, {"scene", observation.sceneId},
        {"author", observation.authorId}, {"channel", observation.channel}, {"map", observation.mapId},
        {"instance", observation.instanceId}, {"zone", observation.zoneId}, {"created_ms", observation.createdMs},
        {"text", observation.text}};
    // A duplicate intent is deliberately not permission to transmit again. If an
    // acknowledgement was lost, leave the pending attempt for inspection after restart.
    return db.Write(DeliveryInsert, observation.eventKey, actor, version, event.dump(),
        pbc_json(witnesses).dump()) && transaction.Commit();
}

bool CharacterStore::CloseDelivery(std::string const& eventKey, std::optional<SourceVersion> delivered)
{
    std::lock_guard lock(_impl->mutex);
    if (!_impl->Ready())
        return false;
    return _impl->connection->Write(DeliveryClose, std::string(delivered ? "committed" : "cancelled"),
        delivered ? delivered->id : uint64_t{0}, eventKey);
}

bool CharacterStore::ReviseSource(SourceVersion source, std::string const& text, bool excluded, bool administrator)
{
    if (!administrator || text.empty() || text.size() > 65536)
        return false;
    std::lock_guard lock(_impl->mutex);
    if (!_impl->Ready())
        return false;
    auto& db = *_impl->connection;
    SqlTransaction transaction(db);
    if (!transaction)
        return false;
    auto current = db.Read(SourceById, source.id);
    if (!current || (*current)[0].Get<uint32>() != source.version || !db.Write(SourceArchive, source.id) ||
        !db.Write(SourceRevise, text, static_cast<uint8>(excluded), source.id))
        return false;
    // Preserve explicit owner corrections; only generated interpretations of changed
    // evidence are invalidated. The old source text remains available in revision history.
    if (auto notes = db.Read(SourceNotes, source.id))
        do
        {
            auto id = (*notes)[0].Get<uint64>();
            if (!db.Write(RecallInvalidate, id) || !db.Write(NotesReopen, id) || !db.Write(NoteInvalidate, id))
                return false;
        } while (notes->NextRow());
    return db.Write(WitnessActorsEdited, source.id) && transaction.Commit();
}

std::vector<ObservationRecord> CharacterStore::Observations(std::string const& actor, bool pending, uint32_t limit)
{
    std::lock_guard lock(_impl->mutex);
    std::vector<ObservationRecord> result;
    if (!_impl->Ready())
        return result;
    auto rows = _impl->connection->Read(pending ? PendingSelect : RecentSelect, actor, std::min(limit, 5000u));
    if (!rows)
        return result;
    do
    {
        auto const& row = *rows;
        ObservationRecord observation;
        observation.source = {row[0].Get<uint64>(), row[1].Get<uint32>()};
        observation.eventKey = row[2].Get<std::string>();
        observation.sceneId = row[3].Get<std::string>();
        observation.authorId = row[4].Get<std::string>();
        observation.channel = row[5].Get<std::string>();
        observation.evidence = row[6].Get<std::string>();
        observation.mapId = row[7].Get<uint32>();
        observation.instanceId = row[8].Get<uint32>();
        observation.zoneId = row[9].Get<uint32>();
        observation.createdMs = row[10].Get<uint64>();
        observation.text = row[11].Get<std::string>();
        result.push_back(std::move(observation));
    } while (rows->NextRow());
    if (!pending)
        std::reverse(result.begin(), result.end());
    return result;
}

std::optional<ObservationRecord> CharacterStore::Source(std::string const& actor, uint64_t id)
{
    std::lock_guard lock(_impl->mutex);
    if (!_impl->Ready())
        return std::nullopt;
    auto row = _impl->connection->Read(SourceRead, actor, id);
    if (!row)
        return std::nullopt;
    ObservationRecord result;
    result.source = {(*row)[0].Get<uint64>(), (*row)[1].Get<uint32>()};
    result.eventKey = (*row)[2].Get<std::string>();
    result.sceneId = (*row)[3].Get<std::string>();
    result.authorId = (*row)[4].Get<std::string>();
    result.channel = (*row)[5].Get<std::string>();
    result.evidence = (*row)[6].Get<std::string>();
    result.mapId = (*row)[7].Get<uint32>();
    result.instanceId = (*row)[8].Get<uint32>();
    result.zoneId = (*row)[9].Get<uint32>();
    result.createdMs = (*row)[10].Get<uint64>();
    result.text = (*row)[11].Get<std::string>();
    return result;
}

std::vector<NoteRecord> CharacterStore::Notes(std::string const& actor, uint32_t limit, bool activeOnly)
{
    std::lock_guard lock(_impl->mutex);
    std::vector<NoteRecord> result;
    if (!_impl->Ready())
        return result;
    auto rows = _impl->connection->Read(activeOnly ? ActiveNoteSelect : NoteSelect, actor, std::min(limit, 5000u));
    if (!rows)
        return result;
    do
    {
        auto const& row = *rows;
        result.push_back({row[0].Get<uint64>(), row[1].Get<uint32>(), row[2].Get<std::string>(),
            row[3].Get<std::string>(), row[4].Get<std::string>(), row[5].Get<std::string>(),
            row[6].Get<std::string>(), row[7].Get<std::string>(), row[8].Get<uint8>() != 0,
            row[9].Get<uint64>() != 0, row[10].Get<uint64>(), {}});
    } while (rows->NextRow());
    std::reverse(result.begin(), result.end());
    std::unordered_map<uint64_t, NoteRecord*> indexed;
    for (auto& note : result)
        indexed.emplace(note.id, &note);
    if (auto sources = _impl->connection->Read(activeOnly ? ActiveNoteSources : NoteSources,
        actor, std::min(limit, 5000u)))
        do
        {
            if (auto note = indexed.find((*sources)[0].Get<uint64>()); note != indexed.end())
                note->second->sources.push_back({(*sources)[1].Get<uint64>(), (*sources)[2].Get<uint32>()});
        } while (sources->NextRow());
    return result;
}

std::vector<NoteRecord> CharacterStore::Reports(std::string const& watch, std::set<std::string> const& subjects,
    uint64_t nowMs, uint64_t lifetimeMs)
{
    std::lock_guard lock(_impl->mutex);
    std::vector<NoteRecord> result;
    if (!_impl->Ready())
        return result;
    auto rows = _impl->connection->Read(ReportSelect, watch, nowMs > lifetimeMs ? nowMs - lifetimeMs : 0,
        nowMs ? nowMs : UINT64_MAX, pbc_json(subjects).dump());
    if (rows)
        do
        {
            auto const& row = *rows;
            result.push_back({row[0].Get<uint64>(), row[1].Get<uint32>(), row[2].Get<std::string>(),
                row[3].Get<std::string>(), row[4].Get<std::string>(), row[5].Get<std::string>(),
                row[6].Get<std::string>(), row[7].Get<std::string>(), row[8].Get<uint8>() != 0,
                row[9].Get<uint64>() != 0, row[10].Get<uint64>(), {}, row[11].Get<std::string>()});
        } while (rows->NextRow());
    std::reverse(result.begin(), result.end());
    return result;
}

std::vector<std::pair<NoteRecord, uint32_t>> CharacterStore::GroupReports(std::string const& group, uint64_t sinceMs)
{
    std::lock_guard lock(_impl->mutex);
    std::vector<std::pair<NoteRecord, uint32_t>> result;
    if (!_impl->Ready())
        return result;
    if (auto rows = _impl->connection->Read(GroupReportList, group, sinceMs))
        do
        {
            auto const& row = *rows;
            result.push_back({{row[0].Get<uint64>(), row[1].Get<uint32>(), row[2].Get<std::string>(),
                row[3].Get<std::string>(), row[4].Get<std::string>(), row[5].Get<std::string>(),
                row[6].Get<std::string>(), row[7].Get<std::string>(), row[8].Get<uint8>() != 0,
                row[9].Get<uint64>() != 0, row[10].Get<uint64>(), {}, row[11].Get<std::string>()},
                row[12].Get<uint32>()});
        } while (rows->NextRow());
    return result;
}

bool CharacterStore::ForgetNote(std::string const& actor, uint64_t noteId, uint32_t expectedVersion,
    uint64_t editorGuid, bool administrator)
{
    std::lock_guard lock(_impl->mutex);
    if (!_impl->Ready())
        return false;
    auto& db = *_impl->connection;
    SqlTransaction transaction(db);
    if (!transaction)
        return false;
    auto owner = db.Read(ActorSelect, actor);
    if (!owner || (!administrator && (!editorGuid || (*owner)[2].Get<uint64>() != editorGuid)))
        return false;
    auto note = db.Read(NoteById, noteId);
    if (!note || (*note)[0].Get<uint32>() != expectedVersion || (*note)[1].Get<std::string>() != actor)
        return false;
    // Recall built from it is rebuilt; the note itself stays in the database, no longer used.
    return db.Write(RecallInvalidate, noteId) && db.Write(NotesReopen, noteId) && db.Write(NoteInvalidate, noteId) &&
        db.Write(ActorEdited, actor) && transaction.Commit();
}

std::vector<ObservationRecord> CharacterStore::HeardBy(std::string const& witness, uint32_t limit)
{
    std::lock_guard lock(_impl->mutex);
    std::vector<ObservationRecord> lines;
    if (!_impl->Ready())
        return lines;
    if (auto rows = _impl->connection->Read(HeardLines, witness, witness, limit))
        do
        {
            ObservationRecord line;
            line.source.id = (*rows)[0].Get<uint64>();
            line.sceneId = (*rows)[1].Get<std::string>();
            line.authorId = (*rows)[2].Get<std::string>();
            line.channel = (*rows)[3].Get<std::string>();
            line.createdMs = (*rows)[4].Get<uint64>();
            line.text = (*rows)[5].Get<std::string>();
            lines.push_back(std::move(line));
        } while (rows->NextRow());
    return lines;
}

std::optional<std::pair<std::string, std::string>> CharacterStore::Opening(std::string const& sceneId)
{
    std::lock_guard lock(_impl->mutex);
    if (!_impl->Ready())
        return std::nullopt;
    auto row = _impl->connection->Read(SceneOpening, sceneId);
    return row ? std::optional<std::pair<std::string, std::string>>({(*row)[0].Get<std::string>(), (*row)[1].Get<std::string>()})
               : std::nullopt;
}

std::vector<std::pair<std::string, std::string>> CharacterStore::NotesAboutSubject(std::string const& actor,
    std::string const& subject)
{
    std::lock_guard lock(_impl->mutex);
    std::vector<std::pair<std::string, std::string>> notes;
    if (!_impl->Ready())
        return notes;
    if (auto rows = _impl->connection->Read(NotesAbout, actor, subject))
        do
        {
            notes.emplace_back((*rows)[0].Get<std::string>(), (*rows)[1].Get<std::string>());
        } while (rows->NextRow());
    return notes;
}

std::vector<std::tuple<std::string, std::string, std::string>> CharacterStore::ActionsOfScene(std::string const& sceneId)
{
    std::lock_guard lock(_impl->mutex);
    std::vector<std::tuple<std::string, std::string, std::string>> actions;
    if (!_impl->Ready())
        return actions;
    if (auto rows = _impl->connection->Read(SceneActions, sceneId))
        do
        {
            actions.emplace_back((*rows)[0].Get<std::string>(), (*rows)[1].Get<std::string>(), (*rows)[2].Get<std::string>());
        } while (rows->NextRow());
    return actions;
}

std::vector<std::string> CharacterStore::PendingActors(uint64_t nowMs, uint32_t inactiveMs,
    uint32_t tokenThreshold, uint32_t limit)
{
    std::lock_guard lock(_impl->mutex);
    std::vector<std::string> result;
    if (!_impl->Ready())
        return result;
    uint64_t cutoff = nowMs >= inactiveMs ? nowMs - inactiveMs : 0;
    auto rows = _impl->connection->Read(DueActors, cutoff, nowMs, inactiveMs,
        static_cast<uint64_t>(tokenThreshold) * 4, std::min(limit, 64u));
    if (rows)
        do
        {
            result.push_back((*rows)[0].Get<std::string>());
        } while (rows->NextRow());
    return result;
}

bool CharacterStore::Extract(std::string const& actor, uint64_t version, std::string const& allowedWatch,
    std::vector<NoteRecord> const& notes, std::vector<SourceVersion> const& covered)
{
    return ExtractGroups(actor, version, allowedWatch.empty() ? std::set<std::string>{} :
        std::set<std::string>{allowedWatch}, notes, covered);
}

bool CharacterStore::ExtractGroups(std::string const& actor, uint64_t version,
    std::set<std::string> const& allowedGroups, std::vector<NoteRecord> const& notes,
    std::vector<SourceVersion> const& covered)
{
    std::lock_guard lock(_impl->mutex);
    if (!_impl->Ready())
        return false;
    auto& db = *_impl->connection;
    SqlTransaction transaction(db);
    if (!transaction)
        return false;
    auto owner = db.Read(ActorSelect, actor);
    if (!owner || (*owner)[4].Get<uint64>() != version)
        return false;
    for (auto const& source : covered)
        if (!db.Read(SourceCheck, actor, source.id, source.version))
            return false;
    for (auto const& note : notes)
    {
        bool shared = note.owner != actor || (*owner)[1].Get<std::string>() == "watch";
        if (note.owner != actor && (!allowedGroups.contains(note.owner) || note.kind != "report"))
            return false;
        if (shared && note.kind != "report")
            return false;
        if (note.text.empty() || note.sources.empty() || note.authority != "model")
            return false;
        bool duplicate = false;
        uint64_t createdMs = note.createdMs;
        for (auto const& source : note.sources)
        {
            if (shared)
            {
                auto evidence = db.Read(GroupSourceCheck, actor, source.id, source.version, note.owner);
                if (!evidence)
                    return false;
                createdMs = std::min(createdMs, (*evidence)[0].Get<uint64>());
                duplicate |= bool(db.Read(ReportDuplicate, note.owner, note.subject, source.id, source.version));
            }
            else if (!db.Read(SourceCheck, actor, source.id, source.version))
                return false;
        }
        if (duplicate || db.Read(NoteByOperation, note.operationId, note.owner))
            continue;
        if (!db.Write(NoteInsert, note.operationId, note.owner, note.subject, note.kind, note.text, createdMs))
            return false;
        auto id = db.Read(LastId);
        if (!id)
            return false;
        for (auto const& source : note.sources)
            if (!db.Write(NoteSourceInsert, (*id)[0].Get<uint64>(), source.id, source.version) ||
                (shared && !db.Write(WitnessProcessed, source.version, note.owner, source.id, source.version)))
                return false;
    }
    for (auto const& source : covered)
        if (!db.Write(WitnessProcessed, source.version, actor, source.id, source.version))
            return false;
    return transaction.Commit();
}

bool CharacterStore::Compact(std::string const& actor, uint64_t version, uint64_t recallVersion,
    std::string const& recall, std::vector<NoteRecord> const& inputs)
{
    if (recall.empty() || recall.size() > 32000 || inputs.empty())
        return false;
    std::lock_guard lock(_impl->mutex);
    if (!_impl->Ready())
        return false;
    auto& db = *_impl->connection;
    SqlTransaction transaction(db);
    if (!transaction)
        return false;
    auto owner = db.Read(ActorSelect, actor);
    if (!owner || (*owner)[4].Get<uint64>() != version || (*owner)[8].Get<uint64>() != recallVersion)
        return false;
    for (auto const& input : inputs)
    {
        auto row = db.Read(NoteById, input.id);
        if (!row || (*row)[0].Get<uint32>() != input.version || (*row)[1].Get<std::string>() != actor ||
            !(*row)[2].Get<uint8>())
            return false;
    }
    if (!db.Write(RecallUpdate, recall, actor, version, recallVersion) ||
        !db.Write(RecallCopy, recallVersion + 1, actor, recallVersion))
        return false;
    for (auto const& input : inputs)
        if (!db.Write(NoteCompacted, recallVersion + 1, input.id, actor) ||
            !db.Write(RecallNoteInsert, actor, recallVersion + 1, input.id, input.version))
            return false;
    return transaction.Commit();
}

bool CharacterStore::EditNote(std::string const& actor, uint64_t noteId, uint32_t expectedVersion,
    std::string const& text, uint64_t editorGuid, bool administrator)
{
    if (text.empty() || text.size() > 4000)
        return false;
    std::lock_guard lock(_impl->mutex);
    if (!_impl->Ready())
        return false;
    auto& db = *_impl->connection;
    SqlTransaction transaction(db);
    if (!transaction)
        return false;
    auto owner = db.Read(ActorSelect, actor);
    if (!owner || (!administrator && (!editorGuid || (*owner)[2].Get<uint64>() != editorGuid)))
        return false;
    auto note = db.Read(NoteById, noteId);
    if (!note || (*note)[0].Get<uint32>() != expectedVersion || (*note)[1].Get<std::string>() != actor)
        return false;
    return db.Write(NoteEdit, text, noteId) && db.Write(ActorEdited, actor) &&
        db.Write(RecallInvalidate, noteId) && db.Write(NotesReopen, noteId) && transaction.Commit();
}

bool CharacterStore::AddOwnerFact(std::string const& actor, uint64_t version, std::string const& operationId,
    std::string const& text, uint64_t editorGuid, bool administrator, uint64_t nowMs)
{
    if (text.empty() || text.size() > 4000 || operationId.empty())
        return false;
    std::lock_guard lock(_impl->mutex);
    if (!_impl->Ready())
        return false;
    auto& db = *_impl->connection;
    SqlTransaction transaction(db);
    if (!transaction)
        return false;
    auto owner = db.Read(ActorSelect, actor);
    if (!owner || (*owner)[4].Get<uint64>() != version ||
        (!administrator && (!editorGuid || (*owner)[2].Get<uint64>() != editorGuid)))
        return false;
    if (db.Read(NoteByOperation, operationId, actor))
        return true;
    return db.Write(OwnerFactInsert, operationId, actor, text, nowMs) &&
        db.Write(ActorEdited, actor) && transaction.Commit();
}

bool CharacterStore::ResolveNote(std::string const& actor, uint64_t noteId, uint32_t expectedVersion,
    uint64_t editorGuid, bool administrator)
{
    std::lock_guard lock(_impl->mutex);
    if (!_impl->Ready())
        return false;
    auto& db = *_impl->connection;
    SqlTransaction transaction(db);
    if (!transaction)
        return false;
    auto owner = db.Read(ActorSelect, actor);
    if (!owner || (!administrator && (!editorGuid || (*owner)[2].Get<uint64>() != editorGuid)))
        return false;
    auto note = db.Read(NoteById, noteId);
    if (!note || (*note)[0].Get<uint32>() != expectedVersion || (*note)[1].Get<std::string>() != actor)
        return false;
    return db.Write(NoteResolve, noteId) && db.Write(ActorEdited, actor) &&
        db.Write(RecallInvalidate, noteId) && db.Write(NotesReopen, noteId) && transaction.Commit();
}
}
