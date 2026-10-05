// PBC Character System changes, 2026-09-30; see NOTICE.md for upstream attribution.
#ifndef MOD_PBC_IDLE_H
#define MOD_PBC_IDLE_H

#include "pbc_config.h"

#include <ctime>
#include <unordered_set>
#include <vector>

class Group;
class Player;

// Called by the existing party poll, once for each mixed human/bot group.
void PBC_IdlePollParty(Group* group, time_t now);
void PBC_IdleFinishPoll(std::unordered_set<uint32_t> const& seenGroups);

// Chat hooks and ordinary event admission advance the conversation revision.
void PBC_IdleNoteActivity(Player* player);
void PBC_IdleNoteEvent(PBC_EventItem const& event);

// Worker-side cancellation and staging; no live game objects are accessed.
bool PBC_IdleIsCurrent(PBC_IdleGuard const& guard);
void PBC_IdleStageCompletion(PBC_IdleGuard guard, std::vector<PBC_HistoryEntry> replies);

// World-thread completion commits both history and Party speech after revalidation.
void PBC_IdleDrainCompletions();

#endif // MOD_PBC_IDLE_H
