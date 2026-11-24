#pragma once
#include "service.hpp"
#include "augur/contract_runtime.hpp"

namespace hi::contracts {
inline const auto learning_check = augur::post_only(
    [](double result, double baseline, double, double) { return std::abs(result - baseline) <= .100001; });
}
