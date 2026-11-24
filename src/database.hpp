#pragma once

#include "service.hpp"

struct sqlite3;
struct sqlite3_stmt;

namespace hi {
class Statement {
public:
    Statement(sqlite3* db, std::string_view sql);
    ~Statement();
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    Statement& bind(int index, std::string_view value);
    Statement& bind(int index, std::int64_t value);
    Statement& bind_real(int index, double value);
    bool step();
    std::string text(int column) const;
    std::int64_t number(int column) const;
    double real(int column) const;

private:
    sqlite3* db_;
    sqlite3_stmt* stmt_ = nullptr;
};

class Database {
public:
    explicit Database(const std::filesystem::path& path, bool read_only = false);
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    Statement query(std::string_view sql) { return Statement(db_, sql); }
    void exec(const char* sql);
    std::int64_t last_id() const;
    int changes() const;

private:
    sqlite3* db_ = nullptr;
};

class Transaction {
public:
    explicit Transaction(Database& db);
    ~Transaction();
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    void commit();

private:
    Database& db_;
    bool committed_ = false;
};
} // namespace hi
