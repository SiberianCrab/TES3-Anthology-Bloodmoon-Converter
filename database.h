#pragma once
#include <sqlite3.h>
#include <memory>
#include <stdexcept>
#include <string>

class Database {
public:
    // Constructor that opens the database
    explicit Database(const std::string& filename);

    // Disable copy semantics
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    // Enable move semantics
    Database(Database&&) = default;
    Database& operator=(Database&&) = default;

    // Implicit conversion to sqlite3* for compatibility with SQLite C API
    operator sqlite3* () const { return db_.get(); }

    // Explicit getter for the raw sqlite3* pointer
    sqlite3* get() const { return db_.get(); }

    // Check whether the database connection is valid
    bool is_valid() const { return db_ != nullptr; }

private:
    struct Deleter {
        void operator()(sqlite3* db) const {
            if (db) sqlite3_close(db);
        }
    };

    std::unique_ptr<sqlite3, Deleter> db_;
};