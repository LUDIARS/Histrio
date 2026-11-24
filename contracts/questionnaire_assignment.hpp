#pragma once
#include "service.hpp"
#include "augur/contract_runtime.hpp"

namespace hi::contracts {
inline const auto questionnaire_assignment_check = augur::post_only(
    [](const Json& result, const Json& seed, const Json& score) {
        const auto& dominant = score.at("process").at("dominant");
        return dominant.empty()
            ? result.at("source") == "seed" && result.at("script_pattern_id") == seed.at("script_pattern_id")
            : result.at("source") == "questionnaire" && result.at("script_pattern_id") == dominant.at(0)
                && result.at("confidence") == score.at("process").at("confidence").at(dominant.at(0).get<std::string>());
    });
}
