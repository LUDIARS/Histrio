#include "service.hpp"
#include "augur/contract_runtime.hpp" /* augur-inject:import:25ce1616 */
#include "../contracts/learning.hpp" /* augur-inject:contract-predicate:f88523e7 */

namespace hi {
double learning_step(double baseline, double current, double reward) { return ::augur::invoke_observed(::augur::ContractSpec{"C-11", "8a600e3f", "src/learning.cpp:4"}, hi::contracts::learning_check, [&]() -> double { /* augur-inject:contract-wrap:8a600e3f */
    return std::clamp(std::clamp(current + reward, baseline - .1, baseline + .1), -1., 1.);
}, baseline, current, reward); /* augur-inject:contract-end:8a600e3f */ }

double feedback_reward(std::string_view kind) {
    if (kind == "like" || kind == "reply") return .04;
    if (kind == "unanswered") return -.01;
    if (kind == "boyaki" || kind == "session-end") return .01;
    throw InputError("unknown feedback kind");
}
} // namespace hi
