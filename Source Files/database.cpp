#include "database.h"

Database::Database(const std::string& filename) {
    sqlite3* db_raw = nullptr;
    const int result = sqlite3_open(filename.c_str(), &db_raw);

    if (result != SQLITE_OK) {
        const std::string error_msg = db_raw ? sqlite3_errmsg(db_raw) : "unknown error";
        if (db_raw) sqlite3_close(db_raw);
        throw std::runtime_error("Failed to open database: " + error_msg);
    }

    db_.reset(db_raw);
}