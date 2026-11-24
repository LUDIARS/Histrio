#include "service.hpp"
#include "augur/contract_runtime.hpp" /* augur-inject:import:6afb2466 */
#include "../contracts/role.hpp" /* augur-inject:contract-predicate:39d7bd69 */

namespace hi {
std::string choose_role(std::string_view persona, std::string_view situation, std::string_view operation,
                        const Vector& stance, const Json& matrix, const Json& weights) { return ::augur::invoke_observed(::augur::ContractSpec{"C-6", "cffa5924", "src/roles.cpp:4"}, hi::contracts::role_check, [&]() -> std::string { /* augur-inject:contract-wrap:cffa5924 */
    if (operation == "get-nowhere-with") return "observe";
    const auto situation_weights = matrix.at("situation").value(std::string(situation), Json::object());
    const auto operation_weights = matrix.at("operation").value(std::string(operation), Json::object());
    std::string best;
    double best_score = -1000;
    std::uint64_t best_hash = 0;
    for (const auto& [role, affinities] : matrix.at("stance").items()) {
        const auto vector = affinities.get<Vector>();
        double score = situation_weights.value(role, 0.) + operation_weights.value(role, 0.) * 3
                     + weights.value(role, 0.);
        for (std::size_t i = 0; i < stance.size(); ++i) score += stance[i] * vector[i];
        const auto hash = stable_hash(std::string(persona) + ":" + std::string(situation) + ":" + role);
        if (score > best_score || (score == best_score && hash > best_hash)) {
            best = role;
            best_score = score;
            best_hash = hash;
        }
    }
    return best;
}, persona, situation, operation, stance, matrix, weights); /* augur-inject:contract-end:cffa5924 */ }
} // namespace hi
