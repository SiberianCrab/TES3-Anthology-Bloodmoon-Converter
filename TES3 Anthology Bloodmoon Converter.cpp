#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <cctype>
#include <cstdlib>

#include <json.hpp>
#include <sqlite3.h>

// Define an alias for ordered_json type from the nlohmann library
using ordered_json = nlohmann::ordered_json;

// Define program metadata constants
const std::string PROGRAM_NAME = "TES3 Anthology Bloodmoon Converter";
const std::string PROGRAM_VERSION = "V 1.1.1";
const std::string PROGRAM_AUTHOR = "by SiberianCrab";

// Define the GridOffset structure
struct GridOffset {
    int offsetX;
    int offsetY;
};

// Function to determine offsets based on conversion choice
GridOffset getGridOffset(int conversionChoice) {
    return (conversionChoice == 1) ? GridOffset{ 7, 6 } : GridOffset{ -7, -6 };
}

// Function to clear log file
void logClear() {
    std::ofstream file("tes3_ab_log.txt", std::ios::trunc);
}

// Function to log messages to both a log file and console
void logMessage(const std::string& message, std::ofstream& logFile) {
    logFile << message << std::endl;
    std::cout << message << std::endl;
}

// Function to log errors, close the database and terminate the program
void logErrorAndExit(sqlite3* db, const std::string& message, std::ofstream& logFile) {
    logMessage(message, logFile);

    if (db) sqlite3_close(db);
    logFile.close();

    std::cout << "Press Enter to exit...";
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    std::exit(EXIT_FAILURE);
}

// Unified function for handling user choices
int getUserChoice(const std::string& prompt,
    const std::unordered_set<std::string>& validChoices,
    std::ofstream& logFile,
    const std::string& errorMessage = "\nInvalid choice: enter ")
{
    std::string input;
    while (true) {
        std::cout << prompt;
        std::getline(std::cin, input);

        if (validChoices.count(input)) {
            return std::stoi(input);
        }

        // List of valid options for the error message
        std::string validOptions;
        for (const auto& option : validChoices) {
            if (!validOptions.empty()) validOptions += " or ";
            validOptions += option;
        }
        logMessage(errorMessage + validOptions, logFile);
    }
}

// Function for handling conversion choises
int getUserConversionChoice(std::ofstream& logFile) {
    return getUserChoice(
        "\nConvert a plugin or master file:\n"
        "1. From Bloodmoon to Anthology Bloodmoon\n"
        "2. From Anthology Bloodmoon to Bloodmoon\n"
        "Choice: ",
        { "1", "2" }, logFile
    );
}

// Function for handling input file path from user
std::filesystem::path getInputFilePath(std::ofstream& logFile) {
    std::filesystem::path filePath;
    while (true) {
        std::cout << "\nEnter full path to your .ESP|ESM or just filename (with extension), if your .ESP|ESM is in the same directory\n"
                     "with this program: ";
        std::string input;
        std::getline(std::cin, input);
        filePath = input;

        // Convert the file extension to lowercase for case-insensitive comparison
        std::string extension = filePath.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

        if (std::filesystem::exists(filePath) &&
            (extension == ".esp" || extension == ".esm")) {
            logMessage("Input file found: " + filePath.string(), logFile);
            break;
        }
        logMessage("\nERROR - input file not found: check its directory, name and extension!", logFile);
    }
    return filePath;
}

// Function to check the dependency order of Parent Master files in the input .ESP|ESM data
std::pair<bool, std::unordered_set<int>> checkDependencyOrder(const ordered_json& inputData, std::ofstream& logFile) {
    auto headerIter = std::find_if(inputData.begin(), inputData.end(), [](const ordered_json& item) {
        return item.contains("type") && item["type"] == "Header";
        });

    if (headerIter == inputData.end() || !headerIter->contains("masters")) {
        logMessage("ERROR - missing 'header' section or 'masters' key!", logFile);
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
            logMessage("Valid order of Parent Master files found: M+T+B\n", logFile);
            return { true, {} };
        }
        logMessage("ERROR - invalid order of Parent Master files found: M+B+T\n", logFile);
        return { false, {} };
    }

    if (bPos.has_value() && *bPos > *mwPos) {
        logMessage("Valid order of Parent Master files found: M+B\n", logFile);
        return { true, {} };
    }

    return { false, {} };
}

// Custom hash function for std::pair<int, int>
struct PairHash {
    template <typename T1, typename T2>
    std::size_t operator()(const std::pair<T1, T2>& p) const {
        auto hash1 = std::hash<T1>{}(p.first);
        auto hash2 = std::hash<T2>{}(p.second);
        return hash1 ^ (hash2 << 1);
    }
};

// Function to load custom grid coordinates
void loadCustomGridCoordinates(const std::string& filePath, std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates, std::ofstream& logFile) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        logMessage("ERROR - failed to open custom grid coordinates file: " + filePath, logFile);
        return;
    }

    logMessage("Loading custom grid coordinates from: " + filePath, logFile);

    std::string line;
    while (std::getline(file, line)) {
        // Trim leading and trailing whitespace
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        // Skip empty lines
        if (line.empty()) {
            continue;
        }

        // Skip lines that start with "//" (comments)
        if (line.find("//") == 0) {
            continue;
        }

        std::istringstream lineStream(line);
        int x, y;

        char comma; // To handle the comma separator
        if (lineStream >> x >> comma >> y && comma == ',') {
            customCoordinates.emplace(x, y);
            logMessage("Loaded custom coordinate: (" + std::to_string(x) + ", " + std::to_string(y) + ")", logFile);
        }
        else {
            logMessage("WARNING - invalid format in custom grid coordinates file: " + line, logFile);
        }
    }

    file.close();
}

