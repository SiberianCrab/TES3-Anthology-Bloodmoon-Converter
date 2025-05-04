#include <algorithm>
#include <format>
#include <cstdlib>

#include "ab_file_processor.h"
#include "ab_logger.h"
#include "ab_options.h"

// Function to check if file was already converted
bool hasConversionTag(const ordered_json& inputData, const std::filesystem::path& filePath, std::ofstream& logFile) {
    // Find the header section in the JSON data
    auto headerIter = std::find_if(inputData.begin(), inputData.end(), [](const ordered_json& item) {
        return item.contains("type") && item["type"] == "Header";
        });

    // Check if header contains a description field
    if (headerIter != inputData.end() && headerIter->contains("description")) {
        std::string description = (*headerIter)["description"];
        // Check conversion markers in the description
        if (description.find("Converted (RU->EN) by TES3 Ref_Ind Converter") != std::string::npos ||
            description.find("Converted (EN->RU) by TES3 Ref_Ind Converter") != std::string::npos) {

            return true;
        }
    }
    return false;
}

// Function to check the dependency order of Parent Master files in the input .ESP|ESM data
std::pair<bool, std::unordered_set<int>> checkDependencyOrder(const ordered_json& inputData, std::ofstream& logFile) {
    auto headerIter = std::find_if(inputData.begin(), inputData.end(), [](const ordered_json& item) {
        return item.contains("type") && item["type"] == "Header";
        });

    if (headerIter == inputData.end() || !headerIter->contains("masters")) {
        logMessage("ERROR - missing 'header' section or 'masters' key!\n", logFile);
        return { false, {} };
    }

    const auto& masters = (*headerIter)["masters"];
    std::optional<size_t> mwPos, tPos, bPos;

    for (size_t i = 0; i < masters.size(); ++i) {
        if (masters[i].is_array() && !masters[i].empty()) {
            const std::string masterName = masters[i][0];
            if (masterName == "Morrowind.esm") mwPos.emplace(i);
            else if (masterName == "Tribunal.esm") tPos.emplace(i);
            else if (masterName == "Bloodmoon.esm") bPos.emplace(i);
        }
    }

    if (!mwPos.has_value()) {
        logMessage("ERROR - Morrowind.esm dependency not found!", logFile);
        return { false, {} };
    }

    if (tPos.has_value() && bPos.has_value()) {
        if (*tPos > *mwPos && *bPos > *tPos) {
            logMessage("Valid order of Parent Master files found: M+T+B", logFile);
            return { true, {} };
        }
        logMessage("ERROR - invalid order of Parent Master files found: M+B+T", logFile);
        return { false, {} };
    }

    if (bPos.has_value() && *bPos > *mwPos) {
        logMessage("Valid order of Parent Master files found: M+B", logFile);
        return { true, {} };
    }

    return { false, {} };
}

// Function to add conversion tags to the header description
bool addConversionTag(ordered_json& inputData, const std::string& convPrefix, const ProgramOptions& options, std::ofstream& logFile) {
    // Find the Header block in JSON
    auto headerIter = std::find_if(inputData.begin(), inputData.end(), [](const auto& item) {
        return item.contains("type") && item["type"] == "Header";
        });

    if (headerIter != inputData.end() && headerIter->contains("description")) {
        // Get current description
        std::string currentDesc = (*headerIter)["description"];

        // Add conversion tag
        std::string conversionTag = "\r\n\r\nConverted (" + convPrefix + ") by TES3 Ref_Ind Converter";
        if (currentDesc.find(conversionTag) == std::string::npos) {
            (*headerIter)["description"] = currentDesc + conversionTag;
            if (!options.silentMode) {
                logMessage("Adding conversion tag to the file header...", logFile);
            }
        }
        return true;
    }

    return false;
}

// Function to create backup with automatic numbering
bool createBackup(const std::filesystem::path& filePath, const ProgramOptions& options, std::ofstream& logFile) {
    std::filesystem::path backupPath;
    int counter = 0;
    const int maxBackups = 1000;

    try {
        // First try simple .bac extension
        backupPath = filePath;
        backupPath += ".bac";

        // If simple backup exists, find next available numbered version
        while (std::filesystem::exists(backupPath) && counter < maxBackups) {
            backupPath = filePath;
            backupPath += std::format(".{:03d}.bac", counter++);
        }

        // Safety check to prevent infinite loops
        if (counter >= maxBackups) {
            logMessage("ERROR - reached maximum backup count (" + std::to_string(maxBackups) +
                       ") for file: " + filePath.string(), logFile);
            return false;
        }

        // Perform the actual backup by renaming the file
        std::filesystem::rename(filePath, backupPath);
        if (!options.silentMode) {
            logMessage("Original file backed up as: " + backupPath.string(), logFile);
        }
        return true;
    }
    catch (const std::exception& e) {
        // Log any errors that occur during backup process
        logMessage("ERROR - failed to create backup: " + filePath.string() + ": " + e.what() + "\n", logFile);
        return false;
    }
}

// Function to save the modified JSON data to file
bool saveJsonToFile(const std::filesystem::path& jsonImportPath, const ordered_json& inputData, const ProgramOptions& options, std::ofstream& logFile) {
    std::ofstream outputFile(jsonImportPath);
    if (!outputFile) return false;
    outputFile << std::setw(2) << inputData;
    if (!options.silentMode) {
        logMessage("Modified data saved as: " + jsonImportPath.string(), logFile);
    }
    return true;
}

// Function to convert the .JSON file to .ESP|ESM
bool convertJsonToEsp(const std::filesystem::path& jsonImportPath, const std::filesystem::path& espFilePath, const ProgramOptions& options, std::ofstream& logFile) {
    std::ostringstream command;
    command << TES3CONV_COMMAND << " "
            << std::quoted(jsonImportPath.string()) << " "
            << std::quoted(espFilePath.string());

    if (std::system(command.str().c_str()) != 0) {
        return false;
    }

    logMessage("Conversion to .ESP|ESM successful: " + espFilePath.string(), logFile);

    if (options.silentMode) {
        logMessage("", logFile);
    }

    return true;
}