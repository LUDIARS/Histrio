#include "questionnaire.hpp"

#include <limits>
#include <numeric>
#include <set>
#include <vector>
#include "augur/contract_runtime.hpp" /* augur-inject:import:5e0909db */
#include "../contracts/questionnaire_scoring.hpp" /* augur-inject:contract-predicate:f45d29c7 */

namespace hi {
namespace {
constexpr double epsilon = 1e-9;

struct Axis {
    Json scores = Json::object();
    Json conflicts = Json::array();
    Json mean;
};

double mean(const std::vector<double>& values) {
    return std::accumulate(values.begin(), values.end(), 0.) / static_cast<double>(values.size());
}

Json rounded(const Json& value, int digits) {
    if (value.is_null()) return nullptr;
    const auto factor = std::pow(10., digits);
    return std::round((value.get<double>() + std::numeric_limits<double>::epsilon()) * factor) / factor;
}

Axis axis_stats(const Json& keys, const Json& sections, const Json& answers, const Json& scoring) {
    Axis axis;
    std::vector<double> scores;
    for (const auto& key_value : keys) {
        const auto key = key_value.get<std::string>();
        std::size_t total = 0;
        std::vector<double> converted;
        std::vector<std::pair<std::size_t, std::string>> reversed;
        for (const auto& section : sections) {
            if (section.at("type") != "likert") continue;
            for (const auto& item : section.at("items")) {
                if (item.at("key") != key) continue;
                ++total;
                const auto id = required_string(item, "id");
                if (!answers.contains(id)) continue;
                const auto raw = answers.at(id).get<double>();
                const auto reverse = item.at("reverse").get<bool>();
                if (reverse) reversed.emplace_back(converted.size(), id);
                converted.push_back(reverse ? 5 - raw : raw);
            }
        }
        auto& score = axis.scores[key];
        const auto ratio = scoring.at("scaleScore").at("minAnsweredRatio").get<double>();
        if (converted.empty() || static_cast<double>(converted.size()) < static_cast<double>(total) * ratio - epsilon)
            continue;
        const auto sum = std::accumulate(converted.begin(), converted.end(), 0.);
        score = sum / static_cast<double>(converted.size());
        scores.push_back(score.get<double>());
        const auto gap = scoring.at("responseStyle").at("reverseConflictGap").get<double>();
        for (const auto& [index, id] : reversed) {
            if (converted.size() > 1 && std::abs(converted[index] - (sum - converted[index])
                / static_cast<double>(converted.size() - 1)) >= gap - epsilon)
                axis.conflicts.push_back({{"item", id}, {"key", key}});
        }
    }
    if (!scores.empty()) axis.mean = mean(scores);
    return axis;
}

Json dominant(const Axis& axis, const Json& thresholds, bool flat) {
    auto result = Json::array();
    if (flat || axis.mean.is_null()) return result;
    std::vector<std::pair<std::string, double>> qualifying;
    const auto minimum = thresholds.at("minScore").get<double>();
    const auto gap = thresholds.at("minGapFromPersonMean").get<double>();
    for (const auto& [key, value] : axis.scores.items()) {
        if (value.is_null()) continue;
        const auto score = value.get<double>();
        if (score >= minimum - epsilon && score - axis.mean.get<double>() >= gap - epsilon)
            qualifying.emplace_back(key, score);
    }
    std::ranges::stable_sort(qualifying, std::ranges::greater{}, &std::pair<std::string, double>::second);
    const auto limit = thresholds.at("maxReported").get<std::size_t>();
    for (std::size_t i = 0; i < std::min(qualifying.size(), limit); ++i) result.push_back(qualifying[i].first);
    return result;
}

Json band(const Json& value, const Json& thresholds) {
    if (value.is_null()) return nullptr;
    const auto score = value.get<double>();
    if (score >= thresholds.at("borderLow").get<double>() - epsilon
        && score <= thresholds.at("borderHigh").get<double>() + epsilon) return "border";
    return score > thresholds.at("midpoint").get<double>() ? "positive" : "negative";
}

Json rounded_axis(const Axis& axis, const Json& winners, const Json& confidence, int digits) {
    auto scores = axis.scores;
    for (auto& value : scores) value = rounded(value, digits);
    return {{"scores", scores}, {"personMean", rounded(axis.mean, digits)}, {"dominant", winners},
            {"confidence", confidence}};
}

Json forced_picks(const Json& keys, const Json& sections, const Json& answers) {
    auto picks = Json::object();
    for (const auto& key : keys) picks[key.get<std::string>()] = 0;
    for (const auto& section : sections) {
        if (section.at("type") != "forced_choice") continue;
        for (const auto& item : section.at("items")) {
            const auto chosen = field(answers, required_string(item, "id"));
            for (const auto& option : item.at("options")) {
                if (option.at("id") != chosen) continue;
                auto& count = picks.at(required_string(option, "key"));
                count = count.get<int>() + 1;
            }
        }
    }
    return picks;
}
} // namespace

Json score_life_script(const Json& questionnaire, const Json& answers) { return ::augur::invoke_observed(::augur::ContractSpec{"C-14", "5de49fbd", "src/questionnaire_score.cpp:118"}, hi::contracts::questionnaire_scoring_check, [&]() -> Json { /* augur-inject:contract-wrap:5de49fbd */
    if (answers.at("questionnaire") != questionnaire.at("id") || answers.at("revision") != questionnaire.at("revision"))
        throw InputError("unexpected life-script answer revision");
    const auto& likert = answers.at("answers");
    const auto& forced = answers.at("forcedChoice");
    const auto& sections = questionnaire.at("sections");
    const auto& scoring = questionnaire.at("scoring");
    const auto& axes = scoring.at("axes");
    const auto digits = integer(scoring.at("scaleScore"), "round", 2, 0, 6);
    std::vector<double> raw;
    std::size_t forced_count = 0;
    for (const auto& section : sections) {
        const bool is_likert = section.at("type") == "likert";
        auto section_answers = Json::object();
        const auto& values = is_likert ? likert : forced;
        if (!values.is_object()) throw InputError("expected life-script answer map");
        for (const auto& item : section.at("items")) {
            const auto id = required_string(item, "id");
            if (values.contains(id)) section_answers[id] = values.at(id);
        }
        if (!valid_section_answers(section, section_answers)) throw InputError("invalid life-script answers");
        if (is_likert) for (const auto& value : section_answers) raw.push_back(value.get<double>());
        else forced_count += section_answers.size();
    }
    if (raw.size() != likert.size() || forced_count != forced.size()) throw InputError("unknown life-script item");

    const auto& style = scoring.at("responseStyle");
    Json raw_mean;
    Json sd;
    bool flat = false;
    bool one_sided = false;
    if (!raw.empty()) {
        const auto average = mean(raw);
        const auto squares = std::accumulate(raw.begin(), raw.end(), 0., [average](double sum, double value) {
            const auto delta = value - average;
            return sum + delta * delta;
        });
        const auto deviation = std::sqrt(squares / static_cast<double>(raw.size()));
        raw_mean = rounded(average, digits);
        sd = rounded(deviation, digits);
        flat = deviation < style.at("flatSdBelow").get<double>() - epsilon;
        one_sided = average >= style.at("oneSidedHighMean").get<double>() - epsilon
                 || average <= style.at("oneSidedLowMean").get<double>() + epsilon;
    }

    const auto drivers = axis_stats(axes.at("drivers"), sections, likert, scoring);
    const auto process = axis_stats(axes.at("process"), sections, likert, scoring);
    const auto position = axis_stats(axes.at("position"), sections, likert, scoring);
    auto conflicts = drivers.conflicts;
    for (const auto& conflict : process.conflicts) conflicts.push_back(conflict);
    std::set<std::string> conflicted;
    for (const auto& conflict : conflicts) conflicted.insert(required_string(conflict, "key"));
    for (const auto& conflict : position.conflicts) conflicts.push_back(conflict);

    const auto winners = dominant(drivers, scoring.at("dominance"), flat);
    const auto scripts = dominant(process, scoring.at("dominance"), flat);
    const auto picks = forced_picks(axes.at("drivers"), sections, forced);
    const auto agree = scoring.at("forcedChoice").at("agreeMinPicks").get<int>();
    auto driver_confidence = Json::object();
    for (const auto& winner : winners) {
        const auto key = winner.get<std::string>();
        driver_confidence[key] = conflicted.contains(key) ? "low" : picks.at(key).get<int>() >= agree ? "high" : "medium";
    }
    auto process_confidence = Json::object();
    const auto& correspondence = scoring.at("driverProcessCorrespondence");
    for (const auto& winner : scripts) {
        const auto key = winner.get<std::string>();
        const auto matched = std::ranges::any_of(winners, [&](const auto& driver) {
            return field(correspondence, driver.template get<std::string>()) == key;
        });
        process_confidence[key] = conflicted.contains(key) ? "low" : matched ? "high" : "medium";
    }
    auto driver_result = rounded_axis(drivers, winners, driver_confidence, digits);
    driver_result["forcedChoice"] = picks;
    const auto& self = position.scores.at("self");
    const auto& other = position.scores.at("other");
    const auto& thresholds = scoring.at("position");
    return {{"questionnaire", questionnaire.at("id")}, {"revision", questionnaire.at("revision")},
            {"answered", raw.size() + forced_count}, {"drivers", driver_result},
            {"process", rounded_axis(process, scripts, process_confidence, digits)},
            {"position", {{"self", rounded(self, digits)}, {"other", rounded(other, digits)},
                          {"selfBand", band(self, thresholds)}, {"otherBand", band(other, thresholds)}}},
            {"responseStyle", {{"rawMean", raw_mean}, {"rawSd", sd}, {"flat", flat},
                               {"oneSided", one_sided}, {"reverseConflicts", conflicts}}}};
}, questionnaire, answers); /* augur-inject:contract-end:5de49fbd */ }
} // namespace hi
