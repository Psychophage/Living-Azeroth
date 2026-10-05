// SPDX-License-Identifier: GPL-2.0-or-later
// PBC Character System changes, 2026-09-30; upstream attribution in NOTICE.md.

#ifndef PBC_NATIVE_H
#define PBC_NATIVE_H

#include <cstdint>
#include <set>
#include <string>

class Creature;
class Player;
class Unit;

namespace PBC
{
bool EligibleNpc(Player* listener, Creature* creature, float range);
std::string RaceName(uint8_t race);
std::string ClassName(uint8_t characterClass);
std::string NativeEmoteName(uint32_t textEmote);
bool HasNativeEmoteScript(Creature* creature, uint32_t textEmote);
void PlayNativeAnimation(Unit* unit, std::string const& animation);
std::set<std::string> NativeAnimations();
}  // namespace PBC

#endif
