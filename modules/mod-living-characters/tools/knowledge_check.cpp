// SPDX-License-Identifier: GPL-2.0-or-later
// Offline author preview using the production resolver. Never contacts a service.
#include "pbc_knowledge.h"
#include <fstream>
#include <iostream>
#include <sstream>

int main(int argc, char** argv)
{
    if (argc < 3 || argc > 4)
    {
        std::cerr << "Usage: knowledge_check CATALOGUE ERA [FACTS_JSON_FILE]\n";
        return 2;
    }
    auto read = [](char const* path)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
            throw std::runtime_error("cannot open input file");
        std::ostringstream contents;
        contents << stream.rdbuf();
        return contents.str();
    };
    try
    {
        PBC::KnowledgeCatalogue catalogue;
        std::string error;
        if (!catalogue.Load(read(argv[1]), argv[2], error))
            throw std::runtime_error(error);
        pbc_json facts = argc == 4 ? pbc_json::parse(read(argv[3])) : pbc_json::object();
        std::set<std::string> groups;
        for (auto const& group : catalogue.Groups(facts))
            groups.insert(group.id);
        auto selection = catalogue.Select(facts, groups);
        std::cout << pbc_json({{"valid", true}, {"era", catalogue.Era()}, {"bytes", selection.bytes},
                              {"records", selection.records}, {"omitted", selection.omitted},
                              {"groups", groups}}).dump(2) << '\n';
        return 0;
    }
    catch (std::exception const& exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
