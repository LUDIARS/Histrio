#pragma once
#include "service.hpp"
#include "augur/contract_runtime.hpp"

namespace hi::contracts {
inline const auto questionnaire_scoring_check = augur::post_only(
    [](const Json& result, const Json& questionnaire, const Json& answers) {
        return result.at("questionnaire") == questionnaire.at("id")
            && result.at("revision") == questionnaire.at("revision")
            && result.at("answered") == answers.at("answers").size() + answers.at("forcedChoice").size()
            && result.at("drivers").at("scores").size() == questionnaire.at("scoring").at("axes").at("drivers").size()
            && result.at("process").at("scores").size() == questionnaire.at("scoring").at("axes").at("process").size();
    });
}
