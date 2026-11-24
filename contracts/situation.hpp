#pragma once
#include "service.hpp"
#include "augur/contract_runtime.hpp"

namespace hi::contracts {
inline const auto situation_check = augur::post_only(
    [](const Classification& result, const Json&, std::string_view) { return !result.situation.empty() && result.confidence >= 0 && result.confidence <= 1; });
}
