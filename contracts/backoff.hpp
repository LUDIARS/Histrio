#pragma once
#include "service.hpp"
#include "augur/contract_runtime.hpp"

namespace hi::contracts {
inline const auto backoff_check = augur::post_only(
    [](std::chrono::seconds result, unsigned) { return result.count() >= 1 && result.count() <= 60; });
}
