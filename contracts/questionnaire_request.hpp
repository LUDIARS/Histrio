#pragma once
#include "service.hpp"
#include "augur/contract_runtime.hpp"

namespace hi::contracts {
inline const auto questionnaire_request_check = augur::post_only(
    [](bool accepted, const Json& input) {
        if (!input.is_object() || input.size() > 1) return !accepted;
        return accepted == (input.empty() || (input.contains("rerun") && input.at("rerun").is_boolean()));
    });
}
