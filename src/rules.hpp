#pragma once

#include "service.hpp"

#include <map>
#include <mutex>

namespace hi {
class RulesEngine {
public:
    Json pass(const Json& rules, std::string_view event, std::int64_t now, std::optional<int> active = std::nullopt);

private:
    struct Clock { Json rule; std::optional<std::int64_t> last; };
    std::mutex mutex_;
    std::map<std::string, Clock> clocks_;
    int active_ = 0;
};
} // namespace hi
