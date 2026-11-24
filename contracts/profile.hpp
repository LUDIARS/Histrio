#pragma once
#include "service.hpp"
#include "augur/contract_runtime.hpp"

namespace hi::contracts {
inline const auto profile_check = augur::post_only(
    [](const Json& result, const Json&) { return result.at("script_pattern_id").is_string() && result.at("egogram").size() == 5; });
}
