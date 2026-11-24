#include "service.hpp"
#include "augur/contract_runtime.hpp" /* augur-inject:import:ae99532b */
#include "../contracts/decision.hpp" /* augur-inject:contract-predicate:496f72b4 */

namespace hi {
Json decision_record(const Json& persona, const Json& profile, const Json& pattern, const Json& input,
                     const Json& settings, int stress_turns) { return ::augur::invoke_observed(::augur::ContractSpec{"C-10", "4ef24936", "src/decision.cpp:4"}, hi::contracts::decision_check, [&]() -> Json { /* augur-inject:contract-wrap:4ef24936 */
    const auto messages = input.value("messages", Json::array());
    const auto event = field(input, "event");
    const auto signals = classify(messages, event);
    const auto features = merge_features(settings.value("previous_features", Json::object()), signals.features);
    const bool stressed = features.at("pressure").get<int>() > 0 || features.at("risk").get<int>() > 0;
    const auto stance = compute_stance(pattern, profile, features, stressed);
    const auto state = corral(profile, pattern, signals, stance, stress_turns);
    auto situation = field(input, "situation", signals.situation);
    if (!settings.at("matrix").at("situation").contains(situation)) throw InputError("unknown situation");
    const auto role = choose_role(required_string(persona, "id"), situation, state.operation, stance,
                                  settings.at("matrix"), settings.value("weights", Json::object()));
    return {{"script_pattern_id", pattern.at("id")}, {"script_pattern_version", pattern.at("version")},
            {"features", features}, {"stance", stance}, {"self_quadrant", state.self},
            {"other_quadrant", signals.other}, {"other_confidence", signals.other_confidence},
            {"operation", state.operation}, {"role", role}, {"situation", situation},
            {"situation_confidence", input.contains("situation") ? 1. : signals.confidence},
            {"stress_turns", state.stress_turns}, {"stressed", stressed},
            {"reason", {{"classifier", "buffer-statistics"}, {"selection", "situation+operation+stance+feedback"},
                        {"majority", signals.majority}, {"stress_trigger", signals.stress}, {"prior_stress_turns", stress_turns},
                        {"weights", settings.value("weights", Json::object())}}},
            {"speak", role != "observe"}};
}, persona, profile, pattern, input, settings, stress_turns); /* augur-inject:contract-end:4ef24936 */ }
} // namespace hi
