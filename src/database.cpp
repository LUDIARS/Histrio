#include "database.hpp"

#include <limits>
#include <sqlite3.h>

namespace hi {
namespace {
void check(int result) {
    if (result != SQLITE_OK) throw std::runtime_error("SQLite operation failed (" + std::to_string(result) + ")");
}
}

Statement::Statement(sqlite3* db, std::string_view sql) : db_(db) {
    if (sql.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::runtime_error("SQL statement is too long");
    const int result = sqlite3_prepare_v2(db_, sql.data(), static_cast<int>(sql.size()), &stmt_, nullptr);
    if (result != SQLITE_OK) {
        sqlite3_finalize(stmt_);
        check(result);
    }
}

Statement::~Statement() { sqlite3_finalize(stmt_); }

Statement& Statement::bind(int index, std::string_view value) {
    check(sqlite3_bind_text64(stmt_, index, value.empty() ? "" : value.data(), value.size(), SQLITE_TRANSIENT, SQLITE_UTF8));
    return *this;
}

Statement& Statement::bind(int index, std::int64_t value) {
    check(sqlite3_bind_int64(stmt_, index, value));
    return *this;
}

Statement& Statement::bind_real(int index, double value) {
    check(sqlite3_bind_double(stmt_, index, value));
    return *this;
}

bool Statement::step() {
    const auto result = sqlite3_step(stmt_);
    if (result == SQLITE_DONE) return false;
    if (result == SQLITE_ROW) return true;
    check(result);
    return false;
}

std::string Statement::text(int column) const {
    const auto* bytes = sqlite3_column_text(stmt_, column);
    return bytes ? std::string(reinterpret_cast<const char*>(bytes), sqlite3_column_bytes(stmt_, column)) : "";
}

std::int64_t Statement::number(int column) const { return sqlite3_column_int64(stmt_, column); }
double Statement::real(int column) const { return sqlite3_column_double(stmt_, column); }

Database::Database(const std::filesystem::path& path, bool read_only) {
    const int mode = read_only ? SQLITE_OPEN_READONLY : SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
    const auto name = path.u8string();
    try {
        check(sqlite3_open_v2(reinterpret_cast<const char*>(name.c_str()), &db_, mode | SQLITE_OPEN_FULLMUTEX, nullptr));
        sqlite3_limit(db_, SQLITE_LIMIT_LENGTH, static_cast<int>(max_body));
        check(sqlite3_busy_timeout(db_, 5000));
        check(sqlite3_db_config(db_, SQLITE_DBCONFIG_DEFENSIVE, 1, nullptr));
        exec("PRAGMA trusted_schema=OFF; PRAGMA foreign_keys=ON;");
    } catch (...) {
        sqlite3_close(db_);
        throw;
    }
}

Database::~Database() { sqlite3_close(db_); }
void Database::exec(const char* sql) { check(sqlite3_exec(db_, sql, nullptr, nullptr, nullptr)); }
std::int64_t Database::last_id() const { return sqlite3_last_insert_rowid(db_); }
int Database::changes() const { return sqlite3_changes(db_); }

Transaction::Transaction(Database& db) : db_(db) { db_.exec("BEGIN IMMEDIATE"); }

Transaction::~Transaction() {
    if (committed_) return;
    try {
        db_.exec("ROLLBACK");
    } catch (...) {
        error_log("SQLite rollback failed");
    }
}

void Transaction::commit() {
    db_.exec("COMMIT");
    committed_ = true;
}
} // namespace hi
