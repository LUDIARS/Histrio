#include "service.hpp"
#include "augur/contract_runtime.hpp" /* augur-inject:import:54c9ba5d */
#include "../contracts/profile.hpp" /* augur-inject:contract-predicate:d4dbc3a5 */

namespace hi {
Json derive_profile(const Json& persona) { return ::augur::invoke_observed(::augur::ContractSpec{"C-2", "c5b41f62", "src/profile.cpp:4"}, hi::contracts::profile_check, [&]() -> Json { /* augur-inject:contract-wrap:c5b41f62 */
    constexpr std::array ids{"until", "after", "never", "always", "almost", "open_ended"};
    constexpr std::array axes{"CP", "NP", "A", "FC", "AC"};
    constexpr std::array drivers{"perfect", "please", "try", "strong", "hurry"};
    constexpr std::array tendencies{"advance", "maintain", "cautious"};
    const auto id = required_string(persona, "id");
    const auto seed = field(persona, "seed", id);
    const auto signals = persona.value("signals", Json::object());
    if (!signals.is_object()) throw InputError("signals must be an object");
    const auto script = stable_hash(seed);
    const auto hash = stable_hash(seed + ":" + nlohmann::json(signals).dump());
    const auto primary = hash % drivers.size();
    const auto secondary = (primary + 1 + (hash >> 7) % (drivers.size() - 1)) % drivers.size();
    Json egogram = Json::object();
    for (std::size_t i = 0; i < axes.size(); ++i) {
        egogram[axes[i]] = (hash >> (i * 9)) % 101;
    }
    return {{"script_pattern_id", field(persona, "script_pattern_id", ids[script % ids.size()])},
            {"egogram", egogram}, {"primary_driver", drivers[primary]},
            {"secondary_driver", drivers[secondary]}, {"basic_quadrant", "I+U+"},
            {"tendency", tendencies[(hash >> 51) % tendencies.size()]}, {"source", "seed"}};
}, persona); /* augur-inject:contract-end:c5b41f62 */ }
} // namespace hi
