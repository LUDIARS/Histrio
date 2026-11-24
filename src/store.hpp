#pragma once

#include "database.hpp"

#include <mutex>

namespace hi {

struct Conflict : std::runtime_error { using std::runtime_error::runtime_error; };

class Store {
public:
    Store(const std::filesystem::path& path, const Json& patterns, const Json& roles);

    Json put_persona(std::string_view ns, const Json& persona);
    Json list_personas(std::string_view ns);
    Json persona(std::string_view ns, std::string_view id);
    Json profile(std::string_view ns, std::string_view id);
    Json render_snapshot(std::string_view ns, std::string_view id);

    Json put_patterns(std::string_view ns, const Json& document);
    Json patterns(std::string_view ns);
    Json put_rules(std::string_view ns, const Json& document);
    Json rules(std::string_view ns);

    Json decide(std::string_view ns, std::string_view id, const Json& request);
    Json audit(std::string_view ns, std::int64_t id);
    Json learning(std::string_view ns, std::string_view id);
    Json feedback(std::string_view ns, std::int64_t decision_id, const Json& input, std::int64_t utc_day);
    Json import_legacy(const std::filesystem::path& source);

private:
    Json put_persona_unlocked(std::string_view ns, const Json& persona);
    void add_patterns(std::string_view ns, const Json& document);
    Json script(std::string_view ns, std::string_view id);
    Json decision_settings(std::string_view ns, std::string_view id, const Json& pattern,
                           const Json& request, int& turns);
    void apply_feedback(const Json& decision, std::int64_t day, double reward);

    std::mutex mutex_;
    Database db_;
    Json roles_;
};

} // namespace hi
