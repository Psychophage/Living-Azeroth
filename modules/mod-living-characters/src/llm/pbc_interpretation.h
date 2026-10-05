// SPDX-License-Identifier: GPL-2.0-or-later
// Living Azeroth: optional selection over complete meanings, before feasibility.
#ifndef PBC_INTERPRETATION_H
#define PBC_INTERPRETATION_H

#include "pbc_json.h"
#include <map>
#include <string>

namespace PBC
{
struct InterpretationReading
{
    std::map<std::string, std::string> choices;
    std::map<std::string, uint32_t> items;
    std::string description;
};

// Provider probabilities rank alternatives; they never authorize execution or
// establish calibrated joint correctness. OTHER always remains available.
bool NeedsCompleteReading(pbc_json const& request, pbc_json const& answers);
std::map<std::string, InterpretationReading> CompleteReadings(pbc_json const& request, pbc_json const& answers);
bool AmbiguousCompleteReading(std::map<std::string, InterpretationReading> const& readings, pbc_json const& answer,
                              pbc_json const& nativeState);
}  // namespace PBC
#endif
