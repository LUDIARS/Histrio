#pragma once
#include "service.hpp"
#include "augur/contract_runtime.hpp"

namespace hi::contracts {
inline const auto health_check = augur::post_only(
    [](const Json& result) { return result == Json{{"status", "ok"}}; });
}
