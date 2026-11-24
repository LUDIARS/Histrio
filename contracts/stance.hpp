#pragma once
#include "service.hpp"
#include "augur/contract_runtime.hpp"

namespace hi::contracts {
inline const auto stance_check = augur::post_only(
    [](const Vector& result, const Json&, const Json&, const Json&, bool) { return std::ranges::all_of(result, [](double v) { return v >= -1 && v <= 1; }); });
}
