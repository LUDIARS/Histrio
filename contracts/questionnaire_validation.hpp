#pragma once
#include "service.hpp"
#include "augur/contract_runtime.hpp"

namespace hi::contracts {
inline const auto questionnaire_validation_check = augur::post_only(
    [](bool accepted, const Json& section, const Json& answers) {
        if (!answers.is_object()) return !accepted;
        std::size_t recognized = 0;
        bool values_valid = true;
        for (const auto& item : section.at("items")) {
            const auto id = item.at("id").get<std::string>();
            if (!answers.contains(id)) continue;
            ++recognized;
            const auto& value = answers.at(id);
            if (section.at("type") == "likert") {
                values_valid = values_valid && value.is_number_integer() && value >= 1 && value <= 4;
            } else {
                values_valid = values_valid && std::ranges::any_of(item.at("options"),
                    [&](const auto& option) { return option.at("id") == value; });
            }
        }
        return accepted == (values_valid && recognized == answers.size());
    });
}
