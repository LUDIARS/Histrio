#pragma once
#include "service.hpp"
#include "augur/contract_runtime.hpp"

namespace hi::contracts {
inline const auto corral_check = augur::post_only(
    [](const Corral& result, const Json&, const Json&, const Classification&, const Vector&, int) { return result.operation == operation_for(result.self); });
}
