#include "store.hpp"

namespace hi {
namespace {
constexpr auto schema = R"sql(
CREATE TABLE IF NOT EXISTS personas (
  namespace TEXT NOT NULL, id TEXT NOT NULL, payload TEXT NOT NULL, PRIMARY KEY(namespace,id)
);
CREATE TABLE IF NOT EXISTS persona_profiles (
  namespace TEXT NOT NULL, id TEXT NOT NULL, payload TEXT NOT NULL, PRIMARY KEY(namespace,id),
  FOREIGN KEY(namespace,id) REFERENCES personas(namespace,id)
);
CREATE TABLE IF NOT EXISTS script_pattern_versions (
  seq INTEGER PRIMARY KEY, namespace TEXT NOT NULL, version TEXT NOT NULL, payload TEXT NOT NULL,
  UNIQUE(namespace,version)
);
CREATE TABLE IF NOT EXISTS script_patterns (
  version_seq INTEGER NOT NULL REFERENCES script_pattern_versions(seq), id TEXT NOT NULL, payload TEXT NOT NULL,
  PRIMARY KEY(version_seq,id)
);
CREATE INDEX IF NOT EXISTS script_pattern_lookup ON script_patterns(id,version_seq DESC);
CREATE TABLE IF NOT EXISTS rule_sets (namespace TEXT PRIMARY KEY, payload TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS persona_decision_log (
  id INTEGER PRIMARY KEY, namespace TEXT NOT NULL, persona_id TEXT NOT NULL, topic TEXT NOT NULL, payload TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS latest_topic ON persona_decision_log(namespace,persona_id,topic,id DESC);
CREATE TABLE IF NOT EXISTS persona_feedback_log (
  namespace TEXT NOT NULL, source_id TEXT NOT NULL, decision_id INTEGER REFERENCES persona_decision_log(id),
  persona_id TEXT NOT NULL, ts INTEGER NOT NULL, kind TEXT NOT NULL, payload TEXT NOT NULL,
  PRIMARY KEY(namespace,source_id)
);
CREATE TABLE IF NOT EXISTS persona_learning (
  namespace TEXT NOT NULL, persona_id TEXT NOT NULL, pattern_id TEXT NOT NULL, pattern_version TEXT NOT NULL,
  scope TEXT NOT NULL, key TEXT NOT NULL, day INTEGER NOT NULL, baseline REAL NOT NULL, value REAL NOT NULL,
  PRIMARY KEY(namespace,persona_id,pattern_id,pattern_version,scope,key)
);
CREATE TABLE IF NOT EXISTS persona_learning_history (
  id INTEGER PRIMARY KEY, decision_id INTEGER NOT NULL REFERENCES persona_decision_log(id),
  day INTEGER NOT NULL, scope TEXT NOT NULL, key TEXT NOT NULL, before REAL NOT NULL, after REAL NOT NULL
);
)sql";

Json find_persona(Database& db, std::string_view table, std::string_view ns, std::string_view id) {
    const auto sql = "SELECT payload FROM " + std::string(table)
                   + " WHERE id=? AND namespace IN (?,'shared') ORDER BY namespace=? DESC LIMIT 1";
    auto statement = db.query(sql);
    statement.bind(1, id).bind(2, ns).bind(3, ns);
    return statement.step() ? read_json(statement.text(0)) : Json{};
}
}

Store::Store(const std::filesystem::path& path, const Json& patterns, const Json& roles)
    : db_(path), roles_(review_roles(roles)) {
    db_.exec("PRAGMA journal_mode=WAL;");
    db_.exec(schema);
    Transaction tx(db_);
    add_patterns("shared", review_patterns(patterns));
    tx.commit();
}

Json Store::put_persona_unlocked(std::string_view ns, const Json& persona) {
    if (!persona.is_object()) throw InputError("expected a persona object");
    const auto id = required_string(persona, "id");
    check_key(id);
    const auto existing = find_persona(db_, "personas", ns, id);
    auto payload = !existing.is_null() && field(existing, "namespace") == ns ? existing : Json::object();
    // Store only persona fields. Conversation participants belong to the input buffer.
    for (const auto* key : {"id", "name", "display_name", "description", "speech_style", "skill_template",
                            "seed", "script_pattern_id", "origin_session_id"}) {
        if (persona.contains(key)) payload[key] = field(persona, key);
    }
    for (const auto* key : {"traits", "learned_notes"}) {
        if (!persona.contains(key)) continue;
        const auto& values = persona.at(key);
        if (!values.is_array() || values.size() > 128 || !std::ranges::all_of(values, [](const Json& value) {
            return value.is_string();
        })) throw InputError("expected a text array");
        payload[key] = values;
    }
    for (const auto* key : {"generated", "created_at", "updated_at"}) {
        if (persona.contains(key)) payload[key] = persona.at(key);
    }
    if (persona.contains("signals")) {
        const auto& signals = persona.at("signals");
        if (!signals.is_object()) throw InputError("signals must be an object");
        auto activity = Json::object();
        for (const auto* key : {"role_label", "provider", "repo_base", "prompt_count", "edit_count",
                                "dominant_tools", "file_focus"}) {
            if (signals.contains(key)) activity[key] = signals.at(key);
        }
        payload["signals"] = std::move(activity);
    }
    if (field(payload, "name").empty()) payload["name"] = id;
    payload["namespace"] = ns;
    auto profile = derive_profile(payload);
    const auto pattern_id = required_string(profile, "script_pattern_id");
    check_key(pattern_id);
    const auto pattern = script(ns, pattern_id);
    if (pattern.is_null()) throw InputError("unknown script pattern");
    profile["id"] = id;
    profile["namespace"] = ns;
    profile["script_pattern_version"] = pattern.at("version");
    const auto encoded = payload.dump();
    if (encoded.size() > 32 * 1024) throw InputError("persona exceeds storage limit");
    db_.query("INSERT INTO personas VALUES(?,?,?) ON CONFLICT(namespace,id) DO UPDATE SET payload=excluded.payload")
        .bind(1, ns).bind(2, id).bind(3, encoded).step();
    db_.query("INSERT INTO persona_profiles VALUES(?,?,?) ON CONFLICT(namespace,id) DO UPDATE SET payload=excluded.payload")
        .bind(1, ns).bind(2, id).bind(3, profile.dump()).step();
    return payload;
}

Json Store::put_persona(std::string_view ns, const Json& persona) {
    check_namespace(ns);
    std::lock_guard lock(mutex_);
    Transaction tx(db_);
    auto result = put_persona_unlocked(ns, persona);
    tx.commit();
    return result;
}

Json Store::list_personas(std::string_view ns) {
    check_namespace(ns);
    std::lock_guard lock(mutex_);
    auto rows = db_.query("SELECT payload FROM personas p WHERE namespace=? OR (namespace='shared' "
                         "AND NOT EXISTS(SELECT 1 FROM personas q WHERE q.id=p.id AND q.namespace=?)) ORDER BY id LIMIT 256");
    rows.bind(1, ns).bind(2, ns);
    auto list = Json::array();
    while (rows.step()) list.push_back(read_json(rows.text(0)));
    return list;
}

Json Store::persona(std::string_view ns, std::string_view id) {
    check_namespace(ns);
    check_key(id);
    std::lock_guard lock(mutex_);
    return find_persona(db_, "personas", ns, id);
}

Json Store::profile(std::string_view ns, std::string_view id) {
    check_namespace(ns);
    check_key(id);
    std::lock_guard lock(mutex_);
    auto result = find_persona(db_, "persona_profiles", ns, id);
    if (!result.is_null()) {
        const auto pattern = script(ns, required_string(result, "script_pattern_id"));
        result["script_pattern_version"] = pattern.at("version");
        result["script_pattern_label"] = pattern.at("label");
    }
    return result;
}

Json Store::render_snapshot(std::string_view ns, std::string_view id) {
    check_namespace(ns);
    check_key(id);
    std::lock_guard lock(mutex_);
    const auto persona = find_persona(db_, "personas", ns, id);
    if (persona.is_null()) return {};
    const auto profile = find_persona(db_, "persona_profiles", ns, id);
    const auto pattern = script(ns, required_string(profile, "script_pattern_id"));
    auto matrix = roles_;
    auto rule_set = db_.query("SELECT payload FROM rule_sets WHERE namespace IN (?,'shared') ORDER BY namespace=? DESC LIMIT 1");
    rule_set.bind(1, ns).bind(2, ns);
    if (rule_set.step()) matrix = read_json(rule_set.text(0)).value("role_matrix", matrix);
    return {{"persona", persona}, {"profile", profile}, {"pattern", pattern}, {"matrix", matrix}};
}

void Store::add_patterns(std::string_view ns, const Json& document) {
    const auto version = required_string(document, "version");
    auto existing = db_.query("SELECT payload FROM script_pattern_versions WHERE namespace=? AND version=?");
    existing.bind(1, ns).bind(2, version);
    if (existing.step()) {
        if (read_json(existing.text(0)) != document) throw Conflict("script version already has different content");
        return;
    }
    db_.query("INSERT INTO script_pattern_versions(namespace,version,payload) VALUES(?,?,?)")
        .bind(1, ns).bind(2, version).bind(3, document.dump()).step();
    const auto seq = db_.last_id();
    for (auto pattern : document.at("patterns")) {
        pattern["version"] = version;
        pattern["namespace"] = ns;
        db_.query("INSERT INTO script_patterns VALUES(?,?,?)")
            .bind(1, seq).bind(2, required_string(pattern, "id")).bind(3, pattern.dump()).step();
    }
}

Json Store::script(std::string_view ns, std::string_view id) {
    auto row = db_.query("SELECT p.payload FROM script_patterns p JOIN script_pattern_versions v ON p.version_seq=v.seq "
                        "WHERE v.namespace IN (?,'shared') AND p.id=? ORDER BY (v.namespace=?) DESC,v.seq DESC LIMIT 1");
    row.bind(1, ns).bind(2, id).bind(3, ns);
    return row.step() ? read_json(row.text(0)) : Json{};
}

Json Store::put_patterns(std::string_view ns, const Json& document) {
    check_namespace(ns);
    const auto reviewed = review_patterns(document);
    std::lock_guard lock(mutex_);
    Transaction tx(db_);
    add_patterns(ns, reviewed);
    tx.commit();
    return reviewed;
}

Json Store::patterns(std::string_view ns) {
    check_namespace(ns);
    std::lock_guard lock(mutex_);
    auto ids = db_.query("SELECT DISTINCT p.id FROM script_patterns p JOIN script_pattern_versions v ON p.version_seq=v.seq "
                         "WHERE v.namespace IN (?,'shared') ORDER BY p.id LIMIT 256");
    ids.bind(1, ns);
    Json result = Json::array();
    while (ids.step()) result.push_back(script(ns, ids.text(0)));
    return result;
}

Json Store::put_rules(std::string_view ns, const Json& document) {
    check_namespace(ns);
    auto reviewed = review_rules(document);
    std::lock_guard lock(mutex_);
    db_.query("INSERT INTO rule_sets VALUES(?,?) ON CONFLICT(namespace) DO UPDATE SET payload=excluded.payload")
        .bind(1, ns).bind(2, reviewed.dump()).step();
    return reviewed;
}

Json Store::rules(std::string_view ns) {
    check_namespace(ns);
    std::lock_guard lock(mutex_);
    auto row = db_.query("SELECT payload FROM rule_sets WHERE namespace IN (?,'shared') ORDER BY namespace=? DESC LIMIT 1");
    row.bind(1, ns).bind(2, ns);
    return row.step() ? read_json(row.text(0)) : Json{{"rules", Json::array()}};
}

Json Store::decide(std::string_view ns, std::string_view id, const Json& request) {
    check_namespace(ns);
    check_key(id);
    const auto topic = field(request, "topic");
    if (!topic.empty()) check_key(topic);
    std::lock_guard lock(mutex_);
    Transaction tx(db_);
    const auto persona = find_persona(db_, "personas", ns, id);
    if (persona.is_null()) return {};
    const auto profile = find_persona(db_, "persona_profiles", ns, id);
    auto pattern = script(ns, required_string(profile, "script_pattern_id"));
    int turns = 0;
    const auto settings = decision_settings(ns, id, pattern, request, turns);
    for (auto& [feature, vector] : pattern.at("reactions").items()) {
        const auto modifier = settings.at("reaction_learning").value(feature, 0.);
        for (auto& axis : vector) axis = std::clamp(axis.get<double>() * (1 + modifier), -1., 1.);
    }
    auto decision = decision_record(persona, profile, pattern, request, settings, turns);
    decision["namespace"] = ns;
    decision["persona_id"] = id;
    decision["script_pattern_namespace"] = pattern.at("namespace");
    decision["reason"]["profile"] = profile;
    decision["reason"]["pattern"] = pattern;
    decision["reason"]["matrix"] = settings.at("matrix");
    decision["reason"]["reaction_learning"] = settings.at("reaction_learning");
    db_.query("INSERT INTO persona_decision_log(namespace,persona_id,topic,payload) VALUES(?,?,?,?)")
        .bind(1, ns).bind(2, id).bind(3, topic).bind(4, decision.dump()).step();
    decision["decision_id"] = db_.last_id();
    tx.commit();
    return decision;
}

Json Store::audit(std::string_view ns, std::int64_t id) {
    check_namespace(ns);
    std::lock_guard lock(mutex_);
    auto row = db_.query("SELECT payload FROM persona_decision_log WHERE namespace=? AND id=?");
    row.bind(1, ns).bind(2, id);
    if (!row.step()) return {};
    auto record = read_json(row.text(0));
    record["decision_id"] = id;
    auto changes = db_.query("SELECT day,scope,key,before,after FROM persona_learning_history WHERE decision_id=? ORDER BY id LIMIT 256");
    changes.bind(1, id);
    auto history = Json::array();
    while (changes.step()) {
        history.push_back({{"day", changes.number(0)}, {"scope", changes.text(1)}, {"key", changes.text(2)},
                           {"before", changes.real(3)}, {"after", changes.real(4)}});
    }
    record["learning_history"] = std::move(history);
    return record;
}
} // namespace hi
