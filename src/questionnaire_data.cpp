#include "questionnaire.hpp"

#include <set>

namespace hi {
LifeScriptData load_life_script(const std::filesystem::path& directory) {
    auto questionnaire = read_file(directory / "life-script-questionnaire.ja.json");
    auto bank = read_file(directory / "life-script-followups.ja.json");
    // The two upstream documents have independently versioned revisions.
    if (field(questionnaire, "id") != "life-script-ja" || field(questionnaire, "revision") != "2026-11-13"
        || field(bank, "questionnaire") != field(questionnaire, "id")
        || field(bank, "id") != "life-script-followups-ja" || field(bank, "revision") != "2026-11-24")
        throw InputError("unexpected life-script questionnaire or followup revision");

    const auto& sections = questionnaire.at("sections");
    constexpr std::array ids{"drivers", "process", "position", "forced_choice"};
    if (!sections.is_array() || sections.size() != ids.size()) throw InputError("invalid life-script sections");
    std::set<std::string> seen;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        const auto& section = sections.at(i);
        const bool forced = i == ids.size() - 1;
        if (field(section, "id") != ids[i]
            || field(section, "type") != (forced ? "forced_choice" : "likert")
            || !section.at("items").is_array()) throw InputError("invalid life-script section");
        for (const auto& item : section.at("items")) {
            if (!seen.insert(required_string(item, "id")).second) throw InputError("duplicate life-script item");
        }
    }
    if (seen.size() != 51) throw InputError("unexpected life-script item count");
    if (integer(bank.at("limits"), "maxTopics", 0, 1, 5) == 0)
        throw InputError("missing life-script followup limit");
    return {std::move(questionnaire), std::move(bank)};
}
} // namespace hi
