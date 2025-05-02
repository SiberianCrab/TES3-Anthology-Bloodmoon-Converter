#pragma once
#include <iostream>
#include <unordered_set>
#include <utility>
#include <string>
#include <fstream>

#include <sqlite3.h>

#include <ab_database.h>
#include <ab_options.h>

// Custom hash function
struct PairHash {
    std::size_t operator()(const std::pair<int, int>& p) const;
};

// Structure for storing GridOffset
struct GridOffset {
    int offsetX;
    int offsetY;
};

// Function to determine GridOffsets based on conversion choice
GridOffset getGridOffset(int conversionType);

// Function to load custom grid coordinates
void loadCustomGridCoordinates(const std::string& filePath,
    std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates,
    std::ofstream& logFile);

// Function to check if a given grid coordinate (gridX, gridY) is valid. It checks both the database and custom user-defined coordinates
bool isCoordinateValid(const Database& db,
    int gridX,
    int gridY,
    const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates,
    const ProgramOptions& options,
    std::ofstream& logFile);