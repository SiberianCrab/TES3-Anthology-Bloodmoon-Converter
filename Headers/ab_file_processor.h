#pragma once
#include <unordered_set>
#include <filesystem>
#include <fstream>

#include "ab_options.h"

// Function to check if file was already converted
bool hasConversionTag(const ordered_json& inputData, const std::filesystem::path& filePath, std::ofstream& logFile);

// Function to check the dependency order of Parent Master files in the input .ESP|ESM data
std::pair<bool, std::unordered_set<int>> checkDependencyOrder(const ordered_json& inputData, std::ofstream& logFile);

// Function to add conversion tags to the header description
bool addConversionTag(ordered_json& inputData, const std::string& convPrefix, const ProgramOptions& options, std::ofstream& logFile);

// Function to create backup with automatic numbering
bool createBackup(const std::filesystem::path& filePath, const ProgramOptions& options, std::ofstream& logFile);

// Function to save the modified JSON data to file
bool saveJsonToFile(const std::filesystem::path& jsonImportPath, const ordered_json& inputData, const ProgramOptions& options, std::ofstream& logFile);

// Function to convert the .JSON file to .ESP|ESM
bool convertJsonToEsp(const std::filesystem::path& jsonImportPath, const std::filesystem::path& espFilePath, std::ofstream& logFile);