#include "service.hpp"

#include <set>
#include "augur/contract_runtime.hpp" /* augur-inject:import:77d6a4b4 */
#include "../contracts/situation.hpp" /* augur-inject:contract-predicate:69f32165 */
#include "../contracts/corral.hpp" /* augur-inject:contract-predicate:67afe56c */

namespace hi {
namespace {
bool has(std::string_view text, std::initializer_list<std::string_view> words) {
    return std::ranges::any_of(words, [text](auto word) { return text.contains(word); });
}

struct Signals {
    Json features = merge_features(Json::object(), Json::object());
    std::set<std::string> speakers;
    int questions = 0;
    int short_lines = 0;
    int quotes = 0;
    int negative = 0;
    int approval = 0;
    int self_doubt = 0;
    int commands = 0;
    int repeated = 0;
    int celebrations = 0;
    int gaps = 0;
    int options = 0;
    int unanswered = 0;
};

void add_message(Signals& s, const Json& message) {
    const auto text = required_string(message, "text");
    for (const auto& [key, found] : std::initializer_list<std::pair<const char*, bool>>{
        {"proposal", has(text, {"提案", "案", "どうか", "propose"})},
        {"agreement", has(text, {"決定", "合意", "それで", "agreed"})},
        {"risk", has(text, {"エラー", "失敗", "懸念", "risk"})},
        {"success", has(text, {"完了", "成功", "merged", "🎉"})},
        {"precedent", has(text, {"前回", "以前", "いつもの", "precedent"})},
        {"condition", has(text, {"もし", "なら", "未定", "条件", "if "})}}) {
        if (found) s.features[key] = s.features.at(key).get<int>() + 1;
    }
    const bool question = has(text, {"?", "？"});
    s.questions += question;
    s.unanswered = question ? s.unanswered + 1 : std::max(0, s.unanswered - 1);
    s.negative += has(text, {"違う", "反対", "否定", "無理", "wrong", "disagree"});
    s.approval += has(text, {"ありがとう", "賛成", "いいね", "agree", "thanks"});
    s.self_doubt += has(text, {"すみません", "ごめん", "自分が悪い", "sorry"});
    s.commands += has(text, {"しろ", "するな", "命令", "must"});
    s.quotes += text.starts_with('>') || text.contains("\n>");
    s.options += has(text, {"選択肢", "どれ", "どちら", "option"});
    s.celebrations += has(text, {"🎉", "おめでとう", "🥳"});
    s.short_lines += text.size() <= 120;
}

std::pair<std::string, double> situation_for(const Signals& s, std::string_view event, int count) {
    const std::array scores{
        std::pair{"conflict", s.negative * 2 + s.quotes},
        std::pair{"celebration", s.features.at("success").get<int>() * 2 + s.celebrations * 2},
        std::pair{"convergence", s.options * 2 + s.features.at("agreement").get<int>()
                                  + (event.starts_with("ask.") ? 3 : 0)},
        std::pair{"stagnation", s.gaps * 2 + s.repeated + s.unanswered},
        std::pair{"divergence", s.questions + static_cast<int>(s.speakers.size() > 2) * 2 + s.short_lines / 2}
    };
    const auto best = std::ranges::max_element(scores, {}, &std::pair<const char*, int>::second);
    if (best->second < 2) return {"casual", count == 0 ? 1. : .35};
    return {best->first, std::clamp(double(best->second) / (count + 1), 0., 1.)};
}
}

Classification classify(const Json& messages, std::string_view event) { return ::augur::invoke_observed(::augur::ContractSpec{"C-4", "c8ffe4fc", "src/conversation.cpp:67"}, hi::contracts::situation_check, [&]() -> Classification { /* augur-inject:contract-wrap:c8ffe4fc */
    if (!messages.is_array() || messages.size() > 128) throw InputError("invalid conversation buffer");
    Signals s;
    std::string previous;
    int prior_time = 0;
    for (const auto& message : messages) {
        add_message(s, message);
        const auto speaker = field(message, "speaker");
        const auto ts = integer(message, "ts", 0, 0, 2147483647);
        if (!speaker.empty()) {
            s.speakers.insert(speaker);
            s.repeated += speaker == previous;
        }
        s.gaps += ts > prior_time && prior_time > 0 && ts - prior_time >= 300;
        prior_time = ts;
        previous = speaker;
    }
    if (event == "topic.started") s.features["proposal"] = s.features.at("proposal").get<int>() + 1;
    if (event == "ask.answered") s.features["agreement"] = s.features.at("agreement").get<int>() + 1;
    if (event == "rule.error" || event == "session.failed") s.features["risk"] = s.features.at("risk").get<int>() + 1;
    if (event == "pr.merged" || event == "session.completed") s.features["success"] = s.features.at("success").get<int>() + 1;
    const auto count = static_cast<int>(messages.size());
    const auto [situation, confidence] = situation_for(s, event, count);
    const auto denominator = static_cast<double>(std::max(1, count));
    const auto positive = s.approval;
    const auto other_negative = s.negative + s.commands > positive;
    const auto self_negative = s.self_doubt > positive;
    const std::string other = self_negative ? (other_negative ? "I-U-" : "I-U+")
                                            : (other_negative ? "I+U-" : "I+U+");
    const auto evidence = s.negative + s.commands + s.self_doubt + s.approval;
    s.features["pressure"] = s.negative + s.commands;
    return {s.features, situation, confidence, other, std::min(1., evidence / denominator),
            std::clamp((positive - s.negative) / denominator, -1., 1.),
            s.negative > 0 || s.commands > 0 || s.features.at("risk").get<int>() > 0};
}, messages, event); /* augur-inject:contract-end:c8ffe4fc */ }

std::string operation_for(std::string_view quadrant) {
    if (quadrant == "I+U+") return "get-on-with";
    if (quadrant == "I+U-") return "constructive-objection";
    if (quadrant == "I-U+") return "get-away-from";
    if (quadrant == "I-U-") return "get-nowhere-with";
    return {};
}

Corral corral(const Json& profile, const Json& pattern, const Classification& state, const Vector& stance, int turns) { return ::augur::invoke_observed(::augur::ContractSpec{"C-5", "e277f116", "src/conversation.cpp:110"}, hi::contracts::corral_check, [&]() -> Corral { /* augur-inject:contract-wrap:e277f116 */
    auto self = field(profile, "basic_quadrant", "I+U+");
    if (state.stress && std::abs(stance[0] - state.majority) >= .55) turns = 3;
    if (turns > 0) self = field(pattern, "stress_quadrant");
    if (state.other != "I+U+" && state.other_confidence >= .5) --turns;
    if (turns <= 0 && (state.other != "I+U+" || self != field(profile, "basic_quadrant"))) self = "I+U+";
    return {self, operation_for(self), std::max(0, turns - 1)};
}, profile, pattern, state, stance, turns); /* augur-inject:contract-end:e277f116 */ }
} // namespace hi
