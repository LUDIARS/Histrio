#pragma once
#include "service.hpp"
#include "augur/contract_runtime.hpp"

namespace hi::contracts {
inline const auto questionnaire_followups_check = augur::post_only(
    [](const Json& result, const Json& topics, const Json&, const Invoke&) {
        if (!result.is_array() || result.size() != topics.size()) return false;
        for (std::size_t i = 0; i < result.size(); ++i) {
            if (result.at(i).size() != 2 || result.at(i).at("ask") != topics.at(i).at("ask")
                || !result.at(i).at("answer").is_string()) return false;
        }
        return true;
    });
}
