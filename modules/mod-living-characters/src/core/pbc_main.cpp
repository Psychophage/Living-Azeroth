// PBC Character System changes, 2026-09-30; see NOTICE.md for upstream attribution.
#include "pbc_config.h"
#include "pbc_world.h"
#include "pbc_commands.h"
#include "pbc_log.h"
#include "pbc_player_scripts.h"
#include "pbc_group_scripts.h"
#include "pbc_quest_scripts.h"

void AddPBCCharacterHooks();

// The module loader calls Add<directory name>Scripts().
void Addmod_living_charactersScripts()
{
    PBC_Log(PBC_LogLevel::PBC_DEFAULT, "Registering mod-living-characters scripts.");

    new PBC_WorldScript();
    new PBC_PlayerEvents();
    new PBC_GroupEvents();
    new PBC_AllCreatureQuestScript();
    new PBC_AllGameObjectQuestScript();
    new PBC_AllItemQuestScript();
    new PBC_CommandScript();
    AddPBCCharacterHooks();
}
