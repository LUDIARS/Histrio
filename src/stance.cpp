#include "service.hpp"
#include "augur/contract_runtime.hpp" /* augur-inject:import:9fa71ff2 */
#include "../contracts/stance.hpp" /* augur-inject:contract-predicate:cbb45bb6 */

namespace hi {
Vector compute_stance(const Json& pattern, const Json& profile, const Json& features, bool stressed) { return ::augur::invoke_observed(::augur::ContractSpec{"C-3", "f0c8e0fa", "src/stance.cpp:4"}, hi::contracts::stance_check, [&]() -> Vector { /* augur-inject:contract-wrap:f0c8e0fa */
    Vector result = pattern.at("base").get<Vector>();
    const auto tendency = field(profile, "tendency");
    if (tendency == "advance") result[1] += .05;
    if (tendency == "cautious") result[1] -= .05;
    const auto driver = field(profile, "primary_driver");
    const auto secondary = field(profile, "secondary_driver");
    const auto amplify = [&](std::string_view name) {
        if (name == "perfect" || name == "please") result[name == "perfect" ? 3 : 4] -= .08;
        if (name == "try" || name == "hurry") result[name == "try" ? 2 : 1] += .08;
        if (name == "strong") result[2] -= .08;
    };
    if (stressed) {
        amplify(driver);
        amplify(secondary);
    }
    for (const auto& [feature, vector] : pattern.at("reactions").items()) {
        const auto count = features.value(feature, 0);
        const auto scale = static_cast<double>(count) / (1.0 + count);
        const auto coefficients = vector.get<Vector>();
        for (std::size_t i = 0; i < result.size(); ++i) result[i] += coefficients[i] * scale;
    }
    std::ranges::transform(result, result.begin(), [](double value) { return std::clamp(value, -1., 1.); });
    return result;
}, pattern, profile, features, stressed); /* augur-inject:contract-end:f0c8e0fa */ }

Json merge_features(const Json& previous, const Json& next) {
    Json result = Json::object();
    for (const char* feature : {"proposal", "agreement", "risk", "success", "precedent", "condition", "pressure"}) {
        const auto a = integer(previous, feature, 0, 0, 1000000);
        const auto b = integer(next, feature, 0, 0, 1000000);
        result[feature] = std::min(1000000, a + b);
    }
    return result;
}
} // namespace hi
