// PBC Character System changes, 2026-09-30; see NOTICE.md for upstream attribution.
// Living Azeroth: durable character identity and witnessed evidence.
#ifndef PBC_STORE_H
#define PBC_STORE_H

#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <tuple>
#include <utility>
#include <string>
#include <vector>

namespace PBC
{
struct ActorRecord
{
    std::string id;
    std::string kind;
    uint64_t ownerGuid = 0;
    std::string name;
    uint64_t version = 1;
    std::string foundation;
    std::string summary;
    std::string recall;
    uint64_t recallVersion = 0;
    uint64_t lastObservedMs = 0;
};

struct SourceVersion
{
    uint64_t id = 0;
    uint32_t version = 0;
    std::string Key() const;
    static std::optional<SourceVersion> Parse(std::string const& key);
};

struct ObservationRecord
{
    SourceVersion source;
    std::string eventKey;
    std::string sceneId;
    std::string authorId;
    std::string channel;
    std::string evidence = "observed";
    uint32_t mapId = 0;
    uint32_t instanceId = 0;
    uint32_t zoneId = 0;
    uint64_t createdMs = 0;
    std::string text;
};

struct NoteRecord
{
    uint64_t id = 0;
    uint32_t version = 1;
    std::string operationId;
    std::string owner;
    std::string subject;
    std::string kind;
    std::string text;
    std::string authority = "model";
    bool resolved = false;
    bool compacted = false;
    uint64_t createdMs = 0;
    std::vector<SourceVersion> sources;
    std::string reportedBy; // Native author IDs of cited observations, not model attribution.
};

struct NpcIdentity
{
    uint32_t spawnId = 0;
    uint32_t mapId = 0;
    uint32_t instanceId = 0;
    std::string actorId;
    std::string watchId;
};

// Call on storage/model workers. Operations use one connection and transaction;
// IDs are never fetched through a different pooled connection.
class CharacterStore
{
public:
    CharacterStore();
    ~CharacterStore();
    bool Open(std::string const& connectionInfo);
    bool EnsureActor(ActorRecord const& actor);
    bool AssignOwner(std::string const& actor, uint64_t ownerGuid);
    bool MapNpc(NpcIdentity const& identity);
    std::optional<NpcIdentity> Npc(uint32_t spawnId, uint32_t mapId, uint32_t instanceId);
    std::optional<ActorRecord> Actor(std::string const& id);
    bool Foundation(std::string const& actor, uint64_t version, std::string const& text,
        std::string const& summary);
    std::optional<SourceVersion> Observe(ObservationRecord const& observation,
        std::vector<std::string> const& witnesses);
    bool PrepareDelivery(std::string const& actor, uint64_t version, ObservationRecord const& observation,
        std::vector<std::string> const& witnesses);
    bool CloseDelivery(std::string const& eventKey, std::optional<SourceVersion> delivered);
    bool ReviseSource(SourceVersion source, std::string const& text, bool excluded, bool administrator);
    std::vector<ObservationRecord> Observations(std::string const& actor, bool pending, uint32_t limit = 512);
    std::optional<ObservationRecord> Source(std::string const& actor, uint64_t id);
    std::vector<NoteRecord> Notes(std::string const& actor, uint32_t limit = 1024, bool activeOnly = false);
    std::vector<NoteRecord> Reports(std::string const& watch, std::set<std::string> const& subjects,
        uint64_t nowMs = 0, uint64_t lifetimeMs = 259200000);
    std::vector<std::string> PendingActors(uint64_t nowMs, uint32_t inactiveMs = 300000,
        uint32_t tokenThreshold = 4096, uint32_t limit = 16);
    bool Extract(std::string const& actor, uint64_t version, std::string const& allowedWatch,
        std::vector<NoteRecord> const& notes, std::vector<SourceVersion> const& covered);
    bool ExtractGroups(std::string const& actor, uint64_t version, std::set<std::string> const& allowedGroups,
        std::vector<NoteRecord> const& notes, std::vector<SourceVersion> const& covered);
    bool Compact(std::string const& actor, uint64_t version, uint64_t recallVersion,
        std::string const& recall, std::vector<NoteRecord> const& inputs);
    bool EditNote(std::string const& actor, uint64_t noteId, uint32_t expectedVersion,
        std::string const& text, uint64_t editorGuid, bool administrator);
    bool AddOwnerFact(std::string const& actor, uint64_t version, std::string const& operationId,
        std::string const& text, uint64_t editorGuid, bool administrator, uint64_t nowMs);
    bool ResolveNote(std::string const& actor, uint64_t noteId, uint32_t expectedVersion,
        uint64_t editorGuid, bool administrator);
    // A group's reports since a time, resolved included, newest first, each with the zone it came from.
    std::vector<std::pair<NoteRecord, uint32_t>> GroupReports(std::string const& group, uint64_t sinceMs);
    // Lines others spoke that were delivered to this witness, newest first (id, scene, author, channel, time, text).
    std::vector<ObservationRecord> HeardBy(std::string const& witness, uint32_t limit);
    // What started a scene: (author, text) of its first observed input.
    std::optional<std::pair<std::string, std::string>> Opening(std::string const& sceneId);
    // An actor's current notes about one subject: (kind, text), newest first.
    std::vector<std::pair<std::string, std::string>> NotesAboutSubject(std::string const& actor, std::string const& subject);
    // The native actions a scene asked for: (intent JSON, status, detail).
    std::vector<std::tuple<std::string, std::string, std::string>> ActionsOfScene(std::string const& sceneId);
    // Stops a note being used (a rumour the group should forget); its row and sources remain.
    bool ForgetNote(std::string const& actor, uint64_t noteId, uint32_t expectedVersion,
        uint64_t editorGuid, bool administrator);
    // A player's chosen hearing settings as stored JSON; none means the realm's.
    std::optional<std::string> Listener(uint32_t playerGuid);
    bool SaveListener(uint32_t playerGuid, std::string const& settings, uint64_t nowMs);
    // Written guild identities (pbc_guild.h) as stored JSON, by native guild ID.
    std::vector<std::pair<uint32_t, std::string>> GuildIdentities();
    bool SaveGuildIdentity(uint32_t guildId, std::string const& identity, uint32_t editorGuid, uint64_t nowMs);
    bool Healthy() const;

private:
    class Impl;
    std::unique_ptr<Impl> _impl;
};
}

#endif
