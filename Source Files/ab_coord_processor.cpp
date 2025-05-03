#include <iostream>
#include <unordered_set>
#include <sstream>
#include <string>

#include <sqlite3.h>

#include "ab_coord_processor.h"
#include "ab_logger.h"

// Custom hash function
std::size_t PairHash::operator()(const std::pair<int, int>& p) const {
    std::size_t hash1 = std::hash<int>{}(p.first);
    std::size_t hash2 = std::hash<int>{}(p.second);
    return hash1 ^ (hash2 + 0x9e3779b9 + (hash1 << 6) + (hash1 >> 2));
}

// Function to determine GridOffsets based on conversion choice
GridOffset getGridOffset(int conversionType) {
    if (conversionType == 1) return { 7, 6 };
    else return { -7, -6 };
}

// Function to load custom grid coordinates
void loadCustomGridCoordinates(const std::string& filePath,
    std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates,
    std::ofstream& logFile) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        logMessage("ERROR - failed to open custom grid coordinates file: " + filePath, logFile);
        return;
    }

    bool hasAnyParsableLines = false;

    std::string line;
    while (std::getline(file, line)) {
        // Trim leading and trailing whitespace
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        // Skip empty lines and comments
        if (line.empty() || line.find("//") == 0) {
            continue;
        }

        // Search for first parsable line, log the header
        if (!hasAnyParsableLines) {
            logMessage("Loading custom grid coordinates:", logFile);
            hasAnyParsableLines = true;
        }

        std::istringstream lineStream(line);
        int x, y;
        char comma;

        if (lineStream >> x >> comma >> y && comma == ',') {
            customCoordinates.emplace(x, y);
            logMessage("- Coordinate: " + std::to_string(x) + "," + std::to_string(y), logFile);
        }
        else {
            logMessage("WARNING - invalid coordinate format: " + line, logFile);
        }
    }

    file.close();
}

// Function to check if a given grid coordinate (gridX, gridY) is valid. It checks both the database and custom user-defined coordinates
bool isCoordinateValid(const Database& db,
    int gridX,
    int gridY,
    const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates,
    const ProgramOptions& options,
    std::ofstream& logFile) {
    // Get the offset from the getGridOffset function
    GridOffset offset = getGridOffset(options.conversionType);

    int adjustedGridX = gridX;
    int adjustedGridY = gridY;

    // Apply the offset to the coordinates only for conversionType = 2
    if (options.conversionType == 2) {
        adjustedGridX = gridX + offset.offsetX;
        adjustedGridY = gridY + offset.offsetY;
    }

    // Check in the database
    sqlite3_stmt* stmt;
    std::string query = "SELECT BM_Grid_X, BM_Grid_Y FROM [tes3_ab_cell_x-y_data] WHERE BM_Grid_X = ? AND BM_Grid_Y = ?";

    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Error preparing database query: " + std::string(sqlite3_errmsg(db)) << "\n";
        if (stmt) sqlite3_finalize(stmt);
        return false;
    }

    sqlite3_bind_int(stmt, 1, adjustedGridX);
    sqlite3_bind_int(stmt, 2, adjustedGridY);

    bool foundInDB = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);

    // If found in the database, return true
    if (foundInDB) {
        return true;
    }

    // Check in custom coordinates
    if (customCoordinates.find({ gridX, gridY }) != customCoordinates.end()) {
        return true;
    }

    // Not found in either
    return false;
}