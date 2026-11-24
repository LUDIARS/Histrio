#include "questionnaire.hpp"

#include <map>
#include "augur/contract_runtime.hpp" /* augur-inject:import:68abbd47 */
#include "../contracts/questionnaire_assignment.hpp" /* augur-inject:contract-predicate:8282df48 */

namespace hi {
bool questionnaire_eligible(const Json& persona) {
    return persona.is_object() && !persona.contains("speakerKey")
        && !field(persona, "id").starts_with("ext:")
        && field(persona, "description").find_first_not_of(" \t\r\n") != std::string::npos;
}

Json questionnaire_assignment(const Json& seed, const Json& score) { return ::augur::invoke_observed(::augur::ContractSpec{"C-12", "ba5473f6", "src/questionnaire_profile.cpp:12"}, hi::contracts::questionnaire_assignment_check, [&]() -> Json { /* augur-inject:contract-wrap:ba5473f6 */
    auto profile = seed;
    const auto& scripts = score.at("process").at("dominant");
    const auto& drivers = score.at("drivers").at("dominant");
    const auto& position = score.at("position");
    profile["source"] = "seed";
    profile["confidence"] = "low";
    if (!scripts.empty()) {
        const auto id = scripts.at(0).get<std::string>();
        profile["source"] = "questionnaire";
        profile["script_pattern_id"] = id;
        profile["confidence"] = score.at("process").at("confidence").at(id);
    }
    const std::map<std::string, std::string_view> names{
        {"be_perfect", "perfect"}, {"be_strong", "strong"}, {"try_hard", "try"},
        {"please_others", "please"}, {"hurry_up", "hurry"}};
    profile["primary_driver"] = drivers.empty() ? "" : names.at(drivers.at(0).get<std::string>());
    profile["secondary_driver"] = drivers.size() < 2 ? "" : names.at(drivers.at(1).get<std::string>());
    const auto& self = position.at("selfBand");
    const auto& other = position.at("otherBand");
    profile["position"] = {{"self", self}, {"other", other}};
    if (self == "border" || other == "border") profile["basic_quadrant"] = "border";
    else if (!self.is_null() && !other.is_null())
        profile["basic_quadrant"] = std::string(self == "positive" ? "I+" : "I-") + (other == "positive" ? "U+" : "U-");
    return profile;
}, seed, score); /* augur-inject:contract-end:ba5473f6 */ }
} // namespace hi
