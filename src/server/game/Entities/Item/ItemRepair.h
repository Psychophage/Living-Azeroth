// PBC Character System integration changes, 2026-09-30; upstream notices preserved.
/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

// Living Azeroth: one native price calculation for repair quotes and execution.
#ifndef AC_ITEM_REPAIR_H
#define AC_ITEM_REPAIR_H

#include "Define.h"
#include <optional>

class Item;
// Missing/bad DBC data cannot authorize a paid repair. Zero means no damage.
AC_GAME_API std::optional<uint32> CalculateItemRepairCost(Item const* item, float discount);

#endif
