#include "questionnaire.hpp"

#include <set>

namespace hi {
namespace {
std::string fill(std::string text, std::initializer_list<std::pair<std::string_view, std::string>> values) {
    for (const auto& [key, value] : values) {
        const auto token = "{" + std::string(key) + "}";
        for (auto pos = text.find(token); pos != std::string::npos; pos = text.find(token, pos + value.size()))
            text.replace(pos, token.size(), value);
    }
    return text;
}

std::string basis(std::string_view path, const Json& value) {
    return std::string(path) + "=" + (value.is_string() ? value.get<std::string>() : lapilli::json::stringify(value));
}

bool includes(const Json& array, const std::string& key) {
    return std::ranges::find(array, Json(key)) != array.end();
}
} // namespace

Json plan_life_script_followups(const LifeScriptData& data, const Json& score) {
    const auto& q = data.questionnaire;
    const auto& bank = data.bank;
    if (score.at("questionnaire") != q.at("id") || score.at("revision") != q.at("revision"))
        throw InputError("unexpected life-script score revision");
    const auto& limits = bank.at("limits");
    const auto limit = limits.at("maxTopics").get<std::size_t>();
    const auto& templates = bank.at("templates");
    const auto label = [&](const auto& key) { return field(q.at("labels"), key, key); };
    auto topics = Json::array();
    const auto add = [&](std::string_view kind, Json keys, Json ask, Json reasons) {
        topics.push_back({{"n", topics.size() + 1}, {"kind", kind}, {"keys", keys}, {"ask", ask}, {"basis", reasons}});
    };
    const auto& style = score.at("responseStyle");
    if (style.at("flat").get<bool>()) {
        add("response_style", Json::array(), templates.at("flat"), {basis("responseStyle.rawSd", style.at("rawSd"))});
    } else if (style.at("oneSided").get<bool>()) {
        const auto direction = field(bank.at("directions"), style.at("rawMean").get<double>() >= 2.5 ? "high" : "low");
        add("response_style", Json::array(), fill(field(templates, "oneSided"), {{"direction", direction}}),
            {basis("responseStyle.rawMean", style.at("rawMean"))});
    }
    const auto& conflicts = style.at("reverseConflicts");
    std::set<std::string> seen;
    for (const auto& conflict : conflicts) {
        const auto key = required_string(conflict, "key");
        if (!seen.insert(key).second) continue;
        auto reasons = Json::array();
        for (const auto& entry : conflicts) {
            if (entry.at("key") == key) reasons.push_back("responseStyle.reverseConflicts." + required_string(entry, "item"));
        }
        add("reverse_conflict", {key}, fill(field(templates, "reverseConflict"), {{"label", label(key)}}), reasons);
    }
    const auto& drivers = score.at("drivers");
    const auto& process = score.at("process");
    const auto& driver_winners = drivers.at("dominant");
    const auto& process_winners = process.at("dominant");
    const auto& scoring = q.at("scoring");
    const auto agree = scoring.at("forcedChoice").at("agreeMinPicks").get<int>();
    for (const auto& winner : driver_winners) {
        const auto key = winner.get<std::string>();
        const auto& picks = drivers.at("forcedChoice").at(key);
        if (picks.get<int>() < agree)
            add("forced_choice_mismatch", {key}, fill(field(templates, "forcedChoiceMismatch"), {{"label", label(key)}}),
                {basis("drivers.scores." + key, drivers.at("scores").at(key)), basis("drivers.forcedChoice." + key, picks)});
    }
    for (const auto& winner : driver_winners) {
        const auto key = winner.get<std::string>();
        const auto& entry = bank.at("drivers").at(key);
        add("dominant_driver", {key}, entry.at("ask"), {basis("drivers.scores." + key, drivers.at("scores").at(key)),
            basis("drivers.confidence." + key, drivers.at("confidence").at(key))});
        topics.back()["strength"] = entry.at("strength");
    }
    for (const auto& winner : process_winners) {
        const auto key = winner.get<std::string>();
        add("dominant_process", {key}, bank.at("process").at(key).at("ask"),
            {basis("process.scores." + key, process.at("scores").at(key)),
             basis("process.confidence." + key, process.at("confidence").at(key))});
    }
    for (const auto& [driver, mapped] : scoring.at("driverProcessCorrespondence").items()) {
        const auto key = mapped.get<std::string>();
        if (!includes(driver_winners, driver) || includes(process_winners, key)) continue;
        add("correspondence_mismatch", {driver, key}, fill(field(templates, "correspondenceMismatch"),
            {{"driver", label(driver)}, {"process", label(key)}, {"story", field(bank.at("process").at(key), "story")}}),
            {basis("drivers.dominant", driver), basis("process.scores." + key, process.at("scores").at(key))});
    }
    const auto& position = score.at("position");
    const auto& self = position.at("selfBand");
    const auto& other = position.at("otherBand");
    if (self.is_string() && other.is_string()) {
        const auto quadrant = self == "border" || other == "border" ? "border"
            : self.get<std::string>() + "/" + other.get<std::string>();
        if (bank.at("position").contains(quadrant))
            add("position", {"self", "other"}, bank.at("position").at(quadrant),
                {basis("position.self", position.at("self")), basis("position.other", position.at("other"))});
    }
    while (topics.size() > limit) topics.erase(topics.end() - 1);
    if (topics.empty()) add("open", Json::array(), templates.at("open"), {"no-dominant"});
    return {{"questionnaire", q.at("id")}, {"revision", q.at("revision")},
            {"maxDeepeningPerTopic", limits.at("maxDeepeningPerTopic")}, {"topics", topics}};
}
} // namespace hi
