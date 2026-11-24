#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include <lapilli/json.hpp>

namespace hi {

using lapilli::Json;
using Vector = std::array<double, 5>;
inline constexpr std::size_t max_body = 1024 * 1024;

struct Classification {
    Json features;
    std::string situation;
    double confidence;
    std::string other;
    double other_confidence;
    double majority;
    bool stress;
};

struct Corral {
    std::string self;
    std::string operation;
    int stress_turns;
};

struct InputError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

Json read_json(std::string_view text, lorica::input_budget& budget);
Json read_json(std::string_view text);
Json read_file(const std::filesystem::path& path);
std::string field(const Json& object, std::string_view key, std::string fallback = {});
std::string required_string(const Json& object, std::string_view key);
int integer(const Json& object, std::string_view key, int fallback, int minimum, int maximum);
void check_key(std::string_view key);
void check_namespace(std::string_view name);
std::uint64_t stable_hash(std::string_view text);
void error_log(std::string_view message);

Json derive_profile(const Json& persona);
Json review_patterns(const Json& document);
Json review_roles(const Json& matrix);
Vector compute_stance(const Json& pattern, const Json& profile, const Json& features, bool stressed);
Json merge_features(const Json& previous, const Json& next);
Classification classify(const Json& messages, std::string_view event);
Corral corral(const Json& profile, const Json& pattern, const Classification& state, const Vector& stance, int turns);
std::string operation_for(std::string_view quadrant);
std::string choose_role(std::string_view persona, std::string_view situation, std::string_view operation,
                        const Vector& stance, const Json& matrix, const Json& weights);
Json decision_record(const Json& persona, const Json& profile, const Json& pattern, const Json& input,
                     const Json& settings, int stress_turns);
double learning_step(double baseline, double current, double reward);
double feedback_reward(std::string_view kind);

Json review_rules(const Json& document);
Json decide_rule(const Json& rule, int active_sessions);
bool rule_due(const Json& rule, std::string_view event, std::int64_t now, std::optional<std::int64_t> last);
std::chrono::seconds feed_backoff(unsigned failures);

using Invoke = std::function<std::string(std::string_view)>;
std::string build_prompt(const Json& persona, const Json& request, const Json& decision);
Json render(const Json& persona, const Json& request, const Json& decision, const Invoke& invoke);
Invoke claude_client(std::optional<std::string> role = std::nullopt);
Json health();

} // namespace hi
