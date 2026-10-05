// PBC Character System changes, 2026-09-30; see NOTICE.md for upstream attribution.
#include "pbc_conversation.h"
#include <algorithm>
#include <charconv>
#include <cctype>

namespace PBC
{
bool Conversation::Begin(Contribution cause, bool humanAudience, std::set<std::string> eligible,
    uint8_t allowance, uint64_t nowMs)
{
    if (cause == Contribution::Generated || !humanAudience || eligible.empty() || !allowance)
        return false;
    ++_version;
    _actorVersion = 0;
    _eligible = std::move(eligible);
    _remaining = std::min<uint8_t>(allowance, 8);
    _actor.clear();
    _requestId.clear();
    _reply = {};
    _next = 0;
    _aborted = false;
    _dueMs = nowMs;
    _phase = ConversationPhase::Selecting;
    return true;
}

bool Conversation::Choose(uint64_t version, std::string const& actor, uint64_t actorVersion)
{
    if (version != _version || _phase != ConversationPhase::Selecting)
        return false;
    if (actor == "STOP")
    {
        _phase = ConversationPhase::Done;
        return true;
    }
    if (!_eligible.contains(actor) || !_remaining || actor == _actor)
        return false;
    _actor = actor;
    _actorVersion = actorVersion;
    _reply = {};
    _next = 0;
    _phase = ConversationPhase::Generating;
    return true;
}

bool Conversation::Accept(uint64_t version, uint64_t actorVersion, Dialogue dialogue,
    std::string requestId, uint64_t nowMs)
{
    if (version != _version || _phase != ConversationPhase::Generating)
        return false;
    if (actorVersion != _actorVersion)
    {
        _phase = ConversationPhase::Done;
        return false;
    }
    _reply = std::move(dialogue);
    _requestId = std::move(requestId);
    _dueMs = std::max(_dueMs, nowMs);
    --_remaining;
    _phase = _reply.segments.empty() ? ConversationPhase::Persisting : ConversationPhase::Playing;
    return true;
}

std::optional<DueSegment> Conversation::Due(uint64_t nowMs) const
{
    if (_phase != ConversationPhase::Playing || nowMs < _dueMs || _next >= _reply.segments.size())
        return std::nullopt;
    return DueSegment{_version, _next, _reply.segments[_next]};
}

bool Conversation::Delivered(uint64_t version, std::size_t index, uint64_t nowMs)
{
    if (version != _version || _phase != ConversationPhase::Playing ||
        index != _next || nowMs < _dueMs || _next >= _reply.segments.size())
        return false;
    uint64_t readingMs = 0;
    if (_readingWordsPerMinute)
    {
        uint64_t words = 0;
        bool inWord = false;
        for (unsigned char character : _reply.segments[_next].text)
        {
            bool space = std::isspace(character);
            if (!space && !inWord)
                ++words;
            inWord = !space;
        }
        readingMs = (words * 60000 + _readingWordsPerMinute - 1) / _readingWordsPerMinute;
    }
    ++_next;
    _dueMs = nowMs + _spacingMs + readingMs;
    if (_next == _reply.segments.size())
        _phase = ConversationPhase::Persisting;
    return true;
}

void Conversation::Abort()
{
    _aborted = true;
    _remaining = 0;
    if (_phase == ConversationPhase::Playing || _phase == ConversationPhase::Persisting)
        _phase = ConversationPhase::Persisting;
    else
    {
        ++_version; // In-flight selector/generation results are obsolete.
        _phase = ConversationPhase::Done;
    }
}

bool Conversation::Persisted(uint64_t version)
{
    if (version != _version || _phase != ConversationPhase::Persisting)
        return false;
    // Declining to speak contributes no new exchange for another character to answer.
    _phase = !_aborted && _remaining && _eligible.size() > 1 && !_reply.segments.empty() ?
        ConversationPhase::Selecting : ConversationPhase::Done;
    return true;
}

std::vector<std::size_t> Conversation::ReadyNotes() const
{
    std::vector<std::size_t> result;
    for (std::size_t i = 0; i < _reply.notes.size(); ++i)
    {
        bool ready = true;
        for (auto const& source : _reply.notes[i].sources)
        {
            if (!source.starts_with("reply:"))
                continue;
            std::size_t index = 0;
            auto parsed = std::from_chars(source.data() + 6, source.data() + source.size(), index);
            if (parsed.ec != std::errc() || parsed.ptr != source.data() + source.size() || index >= _next)
            {
                ready = false;
                break;
            }
        }
        if (ready)
            result.push_back(i);
    }
    return result;
}

std::string Conversation::DeliveryOutcome() const
{
    if (_reply.segments.empty())
        return "silent";
    if (_next == _reply.segments.size())
        return "delivered";
    return _next ? "partial" : "cancelled";
}
}
