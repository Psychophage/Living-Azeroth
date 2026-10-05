// PBC Character System changes, 2026-09-30; see NOTICE.md for upstream attribution.
#include "pbc_recorded.h"

#include <chrono>
#include <map>
#include <stdexcept>
#include <thread>

#include "pbc_json.h"

namespace PBC
{
namespace
{
void Substitute(pbc_json& value, std::map<std::string, std::string> const& variables)
{
    if (value.is_string())
    {
        auto text = value.get<std::string>();
        for (auto const& [name, replacement] : variables)
            for (std::size_t offset = 0; (offset = text.find(name, offset)) != std::string::npos;)
            {
                text.replace(offset, name.size(), replacement);
                offset += replacement.size();
            }
        value = std::move(text);
    }
    else if (value.is_array() || value.is_object())
        for (auto& child : value)
            Substitute(child, variables);
}
}  // namespace

HttpTransport RecordedTransport(std::string const& fixtureJson)
{
    auto fixture = pbc_json::parse(fixtureJson);
    if (!fixture.is_array())
        throw std::invalid_argument("Recorded responses must be an array");
    return [fixture](auto const& url, auto const& body, auto const&, uint32_t) -> PBC_HttpResponse
    {
        auto request = pbc_json::parse(body);
        bool selector = url.find("decisions") != std::string::npos;
        pbc_json context = selector ? request["state"]
                                    : pbc_json::parse(request["messages"][1]["content"].template get<std::string>());
        std::string task = selector ? "selector" : context.value("task", "dialogue");
        std::map<std::string, std::string> variables;
        variables["${pending_offer}"] = "NONE";
        if (!selector && context.contains("game_facts"))
            variables["${action_clarification}"] =
                context["game_facts"].value("action_clarification", "").substr(0, 200);
        if (context.contains("self") && context["self"].is_object())
        {
            variables["${self_id}"] = context["self"].value("id", "");
            variables["${self_name}"] = context["self"].value("name", "");
        }
        if (context.contains("report_groups") && !context["report_groups"].empty())
            variables["${first_report_group}"] = context["report_groups"][0].template get<std::string>();
        if (context.contains("observations") && !context["observations"].empty())
        {
            auto const& latest = context["observations"].back();
            variables["${latest_source}"] = latest.value("source", "");
            variables["${latest_author}"] = latest.value("author", "");
        }
        if (selector && context.contains("participants") && !context["participants"].empty())
        {
            variables["${first_candidate}"] = context["participants"][0].value("id", "");
            variables["${last_candidate}"] = context["participants"].back().value("id", "");
            variables["${named_candidate}"] = variables["${first_candidate}"];
            for (auto const& candidate : context["participants"])
                if (!candidate.value("id", "").starts_with("player:"))
                    variables["${combat_enemy}"] = candidate.value("id", "");
            for (auto const& candidate : context["participants"])
                if (candidate.value("id", "").starts_with("player:"))
                    variables["${player_candidate}"] = candidate.value("id", "");
            for (auto const& candidate : context["participants"])
                if (candidate.value("id", "").starts_with("npc:"))
                    variables["${named_candidate}"] = candidate.value("id", "");
        }
        if (selector && context.contains("actions"))
        {
            for (auto const& offer : context["actions"].value("pending_supply_offers", pbc_json::array()))
                variables["${pending_offer}"] = offer.value("option", "NONE");
            // Native integration fixtures bind a tactical role from production
            // facts rather than relying on randomized speaker-list ordering.
            for (auto const& actor : context["actions"].value("controlled_companions", pbc_json::array()))
                if (actor.value("current_combat_role", "") == "tank")
                    variables["${native_tank}"] = actor.value("id", "");
            for (auto const& target : context["actions"].value("targets", pbc_json::array()))
                if (target.value("selected_at_input", false))
                    variables["${selected_target}"] = target.value("id", "");
        }
        for (auto const& rule : fixture)
        {
            if (rule.at("task") != task || context.dump().find(rule.value("contains", "")) == std::string::npos)
                continue;
            if (rule.contains("question") &&
                (!selector || !request["questions"].contains(rule["question"].template get<std::string>())))
                continue;
            if (rule.contains("transcript_contains") &&
                (!selector || context["transcript"].dump().find(rule["transcript_contains"].get<std::string>()) ==
                                  std::string::npos))
                continue;
            if (rule.contains("actor_kind") && (!context.contains("self") || context["self"].value("kind", "") !=
                                                                                 rule["actor_kind"].get<std::string>()))
                continue;
            auto contribution = context.value(selector ? "contribution" : "current_contribution", "");
            if (rule.contains("contribution_empty") && rule["contribution_empty"].get<bool>() != contribution.empty())
                continue;
            if (contribution.find(rule.value("contribution_contains", "")) == std::string::npos)
                continue;
            if (rule.contains("first_turn") &&
                rule["first_turn"].get<bool>() != context.value("previous_speaker", "").empty())
                continue;
            if (rule.contains("candidate_count") &&
                (!selector || context["participants"].size() != rule["candidate_count"].get<std::size_t>()))
                return {
                    400,
                    R"({"error":{"message":"Recorded fixture requires the complete candidate roster"},"usage":{"cost":0}})"};
            // Test-only transport latency exercises cancellation while real inference work is pending.
            auto delayMs = rule.value("delay_ms", 0u);
            if (delayMs > 2000)
                throw std::invalid_argument("Recorded response delay exceeds two seconds");
            if (delayMs)
                std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
            if (rule.contains("raw_response"))
                return {rule.value("http_status", 200), rule["raw_response"].get<std::string>()};
            auto content = rule.at("content");
            Substitute(content, variables);
            pbc_json response = {{"id", "recorded-no-network"},
                                 {"provider", "recorded-test"},
                                 {"usage", {{"cost", 0}, {"prompt_tokens", 0}, {"completion_tokens", 0}}}};
            if (selector)
            {
                response["answers"] = pbc_json::object();
                // Recorded fixtures exercise the real multi-question parser. Older speech-only
                // fixtures explicitly choose NONE for newly introduced gameplay questions.
                for (auto const& [name, question] : request["questions"].items())
                {
                    auto fallback = name == "speaker" ? "STOP" : name == "pending_answer" ? "NO" : "NONE";
                    if (content.is_object() && content.contains(name) && content[name].is_object())
                    {
                        response["answers"][name] = content[name];
                        continue;
                    }
                    if (rule.contains("choose_criterion") && rule["choose_criterion"].contains(name))
                    {
                        auto needle = rule["choose_criterion"][name].template get<std::string>();
                        std::string chosen = "NONE";
                        for (auto const& [id, description] : question["criteria"].items())
                            if (description.template get<std::string>().find(needle) != std::string::npos)
                            {
                                chosen = id;
                                break;
                            }
                        response["answers"][name] = {{"type", "choice"}, {"choice", chosen}};
                        continue;
                    }
                    auto choice = content.is_object()        ? content.value(name, fallback)
                                  : name == "speaker"        ? content.get<std::string>()
                                  : name == "pending_answer" ? "NO"
                                                             : "NONE";
                    // Older fixtures use lexical count aliases. Production now
                    // offers one canonical choice for each amount, avoiding
                    // competing synonyms that distort the distribution.
                    if (name.starts_with("quantity") && !question["criteria"].contains(choice))
                    {
                        static std::map<std::string, unsigned> const counts = {
                            {"SINGLE", 1}, {"ONE", 1}, {"one", 1},   {"two", 2},   {"three", 3}, {"four", 4},
                            {"five", 5},   {"six", 6}, {"seven", 7}, {"eight", 8}, {"nine", 9},  {"ten", 10}};
                        auto found = counts.find(choice);
                        if (found != counts.end())
                            choice = "NUM:" + std::to_string(found->second);
                        else if (!choice.empty() && choice.find_first_not_of("0123456789") == std::string::npos)
                            choice = "NUM:" + choice;
                    }
                    response["answers"][name] = {{"type", "choice"}, {"choice", choice}};
                }
            }
            else
                response["choices"] = {{{"finish_reason", "stop"}, {"message", {{"content", content.dump()}}}}};
            return {200, response.dump()};
        }
        return {400, R"({"error":{"message":"No recorded test response matched"},"usage":{"cost":0}})"};
    };
}
}  // namespace PBC
