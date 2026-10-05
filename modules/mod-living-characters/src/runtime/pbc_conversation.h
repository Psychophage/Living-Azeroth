// PBC Character System changes, 2026-09-30; see NOTICE.md for upstream attribution.
// Living Azeroth conversation scheduling. All times are injected monotonic milliseconds.
#ifndef PBC_CONVERSATION_H
#define PBC_CONVERSATION_H

#include "pbc_dialogue.h"
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace PBC
{
enum class Contribution { Human, Ambient, Generated };
enum class ConversationPhase { Done, Selecting, Generating, Playing, Persisting };

struct DueSegment
{
    uint64_t version = 0;
    std::size_t index = 0;
    Segment segment;
};

// World-thread-owned state machine; no sleeping, networking, game pointers or DB IO.
// The runtime uses this same class with game time; tests advance its clock directly.
class Conversation
{
public:
    bool Begin(Contribution cause, bool humanAudience, std::set<std::string> eligible,
        uint8_t allowance, uint64_t nowMs);
    bool Choose(uint64_t version, std::string const& actor, uint64_t actorVersion);
    bool Accept(uint64_t version, uint64_t actorVersion, Dialogue dialogue,
        std::string requestId, uint64_t nowMs);
    std::optional<DueSegment> Due(uint64_t nowMs) const;
    bool Delivered(uint64_t version, std::size_t index, uint64_t nowMs);
    void Abort();
    bool Persisted(uint64_t version);
    std::vector<std::size_t> ReadyNotes() const;
    std::string DeliveryOutcome() const;

    uint64_t Version() const { return _version; }
    uint64_t ActorVersion() const { return _actorVersion; }
    ConversationPhase Phase() const { return _phase; }
    uint8_t Remaining() const { return _remaining; }
    std::string const& Actor() const { return _actor; }
    std::string const& RequestId() const { return _requestId; }
    Dialogue const& Reply() const { return _reply; }
    std::size_t DeliveredCount() const { return _next; }
    void SetSpacing(uint32_t spacingMs) { _spacingMs = spacingMs; }
    void SetReadingWordsPerMinute(uint32_t wordsPerMinute) { _readingWordsPerMinute = wordsPerMinute; }

private:
    uint64_t _version = 0;
    uint64_t _actorVersion = 0;
    uint64_t _dueMs = 0;
    uint32_t _spacingMs = 2000;
    uint32_t _readingWordsPerMinute = 150;
    uint8_t _remaining = 0;
    ConversationPhase _phase = ConversationPhase::Done;
    std::set<std::string> _eligible;
    std::string _actor;
    std::string _requestId;
    Dialogue _reply;
    std::size_t _next = 0;
    bool _aborted = false;
};
}

#endif
