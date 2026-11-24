#pragma once
#include "service.hpp"
#include "augur/contract_runtime.hpp"

namespace hi::contracts {
inline const auto render_check = augur::post_only(
    [](const std::string& result, const Json&, const Json&, const Json&) { return result.contains("人間を否定・攻撃・ラベリングしない") && result.contains("建設的反論"); });
}
