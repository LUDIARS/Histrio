#include "store.hpp"

namespace hi {

Json Store::delete_persona(std::string_view ns, std::string_view id) {
    check_namespace(ns);
    check_key(id);
    std::lock_guard lock(mutex_);
    if (pending_questionnaires_.contains({std::string(ns), std::string(id)}))
        throw Conflict("questionnaire is running for this persona");
    Transaction tx(db_);
    // Delete only rows in the requested namespace. A shared persona stays visible from other namespaces.
    const auto run = [&](const char* sql) { db_.query(sql).bind(1, ns).bind(2, id).step(); };
    run("DELETE FROM persona_reaction_examples WHERE run_id IN "
        "(SELECT id FROM persona_questionnaire_runs WHERE namespace=? AND persona_id=?)");
    run("DELETE FROM persona_questionnaire_runs WHERE namespace=? AND persona_id=?");
    run("DELETE FROM persona_learning_history WHERE decision_id IN "
        "(SELECT id FROM persona_decision_log WHERE namespace=? AND persona_id=?)");
    run("DELETE FROM persona_feedback_log WHERE namespace=? AND persona_id=?");
    run("DELETE FROM persona_decision_log WHERE namespace=? AND persona_id=?");
    run("DELETE FROM persona_learning WHERE namespace=? AND persona_id=?");
    run("DELETE FROM persona_profiles WHERE namespace=? AND id=?");
    run("DELETE FROM personas WHERE namespace=? AND id=?");
    const auto removed = db_.changes() > 0;
    tx.commit();
    if (!removed) return Json{};
    return Json{{"id", id}, {"namespace", ns}, {"deleted", true}};
}

} // namespace hi