// Function to check if a given grid coordinate (gridX, gridY) is valid. It checks both the database and custom user-defined coordinates
bool isCoordinateValid(sqlite3* db, int gridX, int gridY, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates, int conversionChoice, std::ofstream& logFile) {
    // Get the offset from the getGridOffset function
    GridOffset offset = getGridOffset(conversionChoice);

    int adjustedGridX = gridX;
    int adjustedGridY = gridY;

    // Apply the offset to the coordinates only for conversionChoice = 2
    if (conversionChoice == 2) {
        adjustedGridX = gridX + offset.offsetX;
        adjustedGridY = gridY + offset.offsetY;
    }

    // Check in the database
    sqlite3_stmt* stmt;
    std::string query = "SELECT BM_Grid_X, BM_Grid_Y FROM [tes3_ab_cell_x-y_data] WHERE BM_Grid_X = ? AND BM_Grid_Y = ?";

    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Error preparing database query: " + std::string(sqlite3_errmsg(db)) << "\n";
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

// Function to process translations for interior door coordinates
void processInterriorDoorsTranslation(sqlite3* db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates, int conversionChoice, std::ofstream& logFile) {

    // Loop through all objects in inputData
    for (auto& cell : inputData) {
        if (cell.contains("type") && cell["type"] == "Cell") {
            if (cell.contains("data") && cell["data"].contains("flags") &&
                cell["data"]["flags"].get<std::string>().find("IS_INTERIOR") != std::string::npos) {

                if (cell.contains("references") && cell["references"].is_array()) {
                    for (auto& reference : cell["references"]) {
                        if (reference.contains("translation") && reference["translation"].is_array() &&
                            reference.contains("destination") && reference["destination"].contains("translation") &&
                            reference["destination"]["translation"].is_array()) {

                            double destX = reference["destination"]["translation"][0].get<double>();
                            double destY = reference["destination"]["translation"][1].get<double>();

                            // Round only the integer part for grid coordinates
                            int gridX = static_cast<int>(std::floor(destX / 8192.0));
                            int gridY = static_cast<int>(std::floor(destY / 8192.0));

                            // Check if coordinate is valid (in DB or customCoordinates)
                            if (isCoordinateValid(db, gridX, gridY, customCoordinates, conversionChoice, logFile)) {
                                int newGridX = gridX + offset.offsetX;
                                int newGridY = gridY + offset.offsetY;

                                logMessage("Found: Interior Door translation -> grid (" + std::to_string(gridX) + ", " + std::to_string(gridY) +
                                           ") | coordinates (" + std::to_string(destX) + ", " + std::to_string(destY) + ")", logFile);

                                // New calculation keeping the fractional part for destination translation
                                double newDestX = (newGridX * 8192.0) + (destX - (gridX * 8192.0));
                                double newDestY = (newGridY * 8192.0) + (destY - (gridY * 8192.0));

                                // Mark the replacement in replacements
                                replacementsFlag = 1;

                                // Save to original fields
                                reference["destination"]["translation"][0] = newDestX;
                                reference["destination"]["translation"][1] = newDestY;

                                logMessage("Calculating: new destination -----> grid (" + std::to_string(newGridX) + ", " + std::to_string(newGridY) +
                                           ") | coordinates (" + std::to_string(newDestX) + ", " + std::to_string(newDestY) + ")", logFile);
                            }
                        }
                    }
                }
            }
        }
    }
}

// Function to process NPC Travel Service coordinates
void processNpcTravelDestinations(sqlite3* db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates, int conversionChoice, std::ofstream& logFile) {

    // Loop through all objects in inputData
    for (auto& npc : inputData) {
        if (npc.contains("type") && npc["type"] == "Npc") {
            if (npc.contains("travel_destinations") && npc["travel_destinations"].is_array()) {
                for (auto& destination : npc["travel_destinations"]) {
                    if (destination.contains("translation") && destination["translation"].is_array()) {
                        double destX = destination["translation"][0].get<double>();
                        double destY = destination["translation"][1].get<double>();

                        // Round only the integer part for grid coordinates
                        int gridX = static_cast<int>(std::floor(destX / 8192.0));
                        int gridY = static_cast<int>(std::floor(destY / 8192.0));

                        // Check if coordinate is valid (in DB or customCoordinates)
                        if (isCoordinateValid(db, gridX, gridY, customCoordinates, conversionChoice, logFile)) {
                            int newGridX = gridX + offset.offsetX;
                            int newGridY = gridY + offset.offsetY;

                            logMessage("Found: NPC 'Travel Service' translation -> grid (" + std::to_string(gridX) + ", " + std::to_string(gridY) +
                                       ") | coordinates (" + std::to_string(destX) + ", " + std::to_string(destY) + ")", logFile);

                            // New calculation keeping the fractional part for destination coordinates
                            double newDestX = (newGridX * 8192.0) + (destX - (gridX * 8192.0));
                            double newDestY = (newGridY * 8192.0) + (destY - (gridY * 8192.0));

                            // Mark the replacement in replacements
                            replacementsFlag = 1;

                            // Save to original fields
                            destination["translation"][0] = newDestX;
                            destination["translation"][1] = newDestY;

                            logMessage("Calculating: new destination ------------> grid (" + std::to_string(newGridX) + ", " + std::to_string(newGridY) +
                                       ") | coordinates (" + std::to_string(newDestX) + ", " + std::to_string(newDestY) + ")", logFile);
                        }
                    }
                }
            }
        }
    }
}

// Function to process Script AI Escort translation
void processScriptAiEscortTranslation(sqlite3* db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, std::vector<std::string>& updatedScriptIDs, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates, int conversionChoice, std::ofstream& logFile) {

    // Regular expression to find AiEscort commands
    std::regex aiEscortRegex(R"((AiEscort)\s*,?\s*((?:\"[^\"]+\")|\S+)\s*,?\s*(\d+)\s*,?\s*(-?\d+(?:\.\d+)?)\s*,?\s*(-?\d+(?:\.\d+)?)\s*,?\s*(-?\d+(?:\.\d+)?)(?:\s*,?\s*(\d+))?)",
        std::regex_constants::icase);

    // Loop through all objects in inputData
    for (auto& script : inputData) {
        if (script.contains("type") && script["type"] == "Script") {
            if (script.contains("text")) {
                std::string scriptText = script["text"].get<std::string>();
                std::string scriptID = script.contains("id") ? script["id"].get<std::string>() : "Unknown";
                std::smatch match;

                // Resulting text after processing
                std::string updatedText;
                std::string::const_iterator searchStart(scriptText.cbegin());
                bool scriptUpdated = false;

                while (std::regex_search(searchStart, scriptText.cend(), match, aiEscortRegex)) {
                    std::string commandType = match[1].str();
                    std::string actorID = match[2].str();
                    std::string duration = match[3].str();
                    double destX = std::stod(match[4].str());
                    double destY = std::stod(match[5].str());
                    double destZ = std::stod(match[6].str());
                    std::string resetValue = (match.size() > 7 && match[7].matched) ? match[7].str() : "";

                    // Round only the integer part for grid coordinates
                    int gridX = static_cast<int>(std::floor(destX / 8192.0));
                    int gridY = static_cast<int>(std::floor(destY / 8192.0));

                    // Check if coordinate is valid (in DB or customCoordinates)
                    if (isCoordinateValid(db, gridX, gridY, customCoordinates, conversionChoice, logFile)) {
                        int newGridX = gridX + offset.offsetX;
                        int newGridY = gridY + offset.offsetY;

                        logMessage("Found: Script 'AI Escort' translation -> grid (" + std::to_string(gridX) + ", " + std::to_string(gridY) +
                                   ") | coordinates (" + std::to_string(destX) + ", " + std::to_string(destY) + ")", logFile);

                        // New calculation keeping the fractional part
                        double newDestX = (newGridX * 8192.0) + (destX - (gridX * 8192.0));
                        double newDestY = (newGridY * 8192.0) + (destY - (gridY * 8192.0));

                        logMessage("Calculating: new destination ----------> grid (" + std::to_string(newGridX) + ", " + std::to_string(newGridY) +
                                   ") | coordinates (" + std::to_string(newDestX) + ", " + std::to_string(newDestY) + ")", logFile);

                        // Mark replacement in replacements
                        replacementsFlag = 1;
                        scriptUpdated = true;

                        // Form the updated command string
                        std::ostringstream formattedCommand;
                        formattedCommand << std::fixed << std::setprecision(3);  // Set precision
                        formattedCommand << commandType << ", " << actorID << ", " << duration << ", "
                                         << newDestX << ", " << newDestY << ", " << destZ;

                        if (!resetValue.empty()) { // Add Reset if it exists
                            formattedCommand << ", " << resetValue;
                        }

                        updatedText += scriptText.substr(searchStart - scriptText.cbegin(), match.position()) +
                            formattedCommand.str();
                    }
                    else {
                        // If no data in the database, leave the command unchanged
                        updatedText += scriptText.substr(searchStart - scriptText.cbegin(), match.position()) +
                            match.str();
                    }

                    searchStart = match.suffix().first;
                }

                // Add the remaining text
                updatedText += std::string(searchStart, scriptText.cend());

                // Save the updated text
                script["text"] = updatedText;

                // If this script was updated, add its ID to the list
                if (scriptUpdated) {
                    updatedScriptIDs.push_back(scriptID);
                }
            }
        }
    }
}

// Function to process Dialogue AI Escort translation
void processDialogueAiEscortTranslation(sqlite3* db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates, int conversionChoice, std::ofstream& logFile) {

    // Regular expression to find AiEscort commands
    std::regex aiEscortRegex(R"((AiEscort)\s*,?\s*((?:\"[^\"]+\")|\S+)\s*,?\s*(\d+)\s*,?\s*(-?\d+(?:\.\d+)?)\s*,?\s*(-?\d+(?:\.\d+)?)\s*,?\s*(-?\d+(?:\.\d+)?)(?:\s*,?\s*(\d+))?)",
        std::regex_constants::icase);

    // Loop through all objects in inputData
    for (auto& dialogueInfo : inputData) {
        if (dialogueInfo.contains("type") && dialogueInfo["type"] == "DialogueInfo") {
            if (dialogueInfo.contains("script_text")) {
                std::string scriptText = dialogueInfo["script_text"].get<std::string>();
                std::smatch match;

                // Resulting text after processing
                std::string updatedText;
                std::string::const_iterator searchStart(scriptText.cbegin());

                while (std::regex_search(searchStart, scriptText.cend(), match, aiEscortRegex)) {
                    std::string commandType = match[1].str();
                    std::string actorID = match[2].str();
                    std::string duration = match[3].str();
                    double destX = std::stod(match[4].str());
                    double destY = std::stod(match[5].str());
                    double destZ = std::stod(match[6].str());
                    std::string resetValue = (match.size() > 7 && match[7].matched) ? match[7].str() : "";

                    // Round only the integer part for grid coordinates
                    int gridX = static_cast<int>(std::floor(destX / 8192.0));
                    int gridY = static_cast<int>(std::floor(destY / 8192.0));

                    // Check if coordinate is valid (in DB or customCoordinates)
                    if (isCoordinateValid(db, gridX, gridY, customCoordinates, conversionChoice, logFile)) {
                        int newGridX = gridX + offset.offsetX;
                        int newGridY = gridY + offset.offsetY;

                        logMessage("Found: Dialogue 'AI Escort' translation -> grid (" + std::to_string(gridX) + ", " + std::to_string(gridY) +
                                   ") | coordinates (" + std::to_string(destX) + ", " + std::to_string(destY) + ")", logFile);

                        // New calculation keeping the fractional part
                        double newDestX = (newGridX * 8192.0) + (destX - (gridX * 8192.0));
                        double newDestY = (newGridY * 8192.0) + (destY - (gridY * 8192.0));

                        logMessage("Calculating: new destination ------------> grid (" + std::to_string(newGridX) + ", " + std::to_string(newGridY) +
                                   ") | coordinates (" + std::to_string(newDestX) + ", " + std::to_string(newDestY) + ")", logFile);

                        // Mark replacement in replacements
                        replacementsFlag = 1;

                        // Form the updated command string
                        std::ostringstream formattedCommand;
                        formattedCommand << std::fixed << std::setprecision(3);  // Set precision
                        formattedCommand << commandType << ", " << actorID << ", " << duration << ", "
                                         << newDestX << ", " << newDestY << ", " << destZ;

                        if (!resetValue.empty()) { // Add Reset if it exists
                            formattedCommand << ", " << resetValue;
                        }

                        updatedText += scriptText.substr(searchStart - scriptText.cbegin(), match.position()) +
                            formattedCommand.str();
                    }
                    else {
                        // If no data in the database, leave the command unchanged
                        updatedText += scriptText.substr(searchStart - scriptText.cbegin(), match.position()) +
                            match.str();
                    }

                    searchStart = match.suffix().first;
                }

                // Add the remaining text
                updatedText += std::string(searchStart, scriptText.cend());

                // Save the updated text
                dialogueInfo["script_text"] = updatedText;
            }
        }
    }
}

// Function to process Script AI Escort Cell translation
void processScriptAiEscortCellTranslation(sqlite3* db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, std::vector<std::string>& updatedScriptIDs, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates, int conversionChoice, std::ofstream& logFile) {

    // Regular expression to find AiEscortCell commands
    std::regex aiEscortCellRegex(R"((AiEscortCell)\s*,?\s*((?:\"[^\"]+\")|\S+)\s*,?\s*((?:\"[^\"]+\")|\S+)\s*,?\s*(\d+)\s*,?\s*(-?\d+(?:\.\d+)?)\s*,?\s*(-?\d+(?:\.\d+)?)\s*,?\s*(-?\d+(?:\.\d+)?)(?:\s*,?\s*(\d+))?)",
        std::regex_constants::icase);

    // Loop through all objects in inputData
    for (auto& script : inputData) {
        if (script.contains("type") && script["type"] == "Script") {
            if (script.contains("text")) {
                std::string scriptText = script["text"].get<std::string>();
                std::string scriptID = script.contains("id") ? script["id"].get<std::string>() : "Unknown";
                std::smatch match;

                // Resulting text after processing
                std::string updatedText;
                std::string::const_iterator searchStart(scriptText.cbegin());
                bool scriptUpdated = false;

                while (std::regex_search(searchStart, scriptText.cend(), match, aiEscortCellRegex)) {
                    std::string commandType = match[1].str();
                    std::string actorID = match[2].str();
                    std::string cellID = match[3].str();
                    std::string duration = match[4].str();
                    double destX = std::stod(match[5].str());
                    double destY = std::stod(match[6].str());
                    double destZ = std::stod(match[7].str());
                    std::string resetValue = (match.size() > 8 && match[8].matched) ? match[8].str() : "";

                    // Round only the integer part for grid coordinates
                    int gridX = static_cast<int>(std::floor(destX / 8192.0));
                    int gridY = static_cast<int>(std::floor(destY / 8192.0));

                    // Check if coordinate is valid (in DB or customCoordinates)
                    if (isCoordinateValid(db, gridX, gridY, customCoordinates, conversionChoice, logFile)) {
                        int newGridX = gridX + offset.offsetX;
                        int newGridY = gridY + offset.offsetY;

                        logMessage("Found: Script 'AI Escort Cell' translation -> grid (" + std::to_string(gridX) + ", " + std::to_string(gridY) +
                                   ") | coordinates (" + std::to_string(destX) + ", " + std::to_string(destY) + ")", logFile);

                        // New calculation keeping the fractional part
                        double newDestX = (newGridX * 8192.0) + (destX - (gridX * 8192.0));
                        double newDestY = (newGridY * 8192.0) + (destY - (gridY * 8192.0));

                        logMessage("Calculating: new destination ---------------> grid (" + std::to_string(newGridX) + ", " + std::to_string(newGridY) +
                                   ") | coordinates (" + std::to_string(newDestX) + ", " + std::to_string(newDestY) + ")", logFile);

                        // Mark replacement in replacements
                        replacementsFlag = 1;
                        scriptUpdated = true;

                        // Form the updated command string
                        std::ostringstream formattedCommand;
                        formattedCommand << std::fixed << std::setprecision(3);  // Set precision
                        formattedCommand << commandType << ", " << actorID << ", " << cellID << ", " << duration << ", "
                                         << newDestX << ", " << newDestY << ", " << destZ;

                        if (!resetValue.empty()) { // Add Reset if it exists
                            formattedCommand << ", " << resetValue;
                        }

                        updatedText += scriptText.substr(searchStart - scriptText.cbegin(), match.position()) +
                            formattedCommand.str();
                    }
                    else {
                        // If no data in the database, leave the command unchanged
                        updatedText += scriptText.substr(searchStart - scriptText.cbegin(), match.position()) +
                            match.str();
                    }

                    searchStart = match.suffix().first;
                }

                // Add the remaining text
                updatedText += std::string(searchStart, scriptText.cend());

                // Save the updated text
                script["text"] = updatedText;

                // If this script was updated, add its ID to the list
                if (scriptUpdated) {
                    updatedScriptIDs.push_back(scriptID);
                }
            }
        }
    }
}

// Function to process Dialogue AI Escort Cell translation
void processDialogueAiEscortCellTranslation(sqlite3* db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates, int conversionChoice, std::ofstream& logFile) {

    // Regular expression to find AiEscortCell commands
    std::regex aiEscortCellRegex(R"((AiEscortCell)\s*,?\s*((?:\"[^\"]+\")|\S+)\s*,?\s*((?:\"[^\"]+\")|\S+)\s*,?\s*(\d+)\s*,?\s*(-?\d+(?:\.\d+)?)\s*,?\s*(-?\d+(?:\.\d+)?)\s*,?\s*(-?\d+(?:\.\d+)?)(?:\s*,?\s*(\d+))?)",
        std::regex_constants::icase);

    // Loop through all objects in inputData
    for (auto& dialogueInfo : inputData) {
        if (dialogueInfo.contains("type") && dialogueInfo["type"] == "DialogueInfo") {
            if (dialogueInfo.contains("script_text")) {
                std::string scriptText = dialogueInfo["script_text"].get<std::string>();
                std::smatch match;

                // Resulting text after processing
                std::string updatedText;
                std::string::const_iterator searchStart(scriptText.cbegin());

                while (std::regex_search(searchStart, scriptText.cend(), match, aiEscortCellRegex)) {
                    std::string commandType = match[1].str();
                    std::string actorID = match[2].str();
                    std::string cellID = match[3].str();
                    std::string duration = match[4].str();
                    double destX = std::stod(match[5].str());
                    double destY = std::stod(match[6].str());
                    double destZ = std::stod(match[7].str());
                    std::string resetValue = (match.size() > 8 && match[8].matched) ? match[8].str() : "";

                    // Round only the integer part for grid coordinates
                    int gridX = static_cast<int>(std::floor(destX / 8192.0));
                    int gridY = static_cast<int>(std::floor(destY / 8192.0));

                    // Check if coordinate is valid (in DB or customCoordinates)
                    if (isCoordinateValid(db, gridX, gridY, customCoordinates, conversionChoice, logFile)) {
                        int newGridX = gridX + offset.offsetX;
                        int newGridY = gridY + offset.offsetY;

                        logMessage("Found: Dialogue 'AI Escort Cell' translation -> grid (" + std::to_string(gridX) + ", " + std::to_string(gridY) +
                                   ") | coordinates (" + std::to_string(destX) + ", " + std::to_string(destY) + ")", logFile);

                        // New calculation keeping the fractional part
                        double newDestX = (newGridX * 8192.0) + (destX - (gridX * 8192.0));
                        double newDestY = (newGridY * 8192.0) + (destY - (gridY * 8192.0));

                        logMessage("Calculating: new destination -----------------> grid (" + std::to_string(newGridX) + ", " + std::to_string(newGridY) +
                                   ") | coordinates (" + std::to_string(newDestX) + ", " + std::to_string(newDestY) + ")", logFile);

                        // Mark replacement in replacements
                        replacementsFlag = 1;

                        // Form the updated command string
                        std::ostringstream formattedCommand;
                        formattedCommand << std::fixed << std::setprecision(3);  // Set precision
                        formattedCommand << commandType << ", " << actorID << ", " << cellID << ", " << duration << ", "
                                         << newDestX << ", " << newDestY << ", " << destZ;

                        if (!resetValue.empty()) { // Add Reset if it exists
                            formattedCommand << ", " << resetValue;
                        }

                        updatedText += scriptText.substr(searchStart - scriptText.cbegin(), match.position()) +
                            formattedCommand.str();
                    }
                    else {
                        // If no data in the database, leave the command unchanged
                        updatedText += scriptText.substr(searchStart - scriptText.cbegin(), match.position()) +
                            match.str();
                    }

                    searchStart = match.suffix().first;
                }

                // Add the remaining text
                updatedText += std::string(searchStart, scriptText.cend());

                // Save the updated text
                dialogueInfo["script_text"] = updatedText;
            }
        }
    }
}

// Function to process Script AI Follow translation
void processScriptAiFollowTranslation(sqlite3* db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, std::vector<std::string>& updatedScriptIDs, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates, int conversionChoice, std::ofstream& logFile) {

    // Regular expression to find AiFollow commands
    std::regex aiFollowRegex(R"((AiFollow)\s*,?\s*((?:\"[^\"]+\")|\S+)\s*,?\s*(\d+)\s*,?\s*(-?\d+(?:\.\d+)?)\s*,?\s*(-?\d+(?:\.\d+)?)\s*,?\s*(-?\d+(?:\.\d+)?)(?:\s*,?\s*(\d+))?)",
        std::regex_constants::icase);

    // Loop through all objects in inputData
    for (auto& script : inputData) {
        if (script.contains("type") && script["type"] == "Script") {
            if (script.contains("text")) {
                std::string scriptText = script["text"].get<std::string>();
                std::string scriptID = script.contains("id") ? script["id"].get<std::string>() : "Unknown";
                std::smatch match;

                // Resulting text after processing
                std::string updatedText;
                std::string::const_iterator searchStart(scriptText.cbegin());
                bool scriptUpdated = false;

                while (std::regex_search(searchStart, scriptText.cend(), match, aiFollowRegex)) {
                    std::string commandType = match[1].str();
                    std::string actorID = match[2].str();
                    std::string duration = match[3].str();
                    double destX = std::stod(match[4].str());
                    double destY = std::stod(match[5].str());
                    double destZ = std::stod(match[6].str());
                    std::string resetValue = (match.size() > 7 && match[7].matched) ? match[7].str() : "";

                    // Round only the integer part for grid coordinates
                    int gridX = static_cast<int>(std::floor(destX / 8192.0));
                    int gridY = static_cast<int>(std::floor(destY / 8192.0));

                    // Check if coordinate is valid (in DB or customCoordinates)
                    if (isCoordinateValid(db, gridX, gridY, customCoordinates, conversionChoice, logFile)) {
                        int newGridX = gridX + offset.offsetX;
                        int newGridY = gridY + offset.offsetY;

                        logMessage("Found: Script 'AI Follow' translation -> grid (" + std::to_string(gridX) + ", " + std::to_string(gridY) +
                                   ") | coordinates (" + std::to_string(destX) + ", " + std::to_string(destY) + ")", logFile);

                        // New calculation keeping the fractional part
                        double newDestX = (newGridX * 8192.0) + (destX - (gridX * 8192.0));
                        double newDestY = (newGridY * 8192.0) + (destY - (gridY * 8192.0));

                        logMessage("Calculating: new destination ----------> grid (" + std::to_string(newGridX) + ", " + std::to_string(newGridY) +
                                   ") | coordinates (" + std::to_string(newDestX) + ", " + std::to_string(newDestY) + ")", logFile);

                        // Mark replacement in replacements
                        replacementsFlag = 1;
                        scriptUpdated = true;

                        // Form the updated command string
                        std::ostringstream formattedCommand;
                        formattedCommand << std::fixed << std::setprecision(3);  // Set precision
                        formattedCommand << commandType << ", " << actorID << ", " << duration << ", "
                                         << newDestX << ", " << newDestY << ", " << destZ;

                        if (!resetValue.empty()) { // Add Reset if it exists
                            formattedCommand << ", " << resetValue;
                        }

                        updatedText += scriptText.substr(searchStart - scriptText.cbegin(), match.position()) +
                            formattedCommand.str();
                    }
                    else {
                        // If no data in the database, leave the command unchanged
                        updatedText += scriptText.substr(searchStart - scriptText.cbegin(), match.position()) +
                            match.str();
                    }

                    searchStart = match.suffix().first;
                }

                // Add the remaining text
                updatedText += std::string(searchStart, scriptText.cend());

                // Save the updated text
                script["text"] = updatedText;

                // If this script was updated, add its ID to the list
                if (scriptUpdated) {
                    updatedScriptIDs.push_back(scriptID);
                }
            }
        }
    }
}

// Function to process Dialogue AI Follow translation
void processDialogueAiFollowTranslation(sqlite3* db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates, int conversionChoice, std::ofstream& logFile) {

    // Regular expression to find AiFollow commands
    std::regex aiFollowRegex(R"((AiFollow)\s*,?\s*((?:\"[^\"]+\")|\S+)\s*,?\s*(\d+)\s*,?\s*(-?\d+(?:\.\d+)?)\s*,?\s*(-?\d+(?:\.\d+)?)\s*,?\s*(-?\d+(?:\.\d+)?)(?:\s*,?\s*(\d+))?)",
        std::regex_constants::icase);

    // Loop through all objects in inputData
    for (auto& dialogueInfo : inputData) {
        if (dialogueInfo.contains("type") && dialogueInfo["type"] == "DialogueInfo") {
            if (dialogueInfo.contains("script_text")) {
                std::string scriptText = dialogueInfo["script_text"].get<std::string>();
                std::smatch match;

                // Resulting text after processing
                std::string updatedText;
                std::string::const_iterator searchStart(scriptText.cbegin());

                while (std::regex_search(searchStart, scriptText.cend(), match, aiFollowRegex)) {
                    std::string commandType = match[1].str();
                    std::string actorID = match[2].str();
                    std::string duration = match[3].str();
                    double destX = std::stod(match[4].str());
                    double destY = std::stod(match[5].str());
                    double destZ = std::stod(match[6].str());
                    std::string resetValue = (match.size() > 7 && match[7].matched) ? match[7].str() : "";

                    // Round only the integer part for grid coordinates
                    int gridX = static_cast<int>(std::floor(destX / 8192.0));
                    int gridY = static_cast<int>(std::floor(destY / 8192.0));

                    // Check if coordinate is valid (in DB or customCoordinates)
                    if (isCoordinateValid(db, gridX, gridY, customCoordinates, conversionChoice, logFile)) {
                        int newGridX = gridX + offset.offsetX;
                        int newGridY = gridY + offset.offsetY;

                        logMessage("Found: Dialogue 'AI Follow' translation -> grid (" + std::to_string(gridX) + ", " + std::to_string(gridY) +
                                   ") | coordinates (" + std::to_string(destX) + ", " + std::to_string(destY) + ")", logFile);

                        // New calculation keeping the fractional part
                        double newDestX = (newGridX * 8192.0) + (destX - (gridX * 8192.0));
                        double newDestY = (newGridY * 8192.0) + (destY - (gridY * 8192.0));

                        logMessage("Calculating: new destination ------------> grid (" + std::to_string(newGridX) + ", " + std::to_string(newGridY) +
                                   ") | coordinates (" + std::to_string(newDestX) + ", " + std::to_string(newDestY) + ")", logFile);

                        // Mark replacement in replacements
                        replacementsFlag = 1;

                        // Form the updated command string
                        std::ostringstream formattedCommand;
                        formattedCommand << std::fixed << std::setprecision(3);  // Set precision
                        formattedCommand << commandType << ", " << actorID << ", " << duration << ", "
                                         << newDestX << ", " << newDestY << ", " << destZ;

                        if (!resetValue.empty()) { // Add Reset if it exists
                            formattedCommand << ", " << resetValue;
                        }

                        updatedText += scriptText.substr(searchStart - scriptText.cbegin(), match.position()) +
                            formattedCommand.str();
                    }
                    else {
                        // If no data in the database, leave the command unchanged
                        updatedText += scriptText.substr(searchStart - scriptText.cbegin(), match.position()) +
                            match.str();
                    }

                    searchStart = match.suffix().first;
                }

                // Add the remaining text
                updatedText += std::string(searchStart, scriptText.cend());

                // Save the updated text
                dialogueInfo["script_text"] = updatedText;
            }
        }
    }
}

// Function to process Script AI Follow Cell translation
void processScriptAiFollowCellTranslation(sqlite3* db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, std::vector<std::string>& updatedScriptIDs, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates, int conversionChoice, std::ofstream& logFile) {

    // Regular expression to find AiFollow commands
    std::regex aiFollowCellRegex(R"((AIFollowCell)\s*,?\s*((?:\"[^\"]+\")|\S+)\s*,?\s*((?:\"[^\"]+\")|\S+)\s*,?\s*(\d+)\s*,?\s*(-?\d+(?:\.\d+)?)\s*,?\s*(-?\d+(?:\.\d+)?)\s*,?\s*(-?\d+(?:\.\d+)?)(?:\s*,?\s*(\d+))?)",
        std::regex_constants::icase);

    for (auto& script : inputData) {
        if (script.contains("type") && script["type"] == "Script") {
            if (script.contains("text")) {
                std::string scriptText = script["text"].get<std::string>();
                std::string scriptID = script.contains("id") ? script["id"].get<std::string>() : "Unknown";
                std::smatch match;

                // Resulting text after processing
                std::string updatedText;
                std::string::const_iterator searchStart(scriptText.cbegin());
                bool scriptUpdated = false;

                while (std::regex_search(searchStart, scriptText.cend(), match, aiFollowCellRegex)) {
                    std::string commandType = match[1].str();
                    std::string actorID = match[2].str();
                    std::string cellID = match[3].str();
                    std::string duration = match[4].str();
                    double destX = std::stod(match[5].str());
                    double destY = std::stod(match[6].str());
                    double destZ = std::stod(match[7].str());
                    std::string resetValue = (match.size() > 8 && match[8].matched) ? match[8].str() : "";

                    // Round only the integer part for grid coordinates
                    int gridX = static_cast<int>(std::floor(destX / 8192.0));
                    int gridY = static_cast<int>(std::floor(destY / 8192.0));

                    // Check if coordinate is valid (in DB or customCoordinates)
                    if (isCoordinateValid(db, gridX, gridY, customCoordinates, conversionChoice, logFile)) {
                        int newGridX = gridX + offset.offsetX;
                        int newGridY = gridY + offset.offsetY;

                        logMessage("Found: Script 'AI Follow Cell' translation -> grid (" + std::to_string(gridX) + ", " + std::to_string(gridY) +
                                   ") | coordinates (" + std::to_string(destX) + ", " + std::to_string(destY) + ")", logFile);

                        // New calculation keeping the fractional part
                        double newDestX = (newGridX * 8192.0) + (destX - (gridX * 8192.0));
                        double newDestY = (newGridY * 8192.0) + (destY - (gridY * 8192.0));

                        logMessage("Calculating: new destination ---------------> grid (" + std::to_string(newGridX) + ", " + std::to_string(newGridY) +
                                   ") | coordinates (" + std::to_string(newDestX) + ", " + std::to_string(newDestY) + ")", logFile);

                        // Mark replacement in replacements
                        replacementsFlag = 1;
                        scriptUpdated = true;

                        // Form the updated command string
                        std::ostringstream formattedCommand;
                        formattedCommand << std::fixed << std::setprecision(3);  // Set precision
                        formattedCommand << commandType << ", " << actorID << ", " << cellID << ", " << duration << ", "
                                         << newDestX << ", " << newDestY << ", " << destZ;

                        if (!resetValue.empty()) { // Add Reset if it exists
                            formattedCommand << ", " << resetValue;
                        }

                        updatedText += scriptText.substr(searchStart - scriptText.cbegin(), match.position()) +
                            formattedCommand.str();
                    }
                    else {
                        // If no data in the database, leave the command unchanged
                        updatedText += scriptText.substr(searchStart - scriptText.cbegin(), match.position()) +
                            match.str();
                    }

                    searchStart = match.suffix().first;
                }

                // Add the remaining text
                updatedText += std::string(searchStart, scriptText.cend());

                // Save the updated text
                script["text"] = updatedText;

                // If this script was updated, add its ID to the list
                if (scriptUpdated) {
                    updatedScriptIDs.push_back(scriptID);
                }
            }
        }
    }
}

// Function to process Dialogue AI Follow Cell translation
void processDialogueAiFollowCellTranslation(sqlite3* db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates, int conversionChoice, std::ofstream& logFile) {

    // Regular expression to find AiFollow commands
    std::regex aiFollowCellRegex(R"((AIFollowCell)\s*,?\s*((?:\"[^\"]+\")|\S+)\s*,?\s*((?:\"[^\"]+\")|\S+)\s*,?\s*(\d+)\s*,?\s*(-?\d+(?:\.\d+)?)\s*,?\s*(-?\d+(?:\.\d+)?)\s*,?\s*(-?\d+(?:\.\d+)?)(?:\s*,?\s*(\d+))?)",
        std::regex_constants::icase);

    // Loop through all objects in inputData
    for (auto& dialogueInfo : inputData) {
        if (dialogueInfo.contains("type") && dialogueInfo["type"] == "DialogueInfo") {
            if (dialogueInfo.contains("script_text")) {
                std::string scriptText = dialogueInfo["script_text"].get<std::string>();
                std::smatch match;

                // Resulting text after processing
                std::string updatedText;
                std::string::const_iterator searchStart(scriptText.cbegin());

                while (std::regex_search(searchStart, scriptText.cend(), match, aiFollowCellRegex)) {
                    std::string commandType = match[1].str();
                    std::string actorID = match[2].str();
                    std::string cellID = match[3].str();
                    std::string duration = match[4].str();
                    double destX = std::stod(match[5].str());
                    double destY = std::stod(match[6].str());
                    double destZ = std::stod(match[7].str());
                    std::string resetValue = (match.size() > 8 && match[8].matched) ? match[8].str() : "";

                    // Round only the integer part for grid coordinates
                    int gridX = static_cast<int>(std::floor(destX / 8192.0));
                    int gridY = static_cast<int>(std::floor(destY / 8192.0));

                    // Check if coordinate is valid (in DB or customCoordinates)
                    if (isCoordinateValid(db, gridX, gridY, customCoordinates, conversionChoice, logFile)) {
                        int newGridX = gridX + offset.offsetX;
                        int newGridY = gridY + offset.offsetY;

                        logMessage("Found: Dialogue 'AI Follow Cell' translation -> grid (" + std::to_string(gridX) + ", " + std::to_string(gridY) +
                                   ") | coordinates (" + std::to_string(destX) + ", " + std::to_string(destY) + ")", logFile);

                        // New calculation keeping the fractional part
                        double newDestX = (newGridX * 8192.0) + (destX - (gridX * 8192.0));
                        double newDestY = (newGridY * 8192.0) + (destY - (gridY * 8192.0));

                        logMessage("Calculating: new destination -----------------> grid (" + std::to_string(newGridX) + ", " + std::to_string(newGridY) +
                                   ") | coordinates (" + std::to_string(newDestX) + ", " + std::to_string(newDestY) + ")", logFile);

                        // Mark replacement in replacements
                        replacementsFlag = 1;

                        // Form the updated command string
                        std::ostringstream formattedCommand;
                        formattedCommand << std::fixed << std::setprecision(3);  // Set precision
                        formattedCommand << commandType << ", " << actorID << ", " << cellID << ", " << duration << ", "
                                         << newDestX << ", " << newDestY << ", " << destZ;

                        if (!resetValue.empty()) { // Add Reset if it exists
                            formattedCommand << ", " << resetValue;
                        }

                        updatedText += scriptText.substr(searchStart - scriptText.cbegin(), match.position()) +
                            formattedCommand.str();
                    }
                    else {
                        // If no data in the database, leave the command unchanged
                        updatedText += scriptText.substr(searchStart - scriptText.cbegin(), match.position()) +
                            match.str();
                    }

                    searchStart = match.suffix().first;
                }

                // Add the remaining text
                updatedText += std::string(searchStart, scriptText.cend());

                // Save the updated text
                dialogueInfo["script_text"] = updatedText;
            }
        }
    }
}

// Function to process Script AI Travel translation
void processScriptAiTravelTranslation(sqlite3* db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, std::vector<std::string>& updatedScriptIDs, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates, int conversionChoice, std::ofstream& logFile) {

    // Regular expression to find AiTravel commands
    std::regex aiTravelRegex(R"((AiTravel)\s*,?\s*(-?\d+(?:\.\d+)?)\s*,?\s*(-?\d+(?:\.\d+)?)\s*,?\s*(-?\d+(?:\.\d+)?)(?:\s*,?\s*(\d+))?)",
        std::regex_constants::icase);

    // Loop through all objects in inputData
    for (auto& script : inputData) {
        if (script.contains("type") && script["type"] == "Script") {
            if (script.contains("text")) {
                std::string scriptText = script["text"].get<std::string>();
                std::string scriptID = script.contains("id") ? script["id"].get<std::string>() : "Unknown";
                std::smatch match;

                // Resulting text after processing
                std::string updatedText;
                std::string::const_iterator searchStart(scriptText.cbegin());
                bool scriptUpdated = false;

                while (std::regex_search(searchStart, scriptText.cend(), match, aiTravelRegex)) {
                    std::string commandType = match[1].str();
                    double destX = std::stod(match[2].str());
                    double destY = std::stod(match[3].str());
                    double destZ = std::stod(match[4].str());
                    std::string resetValue = (match.size() > 5 && match[5].matched) ? match[5].str() : "";

                    // Round only the integer part for grid coordinates
                    int gridX = static_cast<int>(std::floor(destX / 8192.0));
                    int gridY = static_cast<int>(std::floor(destY / 8192.0));

                    // Check if coordinate is valid (in DB or customCoordinates)
                    if (isCoordinateValid(db, gridX, gridY, customCoordinates, conversionChoice, logFile)) {
                        int newGridX = gridX + offset.offsetX;
                        int newGridY = gridY + offset.offsetY;

                        logMessage("Found: Script 'AI Travel' translation -> grid (" + std::to_string(gridX) + ", " + std::to_string(gridY) +
                                   ") | coordinates (" + std::to_string(destX) + ", " + std::to_string(destY) + ")", logFile);

                        // New calculation keeping the fractional part
                        double newDestX = (newGridX * 8192.0) + (destX - (gridX * 8192.0));
                        double newDestY = (newGridY * 8192.0) + (destY - (gridY * 8192.0));

                        logMessage("Calculating: new destination ----------> grid (" + std::to_string(newGridX) + ", " + std::to_string(newGridY) +
                                   ") | coordinates (" + std::to_string(newDestX) + ", " + std::to_string(newDestY) + ")", logFile);

                        // Mark replacement in replacements
                        replacementsFlag = 1;
                        scriptUpdated = true;

                        // Form the updated command string
                        std::ostringstream formattedCommand;
                        formattedCommand << std::fixed << std::setprecision(3);  // Set precision
                        formattedCommand << commandType << ", " << newDestX << ", " << newDestY << ", " << destZ;

                        if (!resetValue.empty()) { // Add Reset if it exists
                            formattedCommand << ", " << resetValue;
                        }

                        updatedText += scriptText.substr(searchStart - scriptText.cbegin(), match.position()) +
                            formattedCommand.str();
                    }
                    else {
                        // If no data in the database, leave the command unchanged
                        updatedText += scriptText.substr(searchStart - scriptText.cbegin(), match.position()) +
                            match.str();
                    }

                    searchStart = match.suffix().first;
                }

                // Add the remaining text
                updatedText += std::string(searchStart, scriptText.cend());

                // Save the updated text
                script["text"] = updatedText;

                // If this script was updated, add its ID to the list
                if (scriptUpdated) {
                    updatedScriptIDs.push_back(scriptID);
                }
            }
        }
    }
}

// Function to process Dialogue AI Travel translation
void processDialogueAiTravelTranslation(sqlite3* db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates, int conversionChoice, std::ofstream& logFile) {

    // Regular expression to find AiTravel commands
    std::regex aiTravelRegex(R"((AiTravel)\s*,?\s*(-?\d+(?:\.\d+)?)\s*,?\s*(-?\d+(?:\.\d+)?)\s*,?\s*(-?\d+(?:\.\d+)?)(?:\s*,?\s*(\d+))?)",
        std::regex_constants::icase);

    // Loop through all objects in inputData
    for (auto& dialogueInfo : inputData) {
        if (dialogueInfo.contains("type") && dialogueInfo["type"] == "DialogueInfo") {
            if (dialogueInfo.contains("script_text")) {
                std::string scriptText = dialogueInfo["script_text"].get<std::string>();
                std::smatch match;

                // Resulting text after processing
                std::string updatedText;
                std::string::const_iterator searchStart(scriptText.cbegin());

                while (std::regex_search(searchStart, scriptText.cend(), match, aiTravelRegex)) {
                    std::string commandType = match[1].str();
                    double destX = std::stod(match[2].str());
                    double destY = std::stod(match[3].str());
                    double destZ = std::stod(match[4].str());
                    std::string resetValue = (match.size() > 5 && match[5].matched) ? match[5].str() : "";

                    // Round only the integer part for grid coordinates
                    int gridX = static_cast<int>(std::floor(destX / 8192.0));
                    int gridY = static_cast<int>(std::floor(destY / 8192.0));

                    // Check if coordinate is valid (in DB or customCoordinates)
                    if (isCoordinateValid(db, gridX, gridY, customCoordinates, conversionChoice, logFile)) {
                        int newGridX = gridX + offset.offsetX;
                        int newGridY = gridY + offset.offsetY;

                        logMessage("Found: Dialogue 'AI Travel' translation -> grid (" + std::to_string(gridX) + ", " + std::to_string(gridY) +
                                   ") | coordinates (" + std::to_string(destX) + ", " + std::to_string(destY) + ")", logFile);

                        // New calculation keeping the fractional part
                        double newDestX = (newGridX * 8192.0) + (destX - (gridX * 8192.0));
                        double newDestY = (newGridY * 8192.0) + (destY - (gridY * 8192.0));

                        logMessage("Calculating: new destination ------------> grid (" + std::to_string(newGridX) + ", " + std::to_string(newGridY) +
                                   ") | coordinates (" + std::to_string(newDestX) + ", " + std::to_string(newDestY) + ")", logFile);

                        // Mark replacement in replacements
                        replacementsFlag = 1;

                        // Form the updated command string
                        std::ostringstream formattedCommand;
                        formattedCommand << std::fixed << std::setprecision(3);
                        formattedCommand << commandType << ", " << newDestX << ", " << newDestY << ", " << destZ;

                        if (!resetValue.empty()) {
                            formattedCommand << ", " << resetValue;
                        }

                        updatedText += scriptText.substr(searchStart - scriptText.cbegin(), match.position()) +
                            formattedCommand.str();
                    }
                    else {
                        updatedText += scriptText.substr(searchStart - scriptText.cbegin(), match.position()) +
                            match.str();
                    }

                    searchStart = match.suffix().first;
                }

                updatedText += std::string(searchStart, scriptText.cend());
                dialogueInfo["script_text"] = updatedText;
            }
        }
    }
}

// Function to process Script Position translation
void processScriptPositionTranslation(sqlite3* db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, std::vector<std::string>& updatedScriptIDs, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates, int conversionChoice, std::ofstream& logFile) {

    // Regular expression to find Position commands
    std::regex positionRegex(R"((Position)\s*,?\s*(-?\d+(?:\.\d+)?)\s*,?\s*(-?\d+(?:\.\d+)?)\s*,?\s*(-?\d+(?:\.\d+)?)\s*,?\s*(-?\d+(?:\.\d+)?))",
        std::regex_constants::icase);

    // Loop through all objects in inputData
    for (auto& script : inputData) {
        if (script.contains("type") && script["type"] == "Script") {
            if (script.contains("text")) {
                std::string scriptText = script["text"].get<std::string>();
                std::string scriptID = script.contains("id") ? script["id"].get<std::string>() : "Unknown";
                std::smatch match;

                // Resulting text after processing
                std::string updatedText;
                std::string::const_iterator searchStart(scriptText.cbegin());
                bool scriptUpdated = false;

                while (std::regex_search(searchStart, scriptText.cend(), match, positionRegex)) {
                    std::string commandType = match[1].str();
                    double destX = std::stod(match[2].str());
                    double destY = std::stod(match[3].str());
                    double destZ = std::stod(match[4].str());
                    double zRot = std::stod(match[5].str());

                    // Round only the integer part for grid coordinates
                    int gridX = static_cast<int>(std::floor(destX / 8192.0));
                    int gridY = static_cast<int>(std::floor(destY / 8192.0));

                    // Check if coordinate is valid (in DB or customCoordinates)
                    if (isCoordinateValid(db, gridX, gridY, customCoordinates, conversionChoice, logFile)) {
                        int newGridX = gridX + offset.offsetX;
                        int newGridY = gridY + offset.offsetY;

                        logMessage("Found: Script 'Position' translation -> grid (" + std::to_string(gridX) + ", " + std::to_string(gridY) +
                                   ") | coordinates (" + std::to_string(destX) + ", " + std::to_string(destY) + ")", logFile);

                        // New calculation keeping the fractional part
                        double newDestX = (newGridX * 8192.0) + (destX - (gridX * 8192.0));
                        double newDestY = (newGridY * 8192.0) + (destY - (gridY * 8192.0));

                        logMessage("Calculating: new destination ---------> grid (" + std::to_string(newGridX) + ", " + std::to_string(newGridY) +
                                   ") | coordinates (" + std::to_string(newDestX) + ", " + std::to_string(newDestY) + ")", logFile);

                        // Mark replacement in replacements
                        replacementsFlag = 1;
                        scriptUpdated = true;

                        // Form the updated command string
                        std::ostringstream formattedCommand;
                        formattedCommand << std::fixed << std::setprecision(3);
                        formattedCommand << commandType << ", " << newDestX << ", " << newDestY << ", " << destZ;
                        formattedCommand << ", " << std::fixed << std::setprecision(0) << zRot;

                        updatedText += scriptText.substr(searchStart - scriptText.cbegin(), match.position()) +
                            formattedCommand.str();
                    }
                    else {
                        // If no data found in the database, leave the command unchanged
                        updatedText += scriptText.substr(searchStart - scriptText.cbegin(), match.position()) +
                            match.str();
                    }

                    searchStart = match.suffix().first;
                }

                // Add remaining text
                updatedText += std::string(searchStart, scriptText.cend());

                // Save the updated text
                script["text"] = updatedText;

                // If this script was updated, add its ID to the list
                if (scriptUpdated) {
                    updatedScriptIDs.push_back(scriptID);
                }
            }
        }
    }
}

// Function to process Dialogue Position translation
void processDialoguePositionTranslation(sqlite3* db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates, int conversionChoice, std::ofstream& logFile) {

    // Regular expression to find Position commands
    std::regex positionRegex(R"((Position)\s*,?\s*(-?\d+(?:\.\d+)?)\s*,?\s*(-?\d+(?:\.\d+)?)\s*,?\s*(-?\d+(?:\.\d+)?)\s*,?\s*(-?\d+(?:\.\d+)?))",
        std::regex_constants::icase);

    // Loop through all objects in inputData
    for (auto& dialogueInfo : inputData) {
        if (dialogueInfo.contains("type") && dialogueInfo["type"] == "DialogueInfo") {
            if (dialogueInfo.contains("script_text")) {
                std::string scriptText = dialogueInfo["script_text"].get<std::string>();
                std::smatch match;

                // Resulting text after processing
                std::string updatedText;
                std::string::const_iterator searchStart(scriptText.cbegin());

                while (std::regex_search(searchStart, scriptText.cend(), match, positionRegex)) {
                    std::string commandType = match[1].str();
                    double destX = std::stod(match[2].str());
                    double destY = std::stod(match[3].str());
                    double destZ = std::stod(match[4].str());
                    double zRot = std::stod(match[5].str());

                    // Round only the integer part for grid coordinates
                    int gridX = static_cast<int>(std::floor(destX / 8192.0));
                    int gridY = static_cast<int>(std::floor(destY / 8192.0));

                    // Check if coordinate is valid (in DB or customCoordinates)
                    if (isCoordinateValid(db, gridX, gridY, customCoordinates, conversionChoice, logFile)) {
                        int newGridX = gridX + offset.offsetX;
                        int newGridY = gridY + offset.offsetY;

                        logMessage("Found: Dialogue 'Position' translation -> grid (" + std::to_string(gridX) + ", " + std::to_string(gridY) +
                                   ") | coordinates (" + std::to_string(destX) + ", " + std::to_string(destY) + ")", logFile);

                        // New calculation keeping the fractional part
                        double newDestX = (newGridX * 8192.0) + (destX - (gridX * 8192.0));
                        double newDestY = (newGridY * 8192.0) + (destY - (gridY * 8192.0));

                        logMessage("Calculating: new destination -----------> grid (" + std::to_string(newGridX) + ", " + std::to_string(newGridY) +
                                   ") | coordinates (" + std::to_string(newDestX) + ", " + std::to_string(newDestY) + ")", logFile);

                        // Mark replacement in replacements
                        replacementsFlag = 1;

                        // Form the updated command string
                        std::ostringstream formattedCommand;
                        formattedCommand << std::fixed << std::setprecision(3);
                        formattedCommand << commandType << ", " << newDestX << ", " << newDestY << ", " << destZ;
                        formattedCommand << ", " << std::fixed << std::setprecision(0) << zRot;

                        updatedText += scriptText.substr(searchStart - scriptText.cbegin(), match.position()) +
                            formattedCommand.str();
                    }
                    else {
                        updatedText += scriptText.substr(searchStart - scriptText.cbegin(), match.position()) +
                            match.str();
                    }

                    searchStart = match.suffix().first;
                }

                updatedText += std::string(searchStart, scriptText.cend());
                dialogueInfo["script_text"] = updatedText;
            }
        }
    }
}

// Function to process Script PositionCell translation
void processScriptPositionCellTranslation(sqlite3* db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, std::vector<std::string>& updatedScriptIDs, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates, int conversionChoice, std::ofstream& logFile) {

    // Regular expression to find PositionCell commands
    std::regex positionCellRegex(R"((PositionCell)\s*,?\s*(-?\d+(?:\.\d+)?)\s*,?\s*(-?\d+(?:\.\d+)?)\s*,?\s*(-?\d+(?:\.\d+)?)\s*,?\s*(-?\d+(?:\.\d+)?)\s*,?\s*((?:\"[^\"]+\")|\S+))",
        std::regex_constants::icase);

    // Loop through all objects in inputData
    for (auto& script : inputData) {
        if (script.contains("type") && script["type"] == "Script") {
            if (script.contains("text")) {
                std::string scriptText = script["text"].get<std::string>();
                std::string scriptID = script.contains("id") ? script["id"].get<std::string>() : "Unknown";
                std::smatch match;

                // Resulting text after processing
                std::string updatedText;
                std::string::const_iterator searchStart(scriptText.cbegin());
                bool scriptUpdated = false;

                while (std::regex_search(searchStart, scriptText.cend(), match, positionCellRegex)) {
                    std::string commandType = match[1].str();
                    double destX = std::stod(match[2].str());
                    double destY = std::stod(match[3].str());
                    double destZ = std::stod(match[4].str());
                    double zRot = std::stod(match[5].str());
                    std::string cellID = match[6].str();  // Cell name (with escaped quotes)

                    // Round only the integer part for grid coordinates
                    int gridX = static_cast<int>(std::floor(destX / 8192.0));
                    int gridY = static_cast<int>(std::floor(destY / 8192.0));

                    // Check if coordinate is valid (in DB or customCoordinates)
                    if (isCoordinateValid(db, gridX, gridY, customCoordinates, conversionChoice, logFile)) {
                        int newGridX = gridX + offset.offsetX;
                        int newGridY = gridY + offset.offsetY;

                        logMessage("Found: Script 'Position Cell' translation -> grid (" + std::to_string(gridX) + ", " + std::to_string(gridY) +
                                   ") | coordinates (" + std::to_string(destX) + ", " + std::to_string(destY) + ")", logFile);

                        // New calculation keeping the fractional part
                        double newDestX = (newGridX * 8192.0) + (destX - (gridX * 8192.0));
                        double newDestY = (newGridY * 8192.0) + (destY - (gridY * 8192.0));

                        logMessage("Calculating: new destination --------------> grid (" + std::to_string(newGridX) + ", " + std::to_string(newGridY) +
                                   ") | coordinates (" + std::to_string(newDestX) + ", " + std::to_string(newDestY) + ")", logFile);

                        // Mark replacement in replacements
                        replacementsFlag = 1;
                        scriptUpdated = true;

                        // Form the updated command string
                        std::ostringstream formattedCommand;
                        formattedCommand << std::fixed << std::setprecision(3);
                        formattedCommand << commandType << ", " << newDestX << ", " << newDestY << ", " << destZ;
                        formattedCommand << ", " << std::fixed << std::setprecision(0) << zRot;
                        formattedCommand << ", " << cellID;

                        updatedText += scriptText.substr(searchStart - scriptText.cbegin(), match.position()) +
                            formattedCommand.str();
                    }
                    else {
                        // If no data is found in the database, leave the command unchanged
                        updatedText += scriptText.substr(searchStart - scriptText.cbegin(), match.position()) +
                            match.str();
                    }

                    searchStart = match.suffix().first;
                }

                // Add the remaining text
                updatedText += std::string(searchStart, scriptText.cend());

                // Save the updated text
                script["text"] = updatedText;

                // If this script was updated, add its ID to the list
                if (scriptUpdated) {
                    updatedScriptIDs.push_back(scriptID);
                }
            }
        }
    }
}

// Function to process Dialogue PositionCell translation
void processDialoguePositionCellTranslation(sqlite3* db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates, int conversionChoice, std::ofstream& logFile) {

    // Regular expression to find PositionCell commands
    std::regex positionCellRegex(R"((PositionCell)\s*,?\s*(-?\d+(?:\.\d+)?)\s*,?\s*(-?\d+(?:\.\d+)?)\s*,?\s*(-?\d+(?:\.\d+)?)\s*,?\s*(-?\d+(?:\.\d+)?)\s*,?\s*((?:\"[^\"]+\")|\S+))",
        std::regex_constants::icase);

    // Loop through all objects in inputData
    for (auto& dialogueInfo : inputData) {
        if (dialogueInfo.contains("type") && dialogueInfo["type"] == "DialogueInfo") {
            if (dialogueInfo.contains("script_text")) {
                std::string scriptText = dialogueInfo["script_text"].get<std::string>();
                std::smatch match;

                // Resulting text after processing
                std::string updatedText;
                std::string::const_iterator searchStart(scriptText.cbegin());

                while (std::regex_search(searchStart, scriptText.cend(), match, positionCellRegex)) {
                    std::string commandType = match[1].str();
                    double destX = std::stod(match[2].str());
                    double destY = std::stod(match[3].str());
                    double destZ = std::stod(match[4].str());
                    double zRot = std::stod(match[5].str());
                    std::string cellID = match[6].str();  // Cell name (with escaped quotes)

                    // Round only the integer part for grid coordinates
                    int gridX = static_cast<int>(std::floor(destX / 8192.0));
                    int gridY = static_cast<int>(std::floor(destY / 8192.0));

                    // Check if coordinate is valid (in DB or customCoordinates)
                    if (isCoordinateValid(db, gridX, gridY, customCoordinates, conversionChoice, logFile)) {
                        int newGridX = gridX + offset.offsetX;
                        int newGridY = gridY + offset.offsetY;

                        logMessage("Found: Dialogue 'Position Cell' translation -> grid (" + std::to_string(gridX) + ", " + std::to_string(gridY) +
                                   ") | coordinates (" + std::to_string(destX) + ", " + std::to_string(destY) + ")", logFile);

                        // New calculation keeping the fractional part
                        double newDestX = (newGridX * 8192.0) + (destX - (gridX * 8192.0));
                        double newDestY = (newGridY * 8192.0) + (destY - (gridY * 8192.0));

                        logMessage("Calculating: new destination ----------------> grid (" + std::to_string(newGridX) + ", " + std::to_string(newGridY) +
                                   ") | coordinates (" + std::to_string(newDestX) + ", " + std::to_string(newDestY) + ")", logFile);

                        // Mark replacement in replacements
                        replacementsFlag = 1;

                        // Form the updated command string
                        std::ostringstream formattedCommand;
                        formattedCommand << std::fixed << std::setprecision(3);
                        formattedCommand << commandType << ", " << newDestX << ", " << newDestY << ", " << destZ;
                        formattedCommand << ", " << std::fixed << std::setprecision(0) << zRot;
                        formattedCommand << ", " << cellID;

                        updatedText += scriptText.substr(searchStart - scriptText.cbegin(), match.position()) +
                            formattedCommand.str();
                    }
                    else {
                        // If no data is found in the database, leave the command unchanged
                        updatedText += scriptText.substr(searchStart - scriptText.cbegin(), match.position()) +
                            match.str();
                    }

                    searchStart = match.suffix().first;
                }

                // Add the remaining text
                updatedText += std::string(searchStart, scriptText.cend());

                // Save the updated text
                dialogueInfo["script_text"] = updatedText;
            }
        }
    }
}

// Function to process Script PlaceItem translation
void processScriptPlaceItemTranslation(sqlite3* db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, std::vector<std::string>& updatedScriptIDs, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates, int conversionChoice, std::ofstream& logFile) {

    // Regular expression to find PlaceItem commands
    std::regex placeItemRegex(R"((PlaceItem)\s*,?\s*((?:\"[^\"]+\")|\S+)\s*,?\s*(-?\d+(?:\.\d+)?)\s*,?\s*(-?\d+(?:\.\d+)?)\s*,?\s*(-?\d+(?:\.\d+)?)\s*,?\s*(-?\d+(?:\.\d+)?))",
        std::regex_constants::icase);

    // Loop through all objects in inputData
    for (auto& script : inputData) {
        if (script.contains("type") && script["type"] == "Script") {
            if (script.contains("text")) {
                std::string scriptText = script["text"].get<std::string>();
                std::string scriptID = script.contains("id") ? script["id"].get<std::string>() : "Unknown";
                std::smatch match;

                // Resulting text after processing
                std::string updatedText;
                std::string::const_iterator searchStart(scriptText.cbegin());
                bool scriptUpdated = false;

                while (std::regex_search(searchStart, scriptText.cend(), match, placeItemRegex)) {
                    std::string commandType = match[1].str();
                    std::string objectID = match[2].str();
                    double destX = std::stod(match[3].str());
                    double destY = std::stod(match[4].str());
                    double destZ = std::stod(match[5].str());
                    double zRot = std::stod(match[6].str());

                    // Round only the integer part for grid coordinates
                    int gridX = static_cast<int>(std::floor(destX / 8192.0));
                    int gridY = static_cast<int>(std::floor(destY / 8192.0));

                    // Check if coordinate is valid (in DB or customCoordinates)
                    if (isCoordinateValid(db, gridX, gridY, customCoordinates, conversionChoice, logFile)) {
                        int newGridX = gridX + offset.offsetX;
                        int newGridY = gridY + offset.offsetY;

                        logMessage("Found: Script 'Place Item' translation -> grid (" + std::to_string(gridX) + ", " + std::to_string(gridY) +
                                   ") | coordinates (" + std::to_string(destX) + ", " + std::to_string(destY) + ")", logFile);

                        // New calculation keeping the fractional part
                        double newDestX = (newGridX * 8192.0) + (destX - (gridX * 8192.0));
                        double newDestY = (newGridY * 8192.0) + (destY - (gridY * 8192.0));

                        logMessage("Calculating: new destination -----------> grid (" + std::to_string(newGridX) + ", " + std::to_string(newGridY) +
                                   ") | coordinates (" + std::to_string(newDestX) + ", " + std::to_string(newDestY) + ")", logFile);

                        // Mark replacement in replacements
                        replacementsFlag = 1;
                        scriptUpdated = true;

                        // Form the updated command string
                        std::ostringstream formattedCommand;
                        formattedCommand << std::fixed << std::setprecision(3);
                        formattedCommand << commandType << ", " << objectID << ", " << newDestX << ", " << newDestY << ", " << destZ;
                        formattedCommand << ", " << std::fixed << std::setprecision(0) << zRot;

                        updatedText += scriptText.substr(searchStart - scriptText.cbegin(), match.position()) +
                            formattedCommand.str();
                    }
                    else {
                        // If no data found in the database, leave the command unchanged
                        updatedText += scriptText.substr(searchStart - scriptText.cbegin(), match.position()) +
                            match.str();
                    }

                    searchStart = match.suffix().first;
                }

                // Add remaining text
                updatedText += std::string(searchStart, scriptText.cend());

                // Save the updated text
                script["text"] = updatedText;

                // If this script was updated, add its ID to the list
                if (scriptUpdated) {
                    updatedScriptIDs.push_back(scriptID);
                }
            }
        }
    }
}

// Function to process Dialogue PlaceItem translation
void processDialoguePlaceItemTranslation(sqlite3* db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates, int conversionChoice, std::ofstream& logFile) {

    // Regular expression to find PlaceItem commands
    std::regex placeItemRegex(R"((PlaceItem)\s*,?\s*((?:\"[^\"]+\")|\S+)\s*,?\s*(-?\d+(?:\.\d+)?)\s*,?\s*(-?\d+(?:\.\d+)?)\s*,?\s*(-?\d+(?:\.\d+)?)\s*,?\s*(-?\d+(?:\.\d+)?))",
        std::regex_constants::icase);

    // Loop through all objects in inputData
    for (auto& dialogueInfo : inputData) {
        if (dialogueInfo.contains("type") && dialogueInfo["type"] == "DialogueInfo") {
            if (dialogueInfo.contains("script_text")) {
                std::string scriptText = dialogueInfo["script_text"].get<std::string>();
                std::smatch match;

                // Resulting text after processing
                std::string updatedText;
                std::string::const_iterator searchStart(scriptText.cbegin());

                while (std::regex_search(searchStart, scriptText.cend(), match, placeItemRegex)) {
                    std::string commandType = match[1].str();
                    std::string objectID = match[2].str();
                    double destX = std::stod(match[3].str());
                    double destY = std::stod(match[4].str());
                    double destZ = std::stod(match[5].str());
                    double zRot = std::stod(match[6].str());

                    // Round only the integer part for grid coordinates
                    int gridX = static_cast<int>(std::floor(destX / 8192.0));
                    int gridY = static_cast<int>(std::floor(destY / 8192.0));

                    // Check if coordinate is valid (in DB or customCoordinates)
                    if (isCoordinateValid(db, gridX, gridY, customCoordinates, conversionChoice, logFile)) {
                        int newGridX = gridX + offset.offsetX;
                        int newGridY = gridY + offset.offsetY;

                        logMessage("Found: Dialogue 'Place Item' translation -> grid (" + std::to_string(gridX) + ", " + std::to_string(gridY) +
                                   ") | coordinates (" + std::to_string(destX) + ", " + std::to_string(destY) + ")", logFile);

                        // New calculation keeping the fractional part
                        double newDestX = (newGridX * 8192.0) + (destX - (gridX * 8192.0));
                        double newDestY = (newGridY * 8192.0) + (destY - (gridY * 8192.0));

                        logMessage("Calculating: new destination -------------> grid (" + std::to_string(newGridX) + ", " + std::to_string(newGridY) +
                                   ") | coordinates (" + std::to_string(newDestX) + ", " + std::to_string(newDestY) + ")", logFile);

                        // Mark replacement in replacements
                        replacementsFlag = 1;

                        // Form the updated command string
                        std::ostringstream formattedCommand;
                        formattedCommand << std::fixed << std::setprecision(3);
                        formattedCommand << commandType << ", " << objectID << ", " << newDestX << ", " << newDestY << ", " << destZ;
                        formattedCommand << ", " << std::fixed << std::setprecision(0) << zRot;

                        updatedText += scriptText.substr(searchStart - scriptText.cbegin(), match.position()) +
                            formattedCommand.str();
                    }
                    else {
                        // If no data found in the database, leave the command unchanged
                        updatedText += scriptText.substr(searchStart - scriptText.cbegin(), match.position()) +
                            match.str();
                    }

                    searchStart = match.suffix().first;
                }

                // Add remaining text
                updatedText += std::string(searchStart, scriptText.cend());

                // Save the updated text
                dialogueInfo["script_text"] = updatedText;
            }
        }
    }
}

// Function to process Script PlaceItemCell translation
void processScriptPlaceItemCellTranslation(sqlite3* db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, std::vector<std::string>& updatedScriptIDs, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates, int conversionChoice, std::ofstream& logFile) {

    // Regular expression to find PlaceItemCell commands
    std::regex placeItemCellRegex(R"((PlaceItemCell)\s*,?\s*((?:\"[^\"]+\")|\S+)\s*,?\s*((?:\"[^\"]+\")|\S+)\s*,?\s*(-?\d+(?:\.\d+)?)\s*,?\s*(-?\d+(?:\.\d+)?)\s*,?\s*(-?\d+(?:\.\d+)?)\s*,?\s*(-?\d+(?:\.\d+)?))",
        std::regex_constants::icase);

    // Loop through all objects in inputData
    for (auto& script : inputData) {
        if (script.contains("type") && script["type"] == "Script") {
            if (script.contains("text")) {
                std::string scriptText = script["text"].get<std::string>();
                std::string scriptID = script.contains("id") ? script["id"].get<std::string>() : "Unknown";
                std::smatch match;

                // Resulting text after processing
                std::string updatedText;
                std::string::const_iterator searchStart(scriptText.cbegin());
                bool scriptUpdated = false;

                while (std::regex_search(searchStart, scriptText.cend(), match, placeItemCellRegex)) {
                    std::string commandType = match[1].str();
                    std::string objectID = match[2].str();
                    std::string cellID = match[3].str();
                    double destX = std::stod(match[4].str());
                    double destY = std::stod(match[5].str());
                    double destZ = std::stod(match[6].str());
                    double zRot = std::stod(match[7].str());

                    // Round only the integer part for grid coordinates
                    int gridX = static_cast<int>(std::floor(destX / 8192.0));
                    int gridY = static_cast<int>(std::floor(destY / 8192.0));

                    // Check if coordinate is valid (in DB or customCoordinates)
                    if (isCoordinateValid(db, gridX, gridY, customCoordinates, conversionChoice, logFile)) {
                        int newGridX = gridX + offset.offsetX;
                        int newGridY = gridY + offset.offsetY;

                        logMessage("Found: Script 'Place Item Cell' translation -> grid (" + std::to_string(gridX) + ", " + std::to_string(gridY) +
                                   ") | coordinates (" + std::to_string(destX) + ", " + std::to_string(destY) + ")", logFile);

                        // New calculation keeping the fractional part
                        double newDestX = (newGridX * 8192.0) + (destX - (gridX * 8192.0));
                        double newDestY = (newGridY * 8192.0) + (destY - (gridY * 8192.0));

                        logMessage("Calculating: new destination ----------------> grid (" + std::to_string(newGridX) + ", " + std::to_string(newGridY) +
                                   ") | coordinates (" + std::to_string(newDestX) + ", " + std::to_string(newDestY) + ")", logFile);

                        // Mark replacement in replacements
                        replacementsFlag = 1;
                        scriptUpdated = true;

                        // Form the updated command string
                        std::ostringstream formattedCommand;
                        formattedCommand << std::fixed << std::setprecision(3);
                        formattedCommand << commandType << ", " << objectID << ", " << cellID << ", " << newDestX << ", " << newDestY << ", " << destZ;
                        formattedCommand << ", " << std::fixed << std::setprecision(0) << zRot;

                        updatedText += scriptText.substr(searchStart - scriptText.cbegin(), match.position()) +
                            formattedCommand.str();
                    }
                    else {
                        // If no data found in the database, leave the command unchanged
                        updatedText += scriptText.substr(searchStart - scriptText.cbegin(), match.position()) +
                            match.str();
                    }

                    searchStart = match.suffix().first;
                }

                // Add remaining text
                updatedText += std::string(searchStart, scriptText.cend());

                // Save the updated text
                script["text"] = updatedText;

                // If this script was updated, add its ID to the list
                if (scriptUpdated) {
                    updatedScriptIDs.push_back(scriptID);
                }
            }
        }
    }
}

// Function to process Dialogue PlaceItemCell translation
void processDialoguePlaceItemCellTranslation(sqlite3* db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates, int conversionChoice, std::ofstream& logFile) {

    // Regular expression to find PlaceItemCell commands
    std::regex placeItemCellRegex(R"((PlaceItemCell)\s*,?\s*((?:\"[^\"]+\")|\S+)\s*,?\s*((?:\"[^\"]+\")|\S+)\s*,?\s*(-?\d+(?:\.\d+)?)\s*,?\s*(-?\d+(?:\.\d+)?)\s*,?\s*(-?\d+(?:\.\d+)?)\s*,?\s*(-?\d+(?:\.\d+)?))",
        std::regex_constants::icase);

    // Loop through all objects in inputData
    for (auto& dialogueInfo : inputData) {
        if (dialogueInfo.contains("type") && dialogueInfo["type"] == "DialogueInfo") {
            if (dialogueInfo.contains("script_text")) {
                std::string scriptText = dialogueInfo["script_text"].get<std::string>();
                std::smatch match;

                // Resulting text after processing
                std::string updatedText;
                std::string::const_iterator searchStart(scriptText.cbegin());

                while (std::regex_search(searchStart, scriptText.cend(), match, placeItemCellRegex)) {
                    std::string commandType = match[1].str();
                    std::string objectID = match[2].str();
                    std::string cellID = match[3].str();
                    double destX = std::stod(match[4].str());
                    double destY = std::stod(match[5].str());
                    double destZ = std::stod(match[6].str());
                    double zRot = std::stod(match[7].str());

                    // Round only the integer part for grid coordinates
                    int gridX = static_cast<int>(std::floor(destX / 8192.0));
                    int gridY = static_cast<int>(std::floor(destY / 8192.0));

                    // Check if coordinate is valid (in DB or customCoordinates)
                    if (isCoordinateValid(db, gridX, gridY, customCoordinates, conversionChoice, logFile)) {
                        int newGridX = gridX + offset.offsetX;
                        int newGridY = gridY + offset.offsetY;

                        logMessage("Found: Dialogue 'Place Item Cell' translation -> grid (" + std::to_string(gridX) + ", " + std::to_string(gridY) +
                            ") | coordinates (" + std::to_string(destX) + ", " + std::to_string(destY) + ")", logFile);

                        // New calculation keeping the fractional part
                        double newDestX = (newGridX * 8192.0) + (destX - (gridX * 8192.0));
                        double newDestY = (newGridY * 8192.0) + (destY - (gridY * 8192.0));

                        logMessage("Calculating: new destination ------------------> grid (" + std::to_string(newGridX) + ", " + std::to_string(newGridY) +
                            ") | coordinates (" + std::to_string(newDestX) + ", " + std::to_string(newDestY) + ")", logFile);

                        // Mark replacement in replacements
                        replacementsFlag = 1;

                        // Form the updated command string
                        std::ostringstream formattedCommand;
                        formattedCommand << std::fixed << std::setprecision(3);
                        formattedCommand << commandType << ", " << objectID << ", " << cellID << ", " << newDestX << ", " << newDestY << ", " << destZ;
                        formattedCommand << ", " << std::fixed << std::setprecision(0) << zRot;

                        updatedText += scriptText.substr(searchStart - scriptText.cbegin(), match.position()) +
                            formattedCommand.str();
                    }
                    else {
                        // If no data found in the database, leave the command unchanged
                        updatedText += scriptText.substr(searchStart - scriptText.cbegin(), match.position()) +
                            match.str();
                    }

                    searchStart = match.suffix().first;
                }

                // Add remaining text
                updatedText += std::string(searchStart, scriptText.cend());

                // Save the updated text
                dialogueInfo["script_text"] = updatedText;
            }
        }
    }
}

// Function to search and update the translation block inside the references object
void processTranslation(ordered_json& jsonData, const GridOffset& offset, int& replacementsFlag, std::ofstream& logFile) {
    // Check if the 'references' key exists and is an array
    if (!jsonData.contains("references") || !jsonData["references"].is_array()) {
            logMessage("References key is missing or is not an array in JSON.", logFile);
        return;
    }

    // Loop through each reference in the references array
    for (auto& reference : jsonData["references"]) {
        // Check if the reference is marked as deleted
        if (reference.contains("deleted") && reference["deleted"].get<bool>()) {
            //logMessage("Skipping deleted reference -> " + reference.value("id", "Unknown ID"), logFile);
            continue; // Skip this reference
        }

        // Check if the reference contains "temporary" and "translation", and if translation is an array of at least 2 elements
        if (reference.contains("temporary") &&
            reference.contains("translation") &&
            reference["translation"].is_array() &&
            reference["translation"].size() >= 2) {

            logMessage("Processing: " + reference.value("id", "Unknown ID"), logFile);

            // Log the original translation values before update
            double originalX = reference["translation"][0].get<double>();
            double originalY = reference["translation"][1].get<double>();
            logMessage("Found reference coordinates -> X = " + std::to_string(originalX) + ", Y = " + std::to_string(originalY), logFile);

            // Apply the offset to the X and Y values (multiplied by 8192 for scaling)
            reference["translation"][0] = originalX + offset.offsetX * 8192;
            reference["translation"][1] = originalY + offset.offsetY * 8192;

            // Mark that a replacement has been made
            replacementsFlag = 1;

            // Log the updated translation values after modification
            double updatedX = reference["translation"][0].get<double>();
            double updatedY = reference["translation"][1].get<double>();
            logMessage("Calculating new coordinates -> X = " + std::to_string(updatedX) + ", Y = " + std::to_string(updatedY), logFile);
        }
        else {
            logMessage("No valid temporary or translation array found in reference: " + reference.value("id", "Unknown ID"), logFile);
        }
    }
}

// Function to process coordinates for Cell, Landscape, and PathGrid types
void processGridValues(sqlite3* db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates, int conversionChoice, std::ofstream& logFile) {

    // List of supported types
    std::vector<std::string> typeNames = { "Cell", "Landscape", "PathGrid" };

    // Loop through each item in the input data
    for (auto& item : inputData) {
        // Check if the 'type' key exists and matches one of the supported types
        if (!item.contains("type") || std::find(typeNames.begin(), typeNames.end(), item["type"].get<std::string>()) == typeNames.end()) {
            continue;
        }

        std::string typeName = item["type"];

        // Check for the presence of 'grid' at the top level or inside the 'data' object
        bool hasTopLevelGrid = item.contains("grid") && item["grid"].is_array();
        bool hasDataGrid = item.contains("data") && item["data"].contains("grid") && item["data"]["grid"].is_array();

        if (!hasTopLevelGrid && !hasDataGrid) {
            logMessage("Grid key is missing for type: " + typeName, logFile);
            continue;
        }

        // Get current grid coordinates (X, Y)
        int gridX = 0, gridY = 0;
        if (hasTopLevelGrid) {
            gridX = item["grid"][0].get<int>();
            gridY = item["grid"][1].get<int>();
        }
        else if (hasDataGrid) {
            gridX = item["data"]["grid"][0].get<int>();
            gridY = item["data"]["grid"][1].get<int>();
        }

        // Check if coordinate is valid (in DB or customCoordinates)
        if (isCoordinateValid(db, gridX, gridY, customCoordinates, conversionChoice, logFile)) {
            int newGridX = gridX + offset.offsetX;
            int newGridY = gridY + offset.offsetY;

            logMessage("Updating grid coordinates for (" + typeName + "): (" + std::to_string(gridX) + ", " + std::to_string(gridY) +
                       ") -> (" + std::to_string(newGridX) + ", " + std::to_string(newGridY) + ")", logFile);

            // Update the grid coordinates in the data
            if (hasTopLevelGrid) {
                item["grid"][0] = newGridX;
                item["grid"][1] = newGridY;
            }
            else if (hasDataGrid) {
                item["data"]["grid"][0] = newGridX;
                item["data"]["grid"][1] = newGridY;
            }

            // If the type is "Cell", call the processTranslation function to adjust translations
            if (typeName == "Cell") {
                processTranslation(item, offset, replacementsFlag, logFile);
            }

            // Mark that a replacement has been made
            replacementsFlag = 1;

        }
    }
}

// Function to log updated script IDs
void logUpdatedScriptIDs(const std::vector<std::string>& updatedScriptIDs, std::ofstream& logFile) {
    if (updatedScriptIDs.empty()) {
        logMessage("\nNo scripts were updated", logFile);
        return;
    }

    std::unordered_set<std::string> seenIDs;
    std::vector<std::string> uniqueIDs;

    // Filter duplicates
    for (const auto& id : updatedScriptIDs) {
        if (seenIDs.insert(id).second) {  // If the ID is not already added
            uniqueIDs.push_back(id);
        }
    }

    logMessage("\nUpdated scripts list:", logFile);
    for (const auto& id : uniqueIDs) {
        logMessage("- Script ID: " + id, logFile);
    }
}

// Function to save the modified JSON data to file
bool saveJsonToFile(const std::filesystem::path& jsonImportPath, const ordered_json& inputData, std::ofstream& logFile) {
    std::ofstream outputFile(jsonImportPath);
        if (!outputFile) return false;
        outputFile << std::setw(2) << inputData;
            logMessage("\nModified data saved as: " + jsonImportPath.string() + "\n", logFile);
    return true;
}

// Function to convert the .JSON file to .ESP|ESM
bool convertJsonToEsp(const std::filesystem::path& jsonImportPath, const std::filesystem::path& espFilePath, std::ofstream& logFile) {
    std::string command = "tes3conv.exe \"" + jsonImportPath.string() + "\" \"" + espFilePath.string() + "\"";
        if (std::system(command.c_str()) != 0) return false;
            logMessage("Conversion to .ESP|ESM successful: " + espFilePath.string() + "\n", logFile);
    return true;
}

// Main function
int main() {
    // Display program information
    std::cout << PROGRAM_NAME << "\n" << PROGRAM_VERSION << "\n" << PROGRAM_AUTHOR << "\n\n";

    // Log file initialisation
    std::ofstream logFile("tes3_ab_log.txt", std::ios::app);
    if (!logFile) {
        std::cerr << "ERROR - failed to open log file!\n\n"
                     "Press Enter to exit...";
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        std::exit(EXIT_FAILURE);
    }

    // Clear log file
    logClear();
    logMessage("Log file cleared...", logFile);

    // Check if the database file exists
    if (!std::filesystem::exists("tes3_ab_cell_x-y_data.db")) {
        logErrorAndExit(nullptr, "ERROR - database file 'tes3_ab_cell_x-y_data.db' not found!\n", logFile);
    }

    // Open the database
    sqlite3* db = nullptr;
    if (sqlite3_open("tes3_ab_cell_x-y_data.db", &db)) {
        logErrorAndExit(db, "ERROR - failed to open database: " + std::string(sqlite3_errmsg(db)) + "!\n", logFile);
    }
    logMessage("Database opened successfully...", logFile);

    // Check if the custom grid coordinates file exists
    std::filesystem::path customDBFilePath = "tes3_ab_custom_cell_x-y_data.txt";
    if (!std::filesystem::exists(customDBFilePath)) {
        logErrorAndExit(nullptr, "ERROR - custom grid coordinates file 'tes3_ab_custom_cell_x-y_data.txt' not found!\n", logFile);
    }

    // Open the custom grid coordinates
    std::unordered_set<std::pair<int, int>, PairHash> customCoordinates;
    loadCustomGridCoordinates(customDBFilePath.string(), customCoordinates, logFile);
    logMessage("Custom grid coordinates loaded successfully...", logFile);

    // Check if the converter executable exists
    if (!std::filesystem::exists("tes3conv.exe")) {
        logErrorAndExit(db, "ERROR - tes3conv.exe not found! Please download the latest version from\n"
                            "github.com/Greatness7/tes3conv/releases and place it in the same directory\n"
                            "with this program.\n", logFile);
    }
    logMessage("tes3conv.exe found...\n"
               "Initialisation complete.", logFile);

    // Get the conversion choice from user
    int conversionChoice = getUserConversionChoice(logFile);

    // Get the input file path from user
    std::filesystem::path pluginImportPath = getInputFilePath(logFile);

    // Define the output file path
    std::filesystem::path jsonImportPath = pluginImportPath.parent_path() / (pluginImportPath.stem().string() + ".json");

    // Convert the input file to .JSON
    std::string command = "tes3conv.exe \"" + pluginImportPath.string() + "\" \"" + jsonImportPath.string() + "\"";
    if (std::system(command.c_str()) != 0) {
        logErrorAndExit(db, "ERROR - converting to .JSON failed!\n", logFile);
    }
    logMessage("Conversion to .JSON successful: " + jsonImportPath.string(), logFile);

    // Load the generated JSON file into a JSON object
    std::ifstream inputFile(jsonImportPath);
    ordered_json inputData;
    inputFile >> inputData;
    inputFile.close();

    // Check the dependency order of the Parent Master files in the input data
    auto [isValid, validMasters] = checkDependencyOrder(inputData, logFile);
    if (!isValid) {
        std::filesystem::remove(jsonImportPath);
        logMessage("Temporary .JSON file deleted: " + jsonImportPath.string() + "\n", logFile);
        logErrorAndExit(db, "ERROR - required Parent Master files dependency not found, or theit order is invalid!\n", logFile);
    }

    // Initialize the replacements flag
    int replacementsFlag = 0;

    // Initialize vector to store the IDs of scripts that were updated during processing
    std::vector<std::string> updatedScriptIDs;

    // Initialize the grid offsets based on user conversion choice
    GridOffset offset = getGridOffset(conversionChoice);

    // Process replacements
    processGridValues(db, inputData, offset, replacementsFlag, customCoordinates, conversionChoice, logFile);
    processInterriorDoorsTranslation(db, inputData, offset, replacementsFlag, customCoordinates, conversionChoice, logFile);
    processNpcTravelDestinations(db, inputData, offset, replacementsFlag, customCoordinates, conversionChoice, logFile);

    processScriptAiEscortTranslation(db, inputData, offset, replacementsFlag, updatedScriptIDs, customCoordinates, conversionChoice, logFile);
    processScriptAiEscortCellTranslation(db, inputData, offset, replacementsFlag, updatedScriptIDs, customCoordinates, conversionChoice, logFile);
    processScriptAiFollowTranslation(db, inputData, offset, replacementsFlag, updatedScriptIDs, customCoordinates, conversionChoice, logFile);
    processScriptAiFollowCellTranslation(db, inputData, offset, replacementsFlag, updatedScriptIDs, customCoordinates, conversionChoice, logFile);
    processScriptAiTravelTranslation(db, inputData, offset, replacementsFlag, updatedScriptIDs, customCoordinates, conversionChoice, logFile);
    processScriptPositionTranslation(db, inputData, offset, replacementsFlag, updatedScriptIDs, customCoordinates, conversionChoice, logFile);
    processScriptPositionCellTranslation(db, inputData, offset, replacementsFlag, updatedScriptIDs, customCoordinates, conversionChoice, logFile);
    processScriptPlaceItemTranslation(db, inputData, offset, replacementsFlag, updatedScriptIDs, customCoordinates, conversionChoice, logFile);
    processScriptPlaceItemCellTranslation(db, inputData, offset, replacementsFlag, updatedScriptIDs, customCoordinates, conversionChoice, logFile);

    processDialogueAiEscortTranslation(db, inputData, offset, replacementsFlag, customCoordinates, conversionChoice, logFile);
    processDialogueAiEscortCellTranslation(db, inputData, offset, replacementsFlag, customCoordinates, conversionChoice, logFile);
    processDialogueAiFollowTranslation(db, inputData, offset, replacementsFlag, customCoordinates, conversionChoice, logFile);
    processDialogueAiFollowCellTranslation(db, inputData, offset, replacementsFlag, customCoordinates, conversionChoice, logFile);
    processDialogueAiTravelTranslation(db, inputData, offset, replacementsFlag, customCoordinates, conversionChoice, logFile);
    processDialoguePositionTranslation(db, inputData, offset, replacementsFlag, customCoordinates, conversionChoice, logFile);
    processDialoguePositionCellTranslation(db, inputData, offset, replacementsFlag, customCoordinates, conversionChoice, logFile);
    processDialoguePlaceItemTranslation(db, inputData, offset, replacementsFlag, customCoordinates, conversionChoice, logFile);
    processDialoguePlaceItemCellTranslation(db, inputData, offset, replacementsFlag, customCoordinates, conversionChoice, logFile);

    // Check if any replacements were made: if no replacements were found, cancel the conversion
    if (replacementsFlag == 0) {
        std::filesystem::remove(jsonImportPath);
        logMessage("Temporary .JSON file deleted: " + jsonImportPath.string() + "\n", logFile);
        logErrorAndExit(db, "No replacements found: conversion canceled\n", logFile);
    }

    // Log updated script IDs
    logUpdatedScriptIDs(updatedScriptIDs, logFile);

    // Define conversion prefix
    std::string convPrefix = (conversionChoice == 1) ? "BMtoAB" : "ABtoBM";

    // Save the modified data to.JSON file
    auto newJsonName = std::format("CONV_{}_{}{}", convPrefix, pluginImportPath.stem().string(), ".json");

    std::filesystem::path jsonExportPath = pluginImportPath.parent_path() / newJsonName;
    if (!saveJsonToFile(jsonExportPath, inputData, logFile)) {
        logErrorAndExit(db, "ERROR - failed to save modified data to .JSON file!\n", logFile);
    }

    // Convert the .JSON file back to .ESP|ESM
    auto pluginExportName = std::format("CONV_{}_{}{}", convPrefix, pluginImportPath.stem().string(), pluginImportPath.extension().string());

    std::filesystem::path pluginExportPath = pluginImportPath.parent_path() / pluginExportName;
    if (!convertJsonToEsp(jsonExportPath, pluginExportPath, logFile)) {
        logErrorAndExit(db, "ERROR - failed to convert .JSON back to .ESP|ESM!\n", logFile);
    }

    // Clean up temporary .JSON files
    std::filesystem::remove(jsonImportPath);
    std::filesystem::remove(jsonExportPath);
    logMessage("Temporary .JSON files deleted: " + jsonImportPath.string() + "\n" +
               "                          and: " + jsonExportPath.string() + "\n", logFile);

    // Close the database
    sqlite3_close(db);
    logMessage("The ending of the words is ALMSIVI\n", logFile);
    logFile.close();

    // Wait for user input before exiting
    std::cout << "Press Enter to continue...";
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    return 0;
}