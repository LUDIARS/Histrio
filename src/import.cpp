#include "store.hpp"

#include <lorica/checked_size.hpp>

namespace hi {
namespace {
Json legacy_json(std::string_view text, lorica::input_budget& budget) {
    return read_json(text, budget);
}

void budget_row(const Json& row, lorica::input_budget& budget) {
    const auto size = lorica::checked_add(row.dump().size(), sizeof(Json));
    if (!size || !budget.consume(*size, 1, 1)) throw InputError("legacy import exceeds its budget");
}
}

Json Store::import_legacy(const std::filesystem::path& source) {
    Database legacy(source, true);
    std::lock_guard lock(mutex_);
    Transaction tx(db_);
    lorica::input_budget budget({max_body * 32, 250000, 250001, 32});
    std::int64_t personas = 0;
    std::int64_t feedback = 0;

    auto rows = legacy.query("SELECT id,name,description,traits,speech_style,skill_template,learned_notes,display_name,"
                             "generated,origin_session_id,created_at,updated_at FROM main.personas ORDER BY id");
    while (rows.step()) {
        Json value{{"id", rows.text(0)}, {"name", rows.text(1)}, {"description", rows.text(2)},
                   {"traits", legacy_json(rows.text(3), budget)}, {"speech_style", rows.text(4)},
                   {"skill_template", rows.text(5)}, {"learned_notes", legacy_json(rows.text(6), budget)},
                   {"display_name", rows.text(7)}, {"generated", rows.number(8)}, {"origin_session_id", rows.text(9)},
                   {"created_at", rows.number(10)}, {"updated_at", rows.number(11)}};
        budget_row(value, budget);
        auto exists = db_.query("SELECT 1 FROM personas WHERE namespace='cc' AND id=?");
        exists.bind(1, rows.text(0));
        if (!exists.step()) {
            put_persona_unlocked("cc", value);
            ++personas;
        }
    }

    auto history = legacy.query("SELECT id,persona_id,session_id,ts,kind,delta,detail FROM main.persona_feedback_log ORDER BY id");
    while (history.step()) {
        const Json value{{"session_id", history.text(2)}, {"delta", history.text(5)}, {"detail", history.text(6)}};
        budget_row(value, budget);
        db_.query("INSERT OR IGNORE INTO persona_feedback_log VALUES('cc',?,NULL,?,?,?,?)")
            .bind(1, "cc:" + std::to_string(history.number(0))).bind(2, history.text(1))
            .bind(3, history.number(3)).bind(4, history.text(4)).bind(5, value.dump()).step();
        feedback += db_.changes();
    }
    tx.commit();
    return {{"personas", personas}, {"feedback", feedback}};
}
} // namespace hi
