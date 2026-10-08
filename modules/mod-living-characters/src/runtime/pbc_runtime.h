// PBC Character System changes, 2026-09-30; see NOTICE.md for upstream attribution.
// Living Azeroth character coordinator; called only from game-thread hooks.
#ifndef PBC_RUNTIME_H
#define PBC_RUNTIME_H

#include "ObjectGuid.h"
#include <cstdint>
#include <functional>
#include <string>

class Channel;
class Player;
class Unit;

namespace PBC
{
bool RuntimeConfigured();
bool StartRuntime();
void StopRuntime();
void UpdateRuntime(uint32_t diff);
void ObserveChat(Player* sender, uint32_t type, uint32_t language, std::string const& text,
    Player* receiver = nullptr, Channel* channel = nullptr);
void ObserveEmote(Player* sender, uint32_t textEmote, ObjectGuid target);
void ObserveWorldEvent(Player* subject, std::string const& text, bool partyOnly = false, bool interruptDialogue = false, Unit* enemy = nullptr);
void CancelActor(std::string const& actorId);
bool CharacterCommand(Player* player, std::string const& command);
// What this player hears from characters (pbc_listener.h), as JSON. False when the system is off.
bool ListenerSettingsFor(Player* player, std::string& json);
// Applies a JSON object of changes for this player; json is then the new settings.
bool ChangeListenerSettings(Player* player, std::string const& changes, std::string& json, std::string& error);
// The player's guild and its written identity (pbc_guild.h) as JSON, with whether they may edit it.
bool GuildIdentityFor(Player* player, std::string& json, std::string& error);
// Officers' changes to their guild's identity; json is then as GuildIdentityFor gives it.
bool ChangeGuildIdentity(Player* player, std::string const& changes, std::string& json, std::string& error);
// The rumours of the player's guild (or, for an administrator, of `group`): JSON with `ok`,
// `group`, `may_change` and `rumours`, given to `done` on the world thread. False when off.
bool GroupRumours(Player* player, std::string const& group, std::function<void(std::string)> done);
// {"note","version","action":"correct"|"resolve"|"forget","text"?,"group"?}; `done` gets {"ok",...}.
bool ChangeRumour(Player* player, std::string const& request, std::function<void(std::string)> done);
// With line 0, the last lines characters said to this player (`lines`); otherwise why that line
// was said: `started_by`, `remembers_you`, `actions`, `cost`. Only lines the player heard.
bool WhyLine(Player* player, uint64_t line, std::function<void(std::string)> done);
}

#endif
