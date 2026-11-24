#pragma once
#include "service.hpp"
#include "augur/contract_runtime.hpp"

namespace hi::contracts {
inline const auto questionnaire_replay_check = augur::post_only(
    [](bool equal, const Json&, const Json&) { return equal; });
}
