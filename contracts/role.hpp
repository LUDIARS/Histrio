#pragma once
#include "service.hpp"
#include "augur/contract_runtime.hpp"

namespace hi::contracts {
inline const auto role_check = augur::post_only(
    [](const std::string& result, std::string_view, std::string_view, std::string_view operation, const Vector&, const Json&, const Json&) { return !result.empty() && (operation != "get-nowhere-with" || result == "observe"); });
}
