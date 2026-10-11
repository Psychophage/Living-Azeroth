// SPDX-License-Identifier: GPL-2.0-or-later
#include "pbc_knowledge.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <openssl/evp.h>
#include <stdexcept>

namespace PBC
{
namespace
{
void Require(bool valid, std::string const& message)
{
    if (!valid)
        throw std::runtime_error(message);
}

std::string ReadCatalogue(std::filesystem::path const& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    Require(bool(file), "cannot open catalogue: " + path.string());
    auto size = file.tellg();
    Require(size >= 0 && size <= 16 * 1024 * 1024, "catalogue exceeds 16 MiB");
    std::string bytes(static_cast<std::size_t>(size), '\0');
    file.seekg(0);
    Require(bool(file.read(bytes.data(), bytes.size())), "cannot read catalogue: " + path.string());
    return bytes;
}

std::string Sha256(std::string const& bytes)
{
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned length = 0;
    Require(EVP_Digest(bytes.data(), bytes.size(), digest, &length, EVP_sha256(), nullptr) == 1,
            "cannot hash knowledge pack");
    std::string result;
    for (unsigned i = 0; i < length; ++i)
    {
        result += "0123456789abcdef"[digest[i] >> 4];
        result += "0123456789abcdef"[digest[i] & 15];
    }
    return result;
}

bool Text(pbc_json const& value, std::size_t limit)
{
    if (!value.is_string())
        return false;
    auto const& text = value.get_ref<std::string const&>();
    return !text.empty() && text.size() <= limit &&
           std::none_of(text.begin(), text.end(), [](unsigned char c) { return c < 32 || c == 127 || c == '|'; });
}

bool Id(pbc_json const& value)
{
    if (!Text(value, 96))
        return false;
    auto const& text = value.get_ref<std::string const&>();
    return std::all_of(text.begin(), text.end(), [](unsigned char c)
                      { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ':' || c == '-' || c == '_'; });
}

void Keys(pbc_json const& value, std::set<std::string> const& keys)
{
    Require(value.is_object(), "expected object");
    for (auto const& [key, unused] : value.items())
        Require(keys.contains(key), "unknown field: " + key);
}

void Conditions(pbc_json const& when, bool group)
{
    Keys(when, group ? std::set<std::string>{"map_id", "zone_id", "area_id", "entries"}
                     : std::set<std::string>{"map_id", "zone_id", "area_id", "entry", "quest_id", "quest_states", "groups"});
    for (auto key : {"map_id", "zone_id", "area_id", "entry", "quest_id"})
        if (when.contains(key))
            Require(when[key].is_number_unsigned() && when[key].get<uint64_t>() <= UINT32_MAX,
                    std::string("invalid native ID: ") + key);
    if (when.contains("groups"))
    {
        Require(when["groups"].is_array() && when["groups"].size() <= 8, "invalid group conditions");
        for (auto const& id : when["groups"])
            Require(Id(id), "invalid group ID");
    }
    if (when.contains("entries"))
    {
        Require(when["entries"].is_array() && !when["entries"].empty() && when["entries"].size() <= 256,
                "group needs bounded entries");
        for (auto const& entry : when["entries"])
            Require(entry.is_number_unsigned() && entry.get<uint64_t>() > 0 && entry.get<uint64_t>() <= UINT32_MAX,
                    "invalid group creature entry");
    }
    if (when.contains("quest_id"))
    {
        Require(when.contains("entry") && when.contains("quest_states"), "quest needs speaker entry and states");
        auto const& states = when["quest_states"];
        Require(states.is_array() && !states.empty() && states.size() <= 5, "invalid quest states");
        std::set<std::string> const allowed = {"none", "incomplete", "complete", "rewarded", "failed"};
        for (auto const& state : states)
            Require(state.is_string() && allowed.contains(state.get<std::string>()), "unknown quest state");
    }
    else
        Require(!when.contains("quest_states"), "quest states need quest_id");
}

bool Matches(pbc_json const& when, pbc_json const& facts, std::set<std::string> const& groups)
{
    for (auto key : {"map_id", "zone_id", "area_id", "entry"})
        if (when.contains(key) && (!facts.contains(key) || when[key] != facts[key]))
            return false;
    if (when.contains("entries"))
    {
        if (!facts.contains("entry"))
            return false;
        auto const& entries = when["entries"];
        if (std::find(entries.begin(), entries.end(), facts["entry"]) == entries.end())
            return false;
    }
    if (when.contains("groups"))
        for (auto const& group : when["groups"])
            if (!groups.contains(group.get<std::string>()))
                return false;
    if (when.contains("quest_id"))
    {
        if (!facts.contains("interaction_quests") || !facts["interaction_quests"].is_object())
            return false;
        auto const& quests = facts["interaction_quests"];
        auto key = std::to_string(when["quest_id"].get<uint32_t>());
        if (!quests.contains(key))
            return false;
        auto const& states = when["quest_states"];
        if (std::find(states.begin(), states.end(), quests[key]) == states.end())
            return false;
    }
    return true;
}

std::string IndexKey(pbc_json const& when)
{
    for (auto key : {"entry", "area_id", "zone_id", "map_id"})
        if (when.contains(key))
            return std::string(key) + ":" + std::to_string(when[key].get<uint32_t>());
    return "world";
}
}

bool KnowledgeCatalogue::Load(std::string const& document, std::string const& era, std::string& error)
{
    // A failed replacement never leaves a partially loaded catalogue behind.
    KnowledgeCatalogue next;
    try
    {
        Require(document.size() <= 16 * 1024 * 1024, "catalogue exceeds 16 MiB");
        auto root = pbc_json::parse(document);
        Keys(root, {"version", "era", "sources", "records", "groups"});
        Require(root.at("version") == 1 && Id(root.at("era")), "unsupported catalogue version or era");
        next._era = root["era"].get<std::string>();
        Require(!era.empty() && next._era == era, "catalogue era does not match configured KnowledgeEra");
        auto const& sources = root.at("sources");
        Require(sources.is_object() && !sources.empty(), "catalogue needs source references");
        for (auto const& [id, source] : sources.items())
        {
            Require(Id(id), "invalid source ID");
            Keys(source, {"reference", "revision", "kind"});
            Require(Text(source.at("reference"), 1024) && Text(source.at("revision"), 120), "invalid source metadata");
            Require(source.at("kind") == "world_db" || source["kind"] == "research" || source["kind"] == "authored",
                    "unknown source kind");
        }
        std::set<std::string> ids;
        auto const& records = root.at("records");
        Require(records.is_array() && records.size() <= 20000, "invalid record collection");
        std::set<std::string> const layers = {"world", "zone", "location", "npc", "quest", "group"};
        for (auto const& record : records)
        {
            Keys(record, {"id", "layer", "text", "when", "sources", "authored", "priority", "language_id"});
            Require(Id(record.at("id")) && ids.insert(record["id"].get<std::string>()).second, "duplicate/invalid record ID");
            Require(record.at("layer").is_string() && layers.contains(record["layer"].get<std::string>()), "unknown layer");
            Require(Text(record.at("text"), 1200), "record text must be 1–1200 bytes, without control characters");
            Require(record.at("authored").is_boolean(), "record must distinguish authored detail from sourced fact");
            Require(record.at("priority").is_number_unsigned() && record["priority"].get<unsigned>() <= 100,
                    "priority must be 0–100");
            Conditions(record.at("when"), false);
            auto const& when = record["when"];
            Require(record["layer"] != "npc" || when.contains("entry"), "NPC facts need speaker entry");
            Require(record["layer"] != "quest" || when.contains("quest_id"), "quest facts need quest condition");
            Require(record["layer"] != "group" || when.contains("groups"), "group facts need membership condition");
            Require(record.at("sources").is_array() && !record["sources"].empty() && record["sources"].size() <= 8,
                    "record needs bounded source references");
            for (auto const& source : record["sources"])
                Require(source.is_string() && sources.contains(source.get<std::string>()), "unknown source reference");
            if (record.contains("language_id"))
            {
                Require(record["layer"] == "npc" && when.contains("entry") && when.size() == 1 &&
                    record["language_id"].is_number_unsigned() && record["language_id"].get<uint64_t>() < 100,
                    "language needs an unconditional NPC entry record");
                Require(next._languages.emplace(when["entry"].get<uint32_t>(), record["language_id"].get<uint32_t>()).second,
                    "duplicate NPC language definition");
            }
            next._index[IndexKey(when)].push_back(next._records.size());
            next._records.push_back(record);
            if (when.contains("quest_id"))
                next._quests[when["entry"].get<uint32_t>()].insert(when["quest_id"].get<uint32_t>());
        }
        Require(root.at("groups").is_array() && root["groups"].size() <= 2000, "invalid group collection");
        std::set<std::string> groupIds;
        for (auto const& group : root["groups"])
        {
            Keys(group, {"id", "name", "description", "when", "report_lifetime_hours", "public_reports"});
            Require(Id(group.at("id")) && groupIds.insert(group["id"].get<std::string>()).second,
                    "duplicate/invalid group ID");
            Require(!group["id"].get<std::string>().starts_with("guild:"), "native guild IDs are reserved");
            Require(Text(group.at("name"), 120) && Text(group.at("description"), 600), "invalid group identity");
            Conditions(group.at("when"), true);
            Require(group["when"].contains("entries"), "NPC group membership needs explicit creature entries");
            Require(group.at("report_lifetime_hours").is_number_unsigned() &&
                        group["report_lifetime_hours"].get<uint64_t>() >= 1 &&
                        group["report_lifetime_hours"].get<uint64_t>() <= 720, "report lifetime must be 1–720 hours");
            Require(group.at("public_reports").is_boolean(), "group requires explicit public report policy");
            next._groups.push_back(group);
        }
        for (auto const& record : next._records)
            if (record["when"].contains("groups"))
                for (auto const& id : record["when"]["groups"])
                    Require(groupIds.contains(id.get<std::string>()), "record references undefined group");
        next._packCount = 1;
        *this = std::move(next);
        error.clear();
        return true;
    }
    catch (std::exception const& exception)
    {
        error = exception.what();
        return false;
    }
}

bool KnowledgeCatalogue::LoadFile(std::string const& path, std::string const& era, std::string& error)
{
    try
    {
        auto document = ReadCatalogue(path);
        auto root = pbc_json::parse(document);
        if (!root.contains("format"))
            return Load(document, era, error);
        Keys(root, {"format", "version", "era", "packs", "native_entries", "runtime_records", "dispositions"});
        Require(root.at("format") == "living-azeroth-knowledge-packs" && root.at("version") == 1,
                "unsupported knowledge pack index");
        Require(!era.empty() && root.at("era") == era, "pack index era does not match KnowledgeEra");
        auto const& packs = root.at("packs");
        Require(packs.is_array() && !packs.empty() && packs.size() <= 1024, "invalid pack collection");
        KnowledgeCatalogue next;
        next._era = era;
        auto base = std::filesystem::canonical(std::filesystem::absolute(path).parent_path());
        std::set<std::filesystem::path> paths;
        std::set<std::string> recordIds, groupIds;
        pbc_json sources = pbc_json::object();
        std::size_t totalBytes = document.size();
        for (auto const& pack : packs)
        {
            Keys(pack, {"path", "sha256", "records", "bytes"});
            Require(Text(pack.at("path"), 1024), "invalid pack path");
            std::filesystem::path relative(pack["path"].get<std::string>());
            Require(!relative.is_absolute(), "pack path must be relative");
            for (auto const& part : relative)
                Require(part != ".." && part != ".", "pack path must stay inside its index directory");
            auto file = std::filesystem::canonical(base / relative);
            auto within = file.lexically_relative(base);
            Require(!within.empty() && *within.begin() != "..", "pack symlink escapes index directory");
            Require(paths.insert(file).second, "duplicate knowledge pack");
            auto bytes = ReadCatalogue(file);
            totalBytes += bytes.size();
            Require(totalBytes <= 128 * 1024 * 1024, "knowledge packs exceed 128 MiB");
            Require(pack.at("bytes").is_number_unsigned() && pack["bytes"] == bytes.size(), "pack size mismatch");
            Require(pack.at("sha256") == Sha256(bytes), "pack hash mismatch: " + relative.string());
            KnowledgeCatalogue part;
            std::string problem;
            if (!part.Load(bytes, era, problem))
                throw std::runtime_error("invalid pack " + relative.string() + ": " + problem);
            Require(pack.at("records").is_number_unsigned() && pack["records"] == part.RecordCount(),
                    "pack record count mismatch");
            auto metadata = pbc_json::parse(bytes).at("sources");
            for (auto const& [id, source] : metadata.items())
            {
                Require(!sources.contains(id) || sources[id] == source, "conflicting source ID: " + id);
                sources[id] = source;
            }
            for (auto const& record : part._records)
            {
                Require(recordIds.insert(record["id"].get<std::string>()).second, "duplicate record across packs");
                next._index[IndexKey(record["when"])].push_back(next._records.size());
                next._records.push_back(record);
            }
            for (auto const& group : part._groups)
            {
                Require(groupIds.insert(group["id"].get<std::string>()).second, "duplicate group across packs");
                next._groups.push_back(group);
            }
            for (auto const& [entry, quests] : part._quests)
                next._quests[entry].insert(quests.begin(), quests.end());
            for (auto const& [entry, language] : part._languages)
                Require(next._languages.emplace(entry, language).second, "duplicate NPC language across packs");
            Require(next._records.size() <= 100000 && next._groups.size() <= 2000, "combined catalogue limit exceeded");
            ++next._packCount;
        }
        Require(root.at("runtime_records").is_number_unsigned() && root["runtime_records"] == next.RecordCount(),
                "index record count mismatch");
        *this = std::move(next);
        error.clear();
        return true;
    }
    catch (std::exception const& exception)
    {
        error = exception.what();
        return false;
    }
}

KnowledgeSelection KnowledgeCatalogue::Select(pbc_json const& facts, std::set<std::string> const& groups,
                                               std::size_t byteBudget) const
{
    KnowledgeSelection result;
    if (!facts.is_object())
        return result;
    std::vector<std::size_t> candidates;
    auto add = [&](std::string const& key)
    {
        if (auto found = _index.find(key); found != _index.end())
            candidates.insert(candidates.end(), found->second.begin(), found->second.end());
    };
    add("world");
    for (auto key : {"entry", "area_id", "zone_id", "map_id"})
        if (facts.contains(key) && facts[key].is_number_unsigned())
            add(std::string(key) + ":" + std::to_string(facts[key].get<uint32_t>()));
    std::stable_sort(candidates.begin(), candidates.end(), [&](auto left, auto right)
                    { return _records[left]["priority"] > _records[right]["priority"]; });
    for (auto index : candidates)
    {
        auto const& record = _records[index];
        if (!Matches(record["when"], facts, groups))
            continue;
        pbc_json brief = {{"id", record["id"]}, {"layer", record["layer"]}, {"text", record["text"]},
                          {"authored", record["authored"]}};
        auto bytes = brief.dump().size() + (result.records.empty() ? 0 : 1);
        if (result.bytes + bytes > byteBudget || result.records.size() >= 12)
            result.omitted.push_back(record["id"].get<std::string>());
        else
        {
            result.bytes += bytes;
            result.records.push_back(std::move(brief));
        }
    }
    return result;
}

std::vector<InformationGroup> KnowledgeCatalogue::Groups(pbc_json const& facts) const
{
    std::vector<InformationGroup> result;
    for (auto const& group : _groups)
        if (Matches(group["when"], facts, {}))
            result.push_back({group["id"], group["name"], group["description"],
                              group["report_lifetime_hours"].get<uint64_t>() * 3600000,
                              group["public_reports"].get<bool>()});
    return result;
}

std::optional<uint32_t> KnowledgeCatalogue::Language(uint32_t entry) const
{
    auto found = _languages.find(entry);
    return found == _languages.end() ? std::nullopt : std::optional<uint32_t>{found->second};
}

std::set<uint32_t> KnowledgeCatalogue::QuestIds(uint32_t entry) const
{
    auto found = _quests.find(entry);
    return found == _quests.end() ? std::set<uint32_t>{} : found->second;
}
}
