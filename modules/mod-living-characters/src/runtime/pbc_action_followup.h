// PBC Character System changes, 2026-09-30; see NOTICE.md for upstream attribution.
// Living Azeroth: a clarification retains authorization, not an executable task.
#ifndef PBC_ACTION_FOLLOWUP_H
#define PBC_ACTION_FOLLOWUP_H

#include "pbc_actions.h"
#include "pbc_model.h"
#include <algorithm>
#include <set>

namespace PBC
{
struct ActionFeedback
{
    std::set<std::string> actors;
    std::set<std::string> missing;
    std::string status = "needs_information";
};

// A conversational reference never grants fresh authority. Every new input
// rechecks master, party and object incarnation against its own native snapshot.
inline bool ActionActorsValid(std::vector<ActionRequest> const& previous, std::vector<ActionRequest> const& current)
{
    return !previous.empty() &&
           std::all_of(previous.begin(), previous.end(),
                       [&](auto const& old)
                       {
                           return std::any_of(current.begin(), current.end(),
                                              [&](auto const& fresh)
                                              {
                                                  auto same = [](auto const& a, auto const& b)
                                                  {
                                                      return a.guid == b.guid && a.map == b.map &&
                                                             a.instance == b.instance && a.incarnation == b.incarnation;
                                                  };
                                                  return same(old.actor, fresh.actor) &&
                                                         same(old.requester, fresh.requester) &&
                                                         old.authorityVersion == fresh.authorityVersion &&
                                                         old.actorGroup == fresh.actorGroup;
                                              });
                       });
}

struct RecentCombatOrder
{
    std::string contribution;
    std::string channel;
    std::string factsJson;
    std::vector<ActionRequest> actors;
    uint64_t expiresMs = 0;

    bool Valid(std::vector<ActionRequest> const& current, std::string const& audience, uint64_t nowMs) const
    {
        return nowMs < expiresMs && channel == audience && ActionActorsValid(actors, current);
    }
};

struct PendingAction
{
    std::string contribution;
    std::string channel;
    std::map<std::string, std::string> decisions;
    std::set<std::string> missing;
    std::vector<ActionRequest> actors;
    std::map<std::string, PlayerbotDialogue::Entity> targets;
    uint64_t expiresMs = 0;

    bool Valid(std::vector<ActionRequest> const& current, std::string const& audience, uint64_t nowMs) const
    {
        if (nowMs >= expiresMs || channel != audience || actors.empty() || missing.empty())
            return false;
        return ActionActorsValid(actors, current);
    }

    ModelReply Merge(ModelReply reply) const
    {
        auto compatible = reply.decisions.find("pending_answer");
        if (!reply.success || compatible == reply.decisions.end() || compatible->second != "YES")
            return reply;
        for (auto const& [field, value] : decisions)
            if (!missing.contains(field))
                reply.decisions[field] = value;

        for (auto const& [field, value] : decisions)
            if (field.starts_with("item") && !missing.contains(field))
                reply.itemSets.erase(field.substr(4));
        // Inference supplies only the missing arguments. Fresh native snapshots
        // still decide whether the resulting request can actually execute.
        return reply;
    }

    bool TargetsValid(std::map<std::string, PlayerbotDialogue::Entity> const& current) const
    {
        return std::all_of(targets.begin(), targets.end(),
                           [&](auto const& old)
                           {
                               auto found = current.find(old.first);
                               return found != current.end() && found->second.guid == old.second.guid &&
                                      found->second.map == old.second.map &&
                                      found->second.instance == old.second.instance &&
                                      found->second.incarnation == old.second.incarnation;
                           });
    }
};
}  // namespace PBC

#endif
