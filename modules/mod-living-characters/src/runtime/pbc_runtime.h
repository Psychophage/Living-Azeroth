// PBC Character System changes, 2026-09-30; see NOTICE.md for upstream attribution.
// Living Azeroth character coordinator; called only from game-thread hooks.
#ifndef PBC_RUNTIME_H
#define PBC_RUNTIME_H

#include "ObjectGuid.h"
#include <cstdint>
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
}

#endif
