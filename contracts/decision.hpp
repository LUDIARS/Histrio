#pragma once
#include "service.hpp"
#include "augur/contract_runtime.hpp"

namespace hi::contracts {
inline const auto decision_check = augur::post_only(
    [](const Json& result, const Json&, const Json&, const Json&, const Json&, const Json&, int) { return result.contains("script_pattern_version") && result.contains("stance") && result.contains("operation") && result.contains("role") && result.contains("other_quadrant"); });
}
