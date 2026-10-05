// PBC Character System changes, 2026-09-30; see NOTICE.md for upstream attribution.
// Living Azeroth: small useful acts use native capability checks and exact live targets.
#include "pbc_initiative.h"
#include "ObjectAccessor.h"
#include "Group.h"
#include "PlayerbotDialogue.h"
#include "PlayerbotDialogueDuel.h"
#include "PlayerbotDialogueInventory.h"
#include "PlayerbotDialogueSupport.h"
#include "Playerbots.h"

namespace PBC
{
InitiativeFrame CaptureInitiative(GameAudience const& audience, GameActor const& actor, std::string const& scene,
                                  uint64_t sequence, uint64_t nowMs)
{
    InitiativeFrame frame;
    if (!actor.guid.IsPlayer())
        return frame;
    auto bot = ObjectAccessor::FindPlayer(actor.guid);
    auto human = ObjectAccessor::FindPlayer(audience.anchor);
    auto ai = bot ? GET_PLAYERBOT_AI(bot) : nullptr;
    if (!HasHumanConnection(human) || !ai)
        return frame;
    bool personal = PlayerbotDialogueBridge::AllowsInitiative(bot, human);
    bool invitation = PlayerbotDialogueBridge::AllowsInvitation(bot, human);
    if (!personal && !invitation)
        return frame;
    ActionRequest base;
    base.groupId = scene;
    base.sequence = sequence;
    base.actor = PlayerbotDialogueBridge::Snapshot(bot);
    base.requester = PlayerbotDialogueBridge::Snapshot(human);
    base.authorityVersion = PlayerbotDialogueBridge::Revision(base.actor.guid);
    base.deadlineMs = nowMs + 120000;
    base.voluntary = true;
    if (invitation)
    {
        auto request = base;
        request.kind = ActionKind::Invite;
        request.target = base.requester;
        request.id = scene + ":personal:" + std::to_string(sequence) + ":invite:human";
        frame.requests.emplace("invite:human", std::move(request));
        frame.descriptions.emplace(
            "invite:human",
            "Optionally volunteer to adventure with this nearby human by "
            "sending a normal party invitation. Choose only when they seek company/help and your character agrees. "
            "They must accept through the normal party interface; nothing joins or travels before consent. "
            "You know only their public request, not private quest objectives. No teleportation or automatic victory.");
    }
    // General participation conveys public words, never remote gameplay authority.
    // Only this nearby consensual invitation is available from the channel.
    if (audience.label == "general" || !personal)
        return frame;
    for (auto target : {human, bot})
        for (auto kind : {ActionKind::Buff, ActionKind::Heal, ActionKind::Cleanse})
        {
            if (!PlayerbotDialogue::HasUsefulSupport(bot, ai, target, kind))
                continue;
            auto option = ActionName(kind) + (target == human ? ":human" : ":self");
            auto request = base;
            request.kind = kind;
            request.target = PlayerbotDialogueBridge::Snapshot(target);
            request.id = scene + ":personal:" + std::to_string(sequence) + ":" + option;
            frame.requests.emplace(option, std::move(request));
            frame.descriptions.emplace(
                option,
                "Optionally " + ActionName(kind) +
                    (target == human ? " the nearby human who is talking with you." : " yourself.") +
                    " A currently known useful native spell exists; execution rechecks it. No promised success.");
        }
    if (PlayerbotDialogueBridge::AllowsInitiativeApproach(bot) && bot->GetExactDist(human) > 4.0f)
    {
        auto request = base;
        request.kind = ActionKind::Approach;
        request.target = base.requester;
        request.id = scene + ":personal:" + std::to_string(sequence) + ":approach:human";
        frame.requests.emplace("approach:human", std::move(request));
        frame.descriptions.emplace(
            "approach:human",
            "Optionally walk a short distance to the nearby human. "
            "Never override a stay/guard order; native checks revalidate. This may precede a supply offer.");
    }
    if (ai->GetMaster() == human && !bot->IsInCombat() &&
        !PlayerbotDialogueBridge::PendingOffer(base.actor.guid, base.requester.guid))
        for (auto const& item : PlayerbotDialogue::SupplyOptions(ai))
        {
            auto option = "offer:" + std::to_string(item.entry);
            auto request = base;
            request.kind = ActionKind::OfferSupplies;
            request.target = base.requester;
            request.objectId = item.entry;
            request.quantity = std::min(3u, item.count - 1);
            request.items = {item};
            request.id = scene + ":personal:" + std::to_string(sequence) + ":" + option;
            frame.descriptions.emplace(
                option, "Optionally offer " + std::to_string(request.quantity) + " x " + item.name +
                            " from your actual supplies. This only makes an offer, opens no trade and transfers "
                            "nothing. The human may accept/decline in dialogue, then must confirm the normal trade. "
                            "Use only if your character and the exchange make generosity appropriate.");
            frame.requests.emplace(option, std::move(request));
        }
    if (PlayerbotDialogue::CanOfferDuel(bot, human, nowMs))
    {
        auto request = base;
        request.kind = ActionKind::Duel;
        request.target = base.requester;
        request.id = scene + ":personal:" + std::to_string(sequence) + ":duel:human";
        frame.requests.emplace("duel:human", std::move(request));
        frame.descriptions.emplace(
            "duel:human",
            "Optionally challenge the nearby human to a native duel, "
            "only when the exchange and your character make it appropriate. They must accept normally. "
            "This is a challenge, never a forced fight. Silence or refusal is valid; do not challenge casually.");
    }
    return frame;
}
}  // namespace PBC
