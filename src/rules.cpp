#include "rules.hpp"

#include <set>
#include "augur/contract_runtime.hpp" /* augur-inject:import:0c6990ce */
#include "../contracts/rules.hpp" /* augur-inject:contract-predicate:70cf0474 */

namespace hi {
namespace {
constexpr std::array channels{"chitchat", "consultation", "報告", "ぼやき", "system", "genius"};
constexpr std::array intents{"chitchat", "review", "reply", "react", "notice", "consult"};

void review_rule(const Json& rule) {
    check_key(required_string(rule, "id"));
    const auto trigger = required_string(rule, "trigger_type");
    if (trigger != "tick" && trigger != "event") throw InputError("invalid trigger");
    if (trigger == "tick" && integer(rule, "tick_sec", 0, 1, 86400) == 0)
        throw InputError("tick requires tick_sec");
    const auto pattern = field(rule, "event_kind", "*");
    if (pattern != "*") check_key(pattern);
    (void)integer(rule, "cooldown_sec", 0, 0, 86400);
    if (rule.contains("enabled") && !rule.at("enabled").is_boolean()) throw InputError("enabled must be boolean");
    const auto target = field(rule, "target", "chitchat");
    if (std::ranges::find(channels, target) == channels.end()) throw InputError("invalid rule channel");
    const auto conditions = rule.value("conditions", Json::array());
    if (!conditions.is_array() || conditions.size() > 16) throw InputError("invalid conditions");
    std::set<std::string> seen;
    for (const auto& c : conditions) {
        const auto type = required_string(c, "type");
        if (!seen.insert(type).second) throw InputError("duplicate condition");
        if (type == "any_active_session") continue;
        if (type == "min_active_sessions") {
            (void)integer(c, "value", 1, 1, 100000);
        } else if (type == "channel" && std::ranges::find(channels, field(c, "value")) != channels.end()) {
            continue;
        } else if (type == "intent" && std::ranges::find(intents, field(c, "value")) != intents.end()) {
            continue;
        } else {
            throw InputError("invalid rule condition");
        }
    }
}
}

Json review_rules(const Json& document) {
    const auto& rules = document.at("rules");
    if (!rules.is_array() || rules.size() > 256) throw InputError("invalid rules array");
    std::set<std::string> ids;
    for (const auto& rule : rules) {
        review_rule(rule);
        if (!ids.insert(required_string(rule, "id")).second) throw InputError("duplicate rule id");
    }
    if (document.contains("role_matrix")) (void)review_roles(document.at("role_matrix"));
    return document;
}

Json decide_rule(const Json& rule, int active_sessions) { return ::augur::invoke_observed(::augur::ContractSpec{"C-7", "e609d4c4", "src/rules.cpp:54"}, hi::contracts::rules_check, [&]() -> Json { /* augur-inject:contract-wrap:e609d4c4 */
    auto channel = field(rule, "target", "chitchat");
    std::string intent;
    std::string reason = "conditions met";
    bool fire = true;
    for (const auto& c : rule.value("conditions", Json::array())) {
        const auto type = required_string(c, "type");
        if (type == "channel") channel = required_string(c, "value");
        if (type == "intent") intent = required_string(c, "value");
        if (fire && type == "any_active_session" && active_sessions == 0) {
            reason = "no active session";
            fire = false;
        }
        if (fire && type == "min_active_sessions") {
            const auto minimum = integer(c, "value", 1, 1, 100000);
            if (active_sessions < minimum) {
                reason = "active sessions < " + std::to_string(minimum);
                fire = false;
            }
        }
    }
    if (intent.empty()) intent = channel == "consultation" ? "consult" : channel == "報告" ? "notice" : "chitchat";
    return {{"fire", fire}, {"channel", channel}, {"intent", intent}, {"reason", reason}};
}, rule, active_sessions); /* augur-inject:contract-end:e609d4c4 */ }

bool rule_due(const Json& rule, std::string_view event, std::int64_t now, std::optional<std::int64_t> last) {
    if (!rule.value("enabled", true) || event == "ping") return false;
    if (last && (now < *last || now - *last < integer(rule, "cooldown_sec", 0, 0, 86400))) return false;
    const auto trigger = required_string(rule, "trigger_type");
    if (trigger == "tick") return event.empty() && (!last || now - *last >= integer(rule, "tick_sec", 1, 1, 86400));
    const auto match = field(rule, "event_kind", "*");
    return !event.empty() && (match == "*" || match == event);
}

Json RulesEngine::pass(const Json& rules, std::string_view event, std::int64_t now, std::optional<int> active) {
    std::lock_guard lock(mutex_);
    if (active) active_ = *active;
    auto results = Json::array();
    std::map<std::string, Clock> next;
    for (const auto& rule : rules.at("rules")) {
        const auto id = required_string(rule, "id");
        const auto old = clocks_.find(id);
        const auto last = old != clocks_.end() && old->second.rule == rule ? old->second.last : std::nullopt;
        auto& clock = next.emplace(id, Clock{rule, last}).first->second;
        if (!rule_due(rule, event, now, last)) continue;
        clock.last = now;
        auto result = decide_rule(rule, active_);
        result["rule_id"] = id;
        results.push_back(std::move(result));
    }
    clocks_ = std::move(next);
    return results;
}
} // namespace hi
