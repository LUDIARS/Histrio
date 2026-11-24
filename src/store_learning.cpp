#include "store.hpp"

namespace hi {
Json Store::learning(std::string_view ns, std::string_view id) {
    check_namespace(ns);
    check_key(id);
    std::lock_guard lock(mutex_);
    auto profile = db_.query("SELECT payload FROM persona_profiles WHERE id=? AND namespace IN (?,'shared') "
                             "ORDER BY namespace=? DESC LIMIT 1");
    profile.bind(1, id).bind(2, ns).bind(3, ns);
    if (!profile.step()) return {};
    const auto pattern_id = required_string(read_json(profile.text(0)), "script_pattern_id");
    const auto pattern = script(ns, pattern_id);
    const auto version = required_string(pattern, "version");
    auto rows = db_.query("SELECT scope,key,value FROM persona_learning WHERE namespace=? AND persona_id=? "
                          "AND pattern_id=? AND pattern_version=? ORDER BY scope,key");
    rows.bind(1, ns).bind(2, id).bind(3, pattern_id).bind(4, required_string(pattern, "namespace") + ":" + version);
    auto weights = Json::object();
    while (rows.step()) weights[rows.text(0)][rows.text(1)] = rows.real(2);
    return {{"script_pattern_id", pattern_id}, {"script_pattern_version", version}, {"weights", weights}};
}

Json Store::decision_settings(std::string_view ns, std::string_view id, const Json& pattern,
                              const Json& request, int& turns) {
    Json previous = Json::object();
    auto weights = Json::object();
    auto reactions = Json::object();
    const auto topic = field(request, "topic");
    if (!topic.empty()) {
        auto last = db_.query("SELECT payload FROM persona_decision_log WHERE namespace=? AND persona_id=? AND topic=? "
                              "ORDER BY id DESC LIMIT 1");
        last.bind(1, ns).bind(2, id).bind(3, topic);
        if (last.step()) {
            const auto value = read_json(last.text(0));
            turns = value.at("stress_turns").get<int>();
            if (request.value("incremental", false)) previous = value.at("features");
        }
    } else if (request.value("incremental", false)) {
        throw InputError("incremental mode requires a topic");
    }
    auto matrix = roles_;
    auto rule_set = db_.query("SELECT payload FROM rule_sets WHERE namespace IN (?,'shared') ORDER BY namespace=? DESC LIMIT 1");
    rule_set.bind(1, ns).bind(2, ns);
    if (rule_set.step()) matrix = read_json(rule_set.text(0)).value("role_matrix", matrix);
    const auto messages = request.value("messages", Json::array());
    const auto situation = field(request, "situation", classify(messages, field(request, "event")).situation);
    auto learned = db_.query("SELECT scope,key,value FROM persona_learning WHERE namespace=? AND persona_id=? "
                             "AND pattern_id=? AND pattern_version=? ORDER BY scope,key");
    learned.bind(1, ns).bind(2, id).bind(3, required_string(pattern, "id"))
        .bind(4, required_string(pattern, "namespace") + ":" + required_string(pattern, "version"));
    const auto scope = "role:" + situation;
    while (learned.step()) {
        const auto key = learned.text(1);
        const double value = learned.real(2);
        if (learned.text(0) == scope) weights[key] = value;
        if (learned.text(0) == "reaction") reactions[key] = value;
    }
    return {{"matrix", matrix}, {"weights", weights}, {"reaction_learning", reactions}, {"previous_features", previous}};
}

void Store::apply_feedback(const Json& decision, std::int64_t day, double reward) {
    const auto ns = required_string(decision, "namespace");
    const auto persona = required_string(decision, "persona_id");
    const auto pattern = required_string(decision, "script_pattern_id");
    const auto version = required_string(decision, "script_pattern_namespace") + ":" +
                         required_string(decision, "script_pattern_version");
    const auto decision_id = decision.at("decision_id").get<std::int64_t>();
    const auto update = [&](const std::string& scope, const std::string& key) {
        auto previous = db_.query("SELECT day,baseline,value FROM persona_learning WHERE namespace=? AND persona_id=? "
                                  "AND pattern_id=? AND pattern_version=? AND scope=? AND key=?");
        previous.bind(1, ns).bind(2, persona).bind(3, pattern).bind(4, version).bind(5, scope).bind(6, key);
        double current = 0;
        double baseline = 0;
        if (previous.step()) {
            if (previous.number(0) > day) throw Conflict("feedback clock precedes existing learning");
            current = previous.real(2);
            baseline = previous.number(0) == day ? previous.real(1) : current;
        }
        const auto value = learning_step(baseline, current, reward);
        db_.query("INSERT INTO persona_learning VALUES(?,?,?,?,?,?,?,?,?) "
                  "ON CONFLICT(namespace,persona_id,pattern_id,pattern_version,scope,key) "
                  "DO UPDATE SET day=excluded.day,baseline=excluded.baseline,value=excluded.value")
            .bind(1, ns).bind(2, persona).bind(3, pattern).bind(4, version).bind(5, scope).bind(6, key)
            .bind(7, day).bind_real(8, baseline).bind_real(9, value).step();
        db_.query("INSERT INTO persona_learning_history(decision_id,day,scope,key,before,after) VALUES(?,?,?,?,?,?)")
            .bind(1, decision_id).bind(2, day).bind(3, scope).bind(4, key).bind_real(5, current).bind_real(6, value).step();
    };
    update("role:" + required_string(decision, "situation"), required_string(decision, "role"));
    const auto& active = decision.at("features");
    for (const auto& [feature, _] : decision.at("reason").at("pattern").at("reactions").items()) {
        if (active.value(feature, 0) > 0) update("reaction", feature);
    }
}

Json Store::feedback(std::string_view ns, std::int64_t decision_id, const Json& input, std::int64_t utc_day) {
    check_namespace(ns);
    if (utc_day < 0 || utc_day > 3650000) throw InputError("feedback day is outside the supported range");
    const auto id = required_string(input, "id");
    check_key(id);
    const auto kind = required_string(input, "kind");
    const auto reward = feedback_reward(kind);
    std::lock_guard lock(mutex_);
    Transaction tx(db_);
    auto row = db_.query("SELECT payload FROM persona_decision_log WHERE namespace=? AND id=?");
    row.bind(1, ns).bind(2, decision_id);
    if (!row.step()) return {};
    auto decision = read_json(row.text(0));
    decision["decision_id"] = decision_id;
    auto existing = db_.query("SELECT decision_id,kind FROM persona_feedback_log WHERE namespace=? AND source_id=?");
    existing.bind(1, ns).bind(2, id);
    if (existing.step()) {
        if (existing.number(0) != decision_id || existing.text(1) != kind)
            throw Conflict("feedback id already has different content");
        return {{"decision_id", decision_id}, {"applied", false}};
    }
    db_.query("INSERT INTO persona_feedback_log VALUES(?,?,?,?,?,?,?)")
        .bind(1, ns).bind(2, id).bind(3, decision_id).bind(4, required_string(decision, "persona_id"))
        .bind(5, utc_day * 86400).bind(6, kind).bind(7, "{}").step();
    apply_feedback(decision, utc_day, reward);
    tx.commit();
    return {{"decision_id", decision_id}, {"applied", true}};
}
} // namespace hi
