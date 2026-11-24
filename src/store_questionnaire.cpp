#include "store.hpp"

namespace hi {
namespace {
Json exact_persona(Database& db, std::string_view ns, std::string_view id) {
    auto row = db.query("SELECT payload FROM personas WHERE namespace=? AND id=?");
    row.bind(1, ns).bind(2, id);
    return row.step() ? read_json(row.text(0)) : Json{};
}

struct PendingRun {
    std::mutex& mutex;
    std::set<std::pair<std::string, std::string>>& pending;
    const std::pair<std::string, std::string>& key;
    ~PendingRun() {
        std::lock_guard lock(mutex);
        pending.erase(key);
    }
};
} // namespace

std::int64_t Store::latest_questionnaire_id(std::string_view ns, std::string_view id) {
    auto row = db_.query("SELECT id FROM persona_questionnaire_runs WHERE namespace=? AND persona_id=? ORDER BY id DESC LIMIT 1");
    row.bind(1, ns).bind(2, id);
    return row.step() ? row.number(0) : 0;
}

Json Store::run_questionnaire(std::string_view ns, std::string_view id, bool rerun,
                             const LifeScriptData& data, const Invoke& invoke) {
    check_namespace(ns);
    check_key(id);
    const std::pair<std::string, std::string> key{ns, id};
    Json persona;
    std::int64_t previous;
    {
        std::lock_guard lock(mutex_);
        persona = exact_persona(db_, ns, id);
        if (persona.is_null()) return {};
        previous = latest_questionnaire_id(ns, id);
        if (!rerun && previous != 0) throw Conflict("questionnaire_already_run");
        if (!questionnaire_eligible(persona)) throw InputError("questionnaire requires a virtual persona with a description");
        if (!pending_questionnaires_.insert(key).second) throw Conflict("questionnaire_in_progress");
    }
    const PendingRun pending{mutex_, pending_questionnaires_, key};
    Json answers;
    Json score;
    Json examples;
    try {
        if (!invoke) throw QuestionnaireError("Claude questionnaire client is not configured");
        answers = generate_life_script_answers(data.questionnaire, persona, invoke);
        score = score_life_script(data.questionnaire, answers);
        const auto plan = plan_life_script_followups(data, score);
        examples = generate_reaction_examples(plan.at("topics"), persona, invoke);
    } catch (const std::exception&) {
        error_log("questionnaire failed: invocation, answer validation, or scoring");
        throw QuestionnaireError("questionnaire_generation_failed");
    }
    auto assignment = keep_registered_pattern(persona, questionnaire_assignment(derive_profile(persona), score));
    const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()).time_since_epoch().count();
    std::lock_guard lock(mutex_);
    Transaction tx(db_);
    if (exact_persona(db_, ns, id) != persona || latest_questionnaire_id(ns, id) != previous)
        throw Conflict("questionnaire_persona_or_run_changed");
    const auto pattern = script(ns, required_string(assignment, "script_pattern_id"));
    if (pattern.is_null()) throw InputError("unknown script pattern");
    assignment["script_pattern_version"] = pattern.at("version");
    assignment["id"] = id;
    assignment["namespace"] = ns;
    const auto questionnaire = required_string(data.questionnaire, "id");
    const auto revision = required_string(data.questionnaire, "revision");
    const auto followup = required_string(data.bank, "revision");
    db_.query("INSERT INTO persona_questionnaire_runs(namespace,persona_id,questionnaire,revision,followup_revision,"
              "role,prompt_revision,created_at,answers,score,assignment) VALUES(?,?,?,?,?,?,?,?,?,?,?)")
        .bind(1, ns).bind(2, id).bind(3, questionnaire).bind(4, revision).bind(5, followup)
        .bind(6, questionnaire_role).bind(7, questionnaire_prompt_revision).bind(8, now)
        .bind(9, answers.dump()).bind(10, score.dump()).bind(11, assignment.dump()).step();
    const auto run = db_.last_id();
    assignment["questionnaire_run_id"] = run;
    db_.query("UPDATE persona_profiles SET payload=? WHERE namespace=? AND id=?")
        .bind(1, assignment.dump()).bind(2, ns).bind(3, id).step();
    std::int64_t ordinal = 0;
    for (const auto& example : examples) {
        db_.query("INSERT INTO persona_reaction_examples VALUES(?,?,?,?)")
            .bind(1, run).bind(2, ++ordinal).bind(3, field(example, "ask")).bind(4, field(example, "answer")).step();
    }
    tx.commit();
    return {{"run_id", run}, {"questionnaire", questionnaire}, {"revision", revision}, {"followup_revision", followup},
            {"role", questionnaire_role}, {"prompt_revision", questionnaire_prompt_revision}, {"created_at", now},
            {"score", score}, {"assignment", assignment}};
}

Json Store::questionnaire(std::string_view ns, std::string_view id, bool include_answers) {
    check_namespace(ns);
    check_key(id);
    std::lock_guard lock(mutex_);
    auto row = db_.query("SELECT id,questionnaire,revision,followup_revision,role,prompt_revision,created_at,score,assignment,"
                         "CASE WHEN ? THEN answers ELSE NULL END FROM persona_questionnaire_runs "
                         "WHERE namespace=? AND persona_id=? ORDER BY id DESC LIMIT 1");
    row.bind(1, static_cast<std::int64_t>(include_answers)).bind(2, ns).bind(3, id);
    if (!row.step()) return {};
    auto assignment = read_json(row.text(8));
    assignment["questionnaire_run_id"] = row.number(0);
    Json run{{"run_id", row.number(0)}, {"questionnaire", row.text(1)}, {"revision", row.text(2)},
             {"followup_revision", row.text(3)}, {"role", row.text(4)}, {"prompt_revision", row.text(5)},
             {"created_at", row.number(6)}, {"score", read_json(row.text(7))}, {"assignment", std::move(assignment)}};
    if (include_answers) run["answers"] = read_json(row.text(9));
    return run;
}

Json Store::questionnaire_examples(const Json& profile) {
    auto result = Json::array();
    if (!profile.contains("questionnaire_run_id")) return result;
    std::lock_guard lock(mutex_);
    auto rows = db_.query("SELECT question,answer FROM persona_reaction_examples WHERE run_id=? ORDER BY ordinal");
    rows.bind(1, profile.at("questionnaire_run_id").get<std::int64_t>());
    while (rows.step()) result.push_back({{"ask", rows.text(0)}, {"answer", rows.text(1)}});
    return result;
}

Json Store::rerun_questionnaires(const LifeScriptData& data, const Invoke& invoke) {
    std::int64_t completed = 0;
    std::int64_t failed = 0;
    for (const auto ns : {"cc", "di", "shared"}) {
        std::string after;
        for (;;) {
            Json persona;
            {
                std::lock_guard lock(mutex_);
                auto row = db_.query("SELECT payload FROM personas WHERE namespace=? AND id>? ORDER BY id LIMIT 1");
                row.bind(1, ns).bind(2, after);
                if (!row.step()) break;
                persona = read_json(row.text(0));
            }
            after = required_string(persona, "id");
            if (!questionnaire_eligible(persona)) continue;
            try {
                if (!run_questionnaire(ns, after, true, data, invoke).is_null()) ++completed;
            } catch (const std::exception&) {
                ++failed;
                error_log("bulk questionnaire skipped one failed persona");
            }
        }
    }
    return {{"completed", completed}, {"failed", failed}};
}
} // namespace hi
