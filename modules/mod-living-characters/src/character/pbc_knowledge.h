// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef PBC_KNOWLEDGE_H
#define PBC_KNOWLEDGE_H

#include "pbc_json.h"
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace PBC
{
struct InformationGroup
{
    std::string id;
    std::string name;
    std::string description;
    uint64_t reportLifetimeMs = 259200000;
    bool publicReports = false;
};

struct KnowledgeSelection
{
    pbc_json records = pbc_json::array();
    std::vector<std::string> omitted;
    std::size_t bytes = 2;
};

// Immutable after startup. Source citations stay in the catalogue; only selected
// short facts enter dialogue. The same resolver powers runtime and author preview.
class KnowledgeCatalogue
{
public:
    bool Load(std::string const& document, std::string const& era, std::string& error);
    KnowledgeSelection Select(pbc_json const& facts, std::set<std::string> const& groups,
                              std::size_t byteBudget = 3600) const;
    std::vector<InformationGroup> Groups(pbc_json const& facts) const;
    std::set<uint32_t> QuestIds(uint32_t entry) const;
    std::optional<uint32_t> Language(uint32_t entry) const;
    bool Empty() const { return _records.empty(); }
    std::string const& Era() const { return _era; }

private:
    std::string _era;
    std::vector<pbc_json> _records;
    std::vector<pbc_json> _groups;
    std::map<std::string, std::vector<std::size_t>> _index;
    std::map<uint32_t, std::set<uint32_t>> _quests;
    std::map<uint32_t, uint32_t> _languages;
};
}

#endif
