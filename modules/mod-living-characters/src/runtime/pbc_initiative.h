// PBC Character System changes, 2026-09-30; see NOTICE.md for upstream attribution.
// Living Azeroth: character-owned choices, separate from shared human orders.
#ifndef PBC_INITIATIVE_H
#define PBC_INITIATIVE_H
#include "pbc_actions.h"
#include "pbc_game.h"

namespace PBC
{
struct InitiativeFrame
{
    std::map<std::string, ActionRequest> requests;
    std::map<std::string, std::string> descriptions;
};
InitiativeFrame CaptureInitiative(GameAudience const& audience, GameActor const& actor,
    std::string const& scene, uint64_t sequence, uint64_t nowMs);
}
#endif
