#include <iostream>               // For standard input and output operations
#include <fstream>                // For file input and output operations
#include <sstream>                // For working with string streams
#include <string>                 // For string manipulation and handling
#include <iomanip>                // For manipulating the output format (e.g., precision)
#include <limits>                 // For setting input limits and working with numeric limits
#include <regex>                  // For regular expressions (pattern matching)
#include <unordered_map>          // For efficient key-value pair storage (hash map)
#include <unordered_set>          // For efficient storage of unique elements (hash set)
#include <functional>             // For using std::hash in custom hash functions
#include <utility>                // For utility functions and data structures like std::pair
#include <vector>                 // For dynamic arrays or lists
#include <filesystem>             // For interacting with the file system (directories, files)
#include <sqlite3.h>              // For interacting with SQLite databases
#include <json.hpp>               // For working with JSON data (nlohmann's JSON library)

// Define an alias for ordered JSON type from the nlohmann library
using ordered_json = nlohmann::ordered_json;

// Define program metadata constants
const std::string PROGRAM_NAME = "TES3 Anthology Bloodmoon Converter";
const std::string PROGRAM_VERSION = "V 1.0.0";
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

// Function to log messages to both a log file and console
void logMessage(const std::string& message, const std::filesystem::path& logFilePath = "tes3_ab_log.txt") {
    std::ofstream logFile(logFilePath, std::ios_base::app);

    // Check if the file opened successfully and write the message
    if (logFile.is_open()) {
        logFile << message << std::endl;
    }
    else {
        std::cerr << "Failed to open log file." << std::endl;
    }

    std::cout << message << std::endl;
}

// Function to log errors, close the database (if open), and terminate the program
void logErrorAndExit(sqlite3* db, const std::string& message) {
    logMessage(message);

    // Close the SQLite database if it is open to avoid memory leaks
    if (db) {
        sqlite3_close(db);
    }

    // Prompt the user to press Enter to continue and clear any input buffer
    std::cout << "Press Enter to continue...";
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    // Throw a runtime error to exit the program and propagate the error
    throw std::runtime_error(message);
}

// Function to clear the log file if it exists, and log the status
void clearLogFile(const std::filesystem::path& logFileName = "tes3_ab_log.txt") {
    // Check if the log file exists before trying to remove it
    if (std::filesystem::exists(logFileName)) {
        try {
            std::filesystem::remove(logFileName);
            logMessage("Log cleared successfully...", logFileName);
        }
        catch (const std::filesystem::filesystem_error& e) {
            // Log any error that occurs during the file removal process
            logMessage("Error clearing log file: " + std::string(e.what()), logFileName);
        }
    }
}

// Function to get user input for conversion choice
int getUserConversionChoice() {
    int ConversionChoice;
    while (true) {
        std::cout << "Convert a plugin or master file:\n"
            "1. From Bloodmoon to Anthology Bloodmoon\n2. From Anthology Bloodmoon to Bloodmoon\nChoice: ";
        std::string input;
        std::getline(std::cin, input);
        if (input == "1" || input == "2") {
            ConversionChoice = input[0] - '0'; // Convert char to int
            break;
        }
        logMessage("Invalid choice. Enter 1 or 2.");
    }
    return ConversionChoice;
}

// Function to get the path of the input file from the user
std::filesystem::path getInputFilePath() {
    std::filesystem::path filePath;
    while (true) {
        std::cout << "Enter full path to your .ESP or .ESM (including extension), or filename (with extension)\n"
            "if it's in the same directory with this program: ";
        std::string input;
        std::getline(std::cin, input);
        filePath = input;

        // Convert the file extension to lowercase for case-insensitive comparison
        std::string extension = filePath.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

        if (std::filesystem::exists(filePath) &&
            (extension == ".esp" || extension == ".esm")) {
            logMessage("Input file found: " + filePath.string());
            break;
        }
        logMessage("Input file not found or incorrect extension.");
    }
    return filePath;
}

// Function to check the order of dependencies in a file's data
std::pair<bool, std::unordered_set<int>> checkDependencyOrder(const ordered_json& inputData) {
    // Look for the "Header" section in the JSON.
    auto headerIter = std::find_if(inputData.begin(), inputData.end(), [](const ordered_json& item) {
        return item.contains("type") && item["type"] == "Header";
        });

    // Check if we found the "Header" section
    if (headerIter == inputData.end() || !headerIter->contains("masters")) {
        logMessage("Error: Missing 'Header' section or 'masters' key.");
        return { false, {} };
    }

    // Extract the list of masters
    const auto& masters = (*headerIter)["masters"];

    // Initialize positions as -1 to track the master files
    size_t mwPos = -1, tPos = -1, bPos = -1;

    // Go through the list and check the positions of the master files
    for (size_t i = 0; i < masters.size(); ++i) {
        if (masters[i].is_array() && !masters[i].empty()) {
            std::string masterName = masters[i][0];
            if (masterName == "Morrowind.esm") mwPos = i;
            else if (masterName == "Tribunal.esm") tPos = i;
            else if (masterName == "Bloodmoon.esm") bPos = i;
        }
    }

    if (mwPos == static_cast<size_t>(-1)) {
        logMessage("Morrowind.esm not found!");
        return { false, {} };
    }

    // Checking the order of Tribunal and Bloodmoon
    if (tPos != static_cast<size_t>(-1) && bPos != static_cast<size_t>(-1)) {
        if (tPos > mwPos && bPos > tPos) {
            logMessage("Valid order of Parent Masters found: M+T+B.\n");
            return { true, {} };
        }
        else {
            logMessage("Invalid order of Parent Masters! Tribunal.esm should be before Bloodmoon.esm.\n");
            return { false, {} };
        }
    }

    if (bPos != static_cast<size_t>(-1) && bPos > mwPos) {
        logMessage("Valid order of Parent Masters found: M+B.\n");
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
        return hash1 ^ (hash2 << 1); // Combine the two hash values
    }
};

// Function to load custom grid coordinates
void loadCustomGridCoordinates(const std::string& filePath, std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        logMessage("Failed to open custom grid coordinates file: " + filePath);
        return;
    }

    logMessage("Loading custom grid coordinates from: " + filePath);

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
            logMessage("Loaded custom coordinate: (" + std::to_string(x) + ", " + std::to_string(y) + ")");
        }
        else {
            logMessage("Invalid format in custom grid coordinates file: " + line);
        }
    }

    file.close();
}

// Function to check if a given grid coordinate (gridX, gridY) is valid. It checks both the database and custom user-defined coordinates
bool isCoordinateValid(sqlite3* db, int gridX, int gridY, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates) {
    // Check in the database
    sqlite3_stmt* stmt;
    std::string query = "SELECT BM_Grid_X, BM_Grid_Y FROM [tes3_ab_cell_x-y_data] WHERE BM_Grid_X = ? AND BM_Grid_Y = ?";

    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Error preparing database query: " + std::string(sqlite3_errmsg(db)) << "\n";
        return false;
    }

    sqlite3_bind_int(stmt, 1, gridX);
    sqlite3_bind_int(stmt, 2, gridY);

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
void processInterriorDoorsTranslation(sqlite3* db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates) {

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
                            if (isCoordinateValid(db, gridX, gridY, customCoordinates)) {
                                int newGridX = gridX + offset.offsetX;
                                int newGridY = gridY + offset.offsetY;

                                logMessage("Found Interior Cell Door translation: (" + std::to_string(gridX) + ", " +
                                    std::to_string(gridY) + ") -> (" + std::to_string(newGridX) + ", " +
                                    std::to_string(newGridY) + ")");

                                // New calculation keeping the fractional part for destination translation
                                double newDestX = (newGridX * 8192.0) + (destX - (gridX * 8192.0));
                                double newDestY = (newGridY * 8192.0) + (destY - (gridY * 8192.0));

                                // Mark the replacement in replacements
                                replacementsFlag = 1;

                                // Save to original fields
                                reference["destination"]["translation"][0] = newDestX;
                                reference["destination"]["translation"][1] = newDestY;

                                logMessage("Calculated new destination coordinates: (" + std::to_string(destX) + ", " +
                                    std::to_string(destY) + ") -> (" + std::to_string(newDestX) + ", " +
                                    std::to_string(newDestY) + ")");
                            }
                        }
                    }
                }
            }
        }
    }
}

// Function to process NPC travel destinations
void processNpcTravelDestinations(sqlite3* db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates) {

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
                        if (isCoordinateValid(db, gridX, gridY, customCoordinates)) {
                            int newGridX = gridX + offset.offsetX;
                            int newGridY = gridY + offset.offsetY;

                            logMessage("Found Npc travel destination: (" + std::to_string(gridX) + ", " +
                                std::to_string(gridY) + ") -> (" + std::to_string(newGridX) + ", " +
                                std::to_string(newGridY) + ")");

                            // New calculation keeping the fractional part for destination translation
                            double newDestX = (newGridX * 8192.0) + (destX - (gridX * 8192.0));
                            double newDestY = (newGridY * 8192.0) + (destY - (gridY * 8192.0));

                            // Mark the replacement in replacements
                            replacementsFlag = 1;

                            // Save to original fields
                            destination["translation"][0] = newDestX;
                            destination["translation"][1] = newDestY;

                            logMessage("Calculated new NPC travel destination coordinates: (" + std::to_string(destX) + ", " +
                                std::to_string(destY) + ") -> (" + std::to_string(newDestX) + ", " +
                                std::to_string(newDestY) + ")");
                        }
                    }
                }
            }
        }
    }
}

// Function to process Script AI Escort translation
void processScriptAiEscortTranslation(sqlite3* db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, std::vector<std::string>& updatedScriptIDs, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates) {

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
                    double x = std::stod(match[4].str());
                    double y = std::stod(match[5].str());
                    double z = std::stod(match[6].str());
                    std::string resetValue = (match.size() > 7 && match[7].matched) ? match[7].str() : "";

                    // Round only the integer part for grid coordinates
                    int gridX = static_cast<int>(std::floor(x / 8192.0));
                    int gridY = static_cast<int>(std::floor(y / 8192.0));

                    // Check if coordinate is valid (in DB or customCoordinates)
                    if (isCoordinateValid(db, gridX, gridY, customCoordinates)) {
                        int newGridX = gridX + offset.offsetX;
                        int newGridY = gridY + offset.offsetY;

                        logMessage("Found Script AI Escort translation: (" + std::to_string(gridX) + ", " +
                            std::to_string(gridY) + ") -> (" + std::to_string(newGridX) + ", " +
                            std::to_string(newGridY) + ")");

                        // New calculation keeping the fractional part
                        double newX = (newGridX * 8192.0) + (x - (gridX * 8192.0));
                        double newY = (newGridY * 8192.0) + (y - (gridY * 8192.0));

                        logMessage("Calculated new coordinates: (" + std::to_string(x) + ", " +
                            std::to_string(y) + ") -> (" + std::to_string(newX) + ", " +
                            std::to_string(newY) + ")");

                        // Mark replacement in replacements
                        replacementsFlag = 1;
                        scriptUpdated = true;

                        // Form the updated command string
                        std::ostringstream formattedCommand;
                        formattedCommand << std::fixed << std::setprecision(3);  // Set precision
                        formattedCommand << commandType << ", " << actorID << ", " << duration << ", "
                            << newX << ", " << newY << ", " << z;

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
void processDialogueAiEscortTranslation(sqlite3* db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates) {

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
                    double x = std::stod(match[4].str());
                    double y = std::stod(match[5].str());
                    double z = std::stod(match[6].str());
                    std::string resetValue = (match.size() > 7 && match[7].matched) ? match[7].str() : "";

                    // Round only the integer part for grid coordinates
                    int gridX = static_cast<int>(std::floor(x / 8192.0));
                    int gridY = static_cast<int>(std::floor(y / 8192.0));

                    // Check if coordinate is valid (in DB or customCoordinates)
                    if (isCoordinateValid(db, gridX, gridY, customCoordinates)) {
                        int newGridX = gridX + offset.offsetX;
                        int newGridY = gridY + offset.offsetY;

                        logMessage("Found Dialogue AI Escort translation: (" + std::to_string(gridX) + ", " +
                            std::to_string(gridY) + ") -> (" + std::to_string(newGridX) + ", " +
                            std::to_string(newGridY) + ")");

                        // New calculation keeping the fractional part
                        double newX = (newGridX * 8192.0) + (x - (gridX * 8192.0));
                        double newY = (newGridY * 8192.0) + (y - (gridY * 8192.0));

                        logMessage("Calculated new coordinates: (" + std::to_string(x) + ", " +
                            std::to_string(y) + ") -> (" + std::to_string(newX) + ", " +
                            std::to_string(newY) + ")");

                        // Mark replacement in replacements
                        replacementsFlag = 1;

                        // Form the updated command string
                        std::ostringstream formattedCommand;
                        formattedCommand << std::fixed << std::setprecision(3);  // Set precision
                        formattedCommand << commandType << ", " << actorID << ", " << duration << ", "
                            << newX << ", " << newY << ", " << z;

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
void processScriptAiEscortCellTranslation(sqlite3* db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, std::vector<std::string>& updatedScriptIDs, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates) {

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
                    double x = std::stod(match[5].str());
                    double y = std::stod(match[6].str());
                    double z = std::stod(match[7].str());
                    std::string resetValue = (match.size() > 8 && match[8].matched) ? match[8].str() : "";

                    // Round only the integer part for grid coordinates
                    int gridX = static_cast<int>(std::floor(x / 8192.0));
                    int gridY = static_cast<int>(std::floor(y / 8192.0));

                    // Check if coordinate is valid (in DB or customCoordinates)
                    if (isCoordinateValid(db, gridX, gridY, customCoordinates)) {
                        int newGridX = gridX + offset.offsetX;
                        int newGridY = gridY + offset.offsetY;

                        logMessage("Found Script AI Escort Cell translation: (" + std::to_string(gridX) + ", " +
                            std::to_string(gridY) + ") -> (" + std::to_string(newGridX) + ", " +
                            std::to_string(newGridY) + ")");

                        // New calculation keeping the fractional part
                        double newX = (newGridX * 8192.0) + (x - (gridX * 8192.0));
                        double newY = (newGridY * 8192.0) + (y - (gridY * 8192.0));

                        logMessage("Calculated new coordinates: (" + std::to_string(x) + ", " +
                            std::to_string(y) + ") -> (" + std::to_string(newX) + ", " +
                            std::to_string(newY) + ")");

                        // Mark replacement in replacements
                        replacementsFlag = 1;
                        scriptUpdated = true;

                        // Form the updated command string
                        std::ostringstream formattedCommand;
                        formattedCommand << std::fixed << std::setprecision(3);  // Set precision
                        formattedCommand << commandType << ", " << actorID << ", " << cellID << ", " << duration << ", "
                            << newX << ", " << newY << ", " << z;

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
void processDialogueAiEscortCellTranslation(sqlite3* db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates) {

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
                    double x = std::stod(match[5].str());
                    double y = std::stod(match[6].str());
                    double z = std::stod(match[7].str());
                    std::string resetValue = (match.size() > 8 && match[8].matched) ? match[8].str() : "";

                    // Round only the integer part for grid coordinates
                    int gridX = static_cast<int>(std::floor(x / 8192.0));
                    int gridY = static_cast<int>(std::floor(y / 8192.0));

                    // Check if coordinate is valid (in DB or customCoordinates)
                    if (isCoordinateValid(db, gridX, gridY, customCoordinates)) {
                        int newGridX = gridX + offset.offsetX;
                        int newGridY = gridY + offset.offsetY;

                        logMessage("Found Dialogue AI Escort Cell translation: (" + std::to_string(gridX) + ", " +
                            std::to_string(gridY) + ") -> (" + std::to_string(newGridX) + ", " +
                            std::to_string(newGridY) + ")");

                        // New calculation keeping the fractional part
                        double newX = (newGridX * 8192.0) + (x - (gridX * 8192.0));
                        double newY = (newGridY * 8192.0) + (y - (gridY * 8192.0));

                        logMessage("Calculated new coordinates: (" + std::to_string(x) + ", " +
                            std::to_string(y) + ") -> (" + std::to_string(newX) + ", " +
                            std::to_string(newY) + ")");

                        // Mark replacement in replacements
                        replacementsFlag = 1;

                        // Form the updated command string
                        std::ostringstream formattedCommand;
                        formattedCommand << std::fixed << std::setprecision(3);  // Set precision
                        formattedCommand << commandType << ", " << actorID << ", " << cellID << ", " << duration << ", "
                            << newX << ", " << newY << ", " << z;

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
void processScriptAiFollowTranslation(sqlite3* db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, std::vector<std::string>& updatedScriptIDs, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates) {

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
                    double x = std::stod(match[4].str());
                    double y = std::stod(match[5].str());
                    double z = std::stod(match[6].str());
                    std::string resetValue = (match.size() > 7 && match[7].matched) ? match[7].str() : "";

                    // Round only the integer part for grid coordinates
                    int gridX = static_cast<int>(std::floor(x / 8192.0));
                    int gridY = static_cast<int>(std::floor(y / 8192.0));

                    // Check if coordinate is valid (in DB or customCoordinates)
                    if (isCoordinateValid(db, gridX, gridY, customCoordinates)) {
                        int newGridX = gridX + offset.offsetX;
                        int newGridY = gridY + offset.offsetY;

                        logMessage("Found Script AI Follow translation: (" + std::to_string(gridX) + ", " +
                            std::to_string(gridY) + ") -> (" + std::to_string(newGridX) + ", " +
                            std::to_string(newGridY) + ")");

                        // New calculation keeping the fractional part
                        double newX = (newGridX * 8192.0) + (x - (gridX * 8192.0));
                        double newY = (newGridY * 8192.0) + (y - (gridY * 8192.0));

                        logMessage("Calculated new coordinates: (" + std::to_string(x) + ", " +
                            std::to_string(y) + ") -> (" + std::to_string(newX) + ", " +
                            std::to_string(newY) + ")");

                        // Mark replacement in replacements
                        replacementsFlag = 1;
                        scriptUpdated = true;

                        // Form the updated command string
                        std::ostringstream formattedCommand;
                        formattedCommand << std::fixed << std::setprecision(3);  // Set precision
                        formattedCommand << commandType << ", " << actorID << ", " << duration << ", "
                            << newX << ", " << newY << ", " << z;

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
void processDialogueAiFollowTranslation(sqlite3* db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates) {

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
                    double x = std::stod(match[4].str());
                    double y = std::stod(match[5].str());
                    double z = std::stod(match[6].str());
                    std::string resetValue = (match.size() > 7 && match[7].matched) ? match[7].str() : "";

                    // Round only the integer part for grid coordinates
                    int gridX = static_cast<int>(std::floor(x / 8192.0));
                    int gridY = static_cast<int>(std::floor(y / 8192.0));

                    // Check if coordinate is valid (in DB or customCoordinates)
                    if (isCoordinateValid(db, gridX, gridY, customCoordinates)) {
                        int newGridX = gridX + offset.offsetX;
                        int newGridY = gridY + offset.offsetY;

                        logMessage("Found Dialogue AI Follow translation: (" + std::to_string(gridX) + ", " +
                            std::to_string(gridY) + ") -> (" + std::to_string(newGridX) + ", " +
                            std::to_string(newGridY) + ")");

                        // New calculation keeping the fractional part
                        double newX = (newGridX * 8192.0) + (x - (gridX * 8192.0));
                        double newY = (newGridY * 8192.0) + (y - (gridY * 8192.0));

                        logMessage("Calculated new coordinates: (" + std::to_string(x) + ", " +
                            std::to_string(y) + ") -> (" + std::to_string(newX) + ", " +
                            std::to_string(newY) + ")");

                        // Mark replacement in replacements
                        replacementsFlag = 1;

                        // Form the updated command string
                        std::ostringstream formattedCommand;
                        formattedCommand << std::fixed << std::setprecision(3);  // Set precision
                        formattedCommand << commandType << ", " << actorID << ", " << duration << ", "
                            << newX << ", " << newY << ", " << z;

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
void processScriptAiFollowCellTranslation(sqlite3* db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, std::vector<std::string>& updatedScriptIDs, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates) {

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
                    double x = std::stod(match[5].str());
                    double y = std::stod(match[6].str());
                    double z = std::stod(match[7].str());
                    std::string resetValue = (match.size() > 8 && match[8].matched) ? match[8].str() : "";

                    // Round only the integer part for grid coordinates
                    int gridX = static_cast<int>(std::floor(x / 8192.0));
                    int gridY = static_cast<int>(std::floor(y / 8192.0));

                    // Check if coordinate is valid (in DB or customCoordinates)
                    if (isCoordinateValid(db, gridX, gridY, customCoordinates)) {
                        int newGridX = gridX + offset.offsetX;
                        int newGridY = gridY + offset.offsetY;

                        logMessage("Found Script AI Follow Cell translation: (" + std::to_string(gridX) + ", " +
                            std::to_string(gridY) + ") -> (" + std::to_string(newGridX) + ", " +
                            std::to_string(newGridY) + ")");

                        // New calculation keeping the fractional part
                        double newX = (newGridX * 8192.0) + (x - (gridX * 8192.0));
                        double newY = (newGridY * 8192.0) + (y - (gridY * 8192.0));

                        logMessage("Calculated new coordinates: (" + std::to_string(x) + ", " +
                            std::to_string(y) + ") -> (" + std::to_string(newX) + ", " +
                            std::to_string(newY) + ")");

                        // Mark replacement in replacements
                        replacementsFlag = 1;
                        scriptUpdated = true;

                        // Form the updated command string
                        std::ostringstream formattedCommand;
                        formattedCommand << std::fixed << std::setprecision(3);  // Set precision
                        formattedCommand << commandType << ", " << actorID << ", " << cellID << ", " << duration << ", "
                            << newX << ", " << newY << ", " << z;

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
void processDialogueAiFollowCellTranslation(sqlite3* db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates) {

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
                    double x = std::stod(match[5].str());
                    double y = std::stod(match[6].str());
                    double z = std::stod(match[7].str());
                    std::string resetValue = (match.size() > 8 && match[8].matched) ? match[8].str() : "";

                    // Round only the integer part for grid coordinates
                    int gridX = static_cast<int>(std::floor(x / 8192.0));
                    int gridY = static_cast<int>(std::floor(y / 8192.0));

                    // Check if coordinate is valid (in DB or customCoordinates)
                    if (isCoordinateValid(db, gridX, gridY, customCoordinates)) {
                        int newGridX = gridX + offset.offsetX;
                        int newGridY = gridY + offset.offsetY;

                        logMessage("Found Dialogue AI Follow translation: (" + std::to_string(gridX) + ", " +
                            std::to_string(gridY) + ") -> (" + std::to_string(newGridX) + ", " +
                            std::to_string(newGridY) + ")");

                        // New calculation keeping the fractional part
                        double newX = (newGridX * 8192.0) + (x - (gridX * 8192.0));
                        double newY = (newGridY * 8192.0) + (y - (gridY * 8192.0));

                        logMessage("Calculated new coordinates: (" + std::to_string(x) + ", " +
                            std::to_string(y) + ") -> (" + std::to_string(newX) + ", " +
                            std::to_string(newY) + ")");

                        // Mark replacement in replacements
                        replacementsFlag = 1;

                        // Form the updated command string
                        std::ostringstream formattedCommand;
                        formattedCommand << std::fixed << std::setprecision(3);  // Set precision
                        formattedCommand << commandType << ", " << actorID << ", " << cellID << ", " << duration << ", "
                            << newX << ", " << newY << ", " << z;

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
void processScriptAiTravelTranslation(sqlite3* db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, std::vector<std::string>& updatedScriptIDs, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates) {

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
                    double x = std::stod(match[2].str());
                    double y = std::stod(match[3].str());
                    double z = std::stod(match[4].str());
                    std::string resetValue = (match.size() > 5 && match[5].matched) ? match[5].str() : "";

                    // Round only the integer part for grid coordinates
                    int gridX = static_cast<int>(std::floor(x / 8192.0));
                    int gridY = static_cast<int>(std::floor(y / 8192.0));

                    // Check if coordinate is valid (in DB or customCoordinates)
                    if (isCoordinateValid(db, gridX, gridY, customCoordinates)) {
                        int newGridX = gridX + offset.offsetX;
                        int newGridY = gridY + offset.offsetY;

                        logMessage("Found Script AI Travel translation: (" + std::to_string(gridX) + ", " +
                            std::to_string(gridY) + ") -> (" + std::to_string(newGridX) + ", " +
                            std::to_string(newGridY) + ")");

                        // New calculation keeping the fractional part
                        double newX = (newGridX * 8192.0) + (x - (gridX * 8192.0));
                        double newY = (newGridY * 8192.0) + (y - (gridY * 8192.0));

                        logMessage("Calculated new coordinates: (" + std::to_string(x) + ", " +
                            std::to_string(y) + ") -> (" + std::to_string(newX) + ", " +
                            std::to_string(newY) + ")");

                        // Mark replacement in replacements
                        replacementsFlag = 1;
                        scriptUpdated = true;

                        // Form the updated command string
                        std::ostringstream formattedCommand;
                        formattedCommand << std::fixed << std::setprecision(3);  // Set precision
                        formattedCommand << commandType << ", " << newX << ", " << newY << ", " << z;

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
void processDialogueAiTravelTranslation(sqlite3* db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates) {

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
                    double x = std::stod(match[2].str());
                    double y = std::stod(match[3].str());
                    double z = std::stod(match[4].str());
                    std::string resetValue = (match.size() > 5 && match[5].matched) ? match[5].str() : "";

                    // Round only the integer part for grid coordinates
                    int gridX = static_cast<int>(std::floor(x / 8192.0));
                    int gridY = static_cast<int>(std::floor(y / 8192.0));

                    // Check if coordinate is valid (in DB or customCoordinates)
                    if (isCoordinateValid(db, gridX, gridY, customCoordinates)) {
                        int newGridX = gridX + offset.offsetX;
                        int newGridY = gridY + offset.offsetY;

                        logMessage("Found Dialogue AI Travel translation: (" + std::to_string(gridX) + ", " +
                            std::to_string(gridY) + ") -> (" + std::to_string(newGridX) + ", " +
                            std::to_string(newGridY) + ")");

                        // New calculation keeping the fractional part
                        double newX = (newGridX * 8192.0) + (x - (gridX * 8192.0));
                        double newY = (newGridY * 8192.0) + (y - (gridY * 8192.0));

                        logMessage("Calculated new coordinates: (" + std::to_string(x) + ", " +
                            std::to_string(y) + ") -> (" + std::to_string(newX) + ", " +
                            std::to_string(newY) + ")");

                        // Mark replacement in replacements
                        replacementsFlag = 1;

                        // Form the updated command string
                        std::ostringstream formattedCommand;
                        formattedCommand << std::fixed << std::setprecision(3);
                        formattedCommand << commandType << ", " << newX << ", " << newY << ", " << z;

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
void processScriptPositionTranslation(sqlite3* db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, std::vector<std::string>& updatedScriptIDs, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates) {

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
                    double x = std::stod(match[2].str());
                    double y = std::stod(match[3].str());
                    double z = std::stod(match[4].str());
                    double zRot = std::stod(match[5].str());

                    // Round only the integer part for grid coordinates
                    int gridX = static_cast<int>(std::floor(x / 8192.0));
                    int gridY = static_cast<int>(std::floor(y / 8192.0));

                    // Check if coordinate is valid (in DB or customCoordinates)
                    if (isCoordinateValid(db, gridX, gridY, customCoordinates)) {
                        int newGridX = gridX + offset.offsetX;
                        int newGridY = gridY + offset.offsetY;

                        logMessage("Found Script Position translation: (" + std::to_string(gridX) + ", " +
                            std::to_string(gridY) + ") -> (" + std::to_string(newGridX) + ", " +
                            std::to_string(newGridY) + ")");

                        // New calculation keeping the fractional part
                        double newX = (newGridX * 8192.0) + (x - (gridX * 8192.0));
                        double newY = (newGridY * 8192.0) + (y - (gridY * 8192.0));

                        logMessage("Calculated new coordinates: (" + std::to_string(x) + ", " +
                            std::to_string(y) + ") -> (" + std::to_string(newX) + ", " +
                            std::to_string(newY) + ")");

                        // Mark replacement in replacements
                        replacementsFlag = 1;
                        scriptUpdated = true;

                        // Form the updated command string
                        std::ostringstream formattedCommand;
                        formattedCommand << std::fixed << std::setprecision(3);
                        formattedCommand << commandType << ", " << newX << ", " << newY << ", " << z;
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
void processDialoguePositionTranslation(sqlite3* db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates) {

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
                    double x = std::stod(match[2].str());
                    double y = std::stod(match[3].str());
                    double z = std::stod(match[4].str());
                    double zRot = std::stod(match[5].str());

                    // Round only the integer part for grid coordinates
                    int gridX = static_cast<int>(std::floor(x / 8192.0));
                    int gridY = static_cast<int>(std::floor(y / 8192.0));

                    // Check if coordinate is valid (in DB or customCoordinates)
                    if (isCoordinateValid(db, gridX, gridY, customCoordinates)) {
                        int newGridX = gridX + offset.offsetX;
                        int newGridY = gridY + offset.offsetY;

                        logMessage("Found Dialogue Position translation: (" + std::to_string(gridX) + ", " +
                            std::to_string(gridY) + ") -> (" + std::to_string(newGridX) + ", " +
                            std::to_string(newGridY) + ")");

                        // New calculation keeping the fractional part
                        double newX = (newGridX * 8192.0) + (x - (gridX * 8192.0));
                        double newY = (newGridY * 8192.0) + (y - (gridY * 8192.0));

                        logMessage("Calculated new coordinates: (" + std::to_string(x) + ", " +
                            std::to_string(y) + ") -> (" + std::to_string(newX) + ", " +
                            std::to_string(newY) + ")");

                        // Mark replacement in replacements
                        replacementsFlag = 1;

                        // Form the updated command string
                        std::ostringstream formattedCommand;
                        formattedCommand << std::fixed << std::setprecision(3);
                        formattedCommand << commandType << ", " << newX << ", " << newY << ", " << z;
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
void processScriptPositionCellTranslation(sqlite3* db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, std::vector<std::string>& updatedScriptIDs, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates) {

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
                    double x = std::stod(match[2].str());
                    double y = std::stod(match[3].str());
                    double z = std::stod(match[4].str());
                    double zRot = std::stod(match[5].str());
                    std::string cellID = match[6].str();  // Cell name (with escaped quotes)

                    // Round only the integer part for grid coordinates
                    int gridX = static_cast<int>(std::floor(x / 8192.0));
                    int gridY = static_cast<int>(std::floor(y / 8192.0));

                    // Check if coordinate is valid (in DB or customCoordinates)
                    if (isCoordinateValid(db, gridX, gridY, customCoordinates)) {
                        int newGridX = gridX + offset.offsetX;
                        int newGridY = gridY + offset.offsetY;

                        logMessage("Found Script PositionCell translation: (" + std::to_string(gridX) + ", " +
                            std::to_string(gridY) + ") -> (" + std::to_string(newGridX) + ", " +
                            std::to_string(newGridY) + ")");

                        // New calculation keeping the fractional part
                        double newX = (newGridX * 8192.0) + (x - (gridX * 8192.0));
                        double newY = (newGridY * 8192.0) + (y - (gridY * 8192.0));

                        logMessage("Calculated new coordinates: (" + std::to_string(x) + ", " +
                            std::to_string(y) + ") -> (" + std::to_string(newX) + ", " +
                            std::to_string(newY) + ")");

                        // Mark replacement in replacements
                        replacementsFlag = 1;
                        scriptUpdated = true;

                        // Form the updated command string
                        std::ostringstream formattedCommand;
                        formattedCommand << std::fixed << std::setprecision(3);
                        formattedCommand << commandType << ", " << newX << ", " << newY << ", " << z;
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
void processDialoguePositionCellTranslation(sqlite3* db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates) {

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
                    double x = std::stod(match[2].str());
                    double y = std::stod(match[3].str());
                    double z = std::stod(match[4].str());
                    double zRot = std::stod(match[5].str());
                    std::string cellID = match[6].str();  // Cell name (with escaped quotes)

                    // Round only the integer part for grid coordinates
                    int gridX = static_cast<int>(std::floor(x / 8192.0));
                    int gridY = static_cast<int>(std::floor(y / 8192.0));

                    // Check if coordinate is valid (in DB or customCoordinates)
                    if (isCoordinateValid(db, gridX, gridY, customCoordinates)) {
                        int newGridX = gridX + offset.offsetX;
                        int newGridY = gridY + offset.offsetY;

                        logMessage("Found Dialogue PositionCell translation: (" + std::to_string(gridX) + ", " +
                            std::to_string(gridY) + ") -> (" + std::to_string(newGridX) + ", " +
                            std::to_string(newGridY) + ")");

                        // New calculation keeping the fractional part
                        double newX = (newGridX * 8192.0) + (x - (gridX * 8192.0));
                        double newY = (newGridY * 8192.0) + (y - (gridY * 8192.0));

                        logMessage("Calculated new coordinates: (" + std::to_string(x) + ", " +
                            std::to_string(y) + ") -> (" + std::to_string(newX) + ", " +
                            std::to_string(newY) + ")");

                        // Mark replacement in replacements
                        replacementsFlag = 1;

                        // Form the updated command string
                        std::ostringstream formattedCommand;
                        formattedCommand << std::fixed << std::setprecision(3);
                        formattedCommand << commandType << ", " << newX << ", " << newY << ", " << z;
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
void processScriptPlaceItemTranslation(sqlite3* db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, std::vector<std::string>& updatedScriptIDs, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates) {

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
                    double x = std::stod(match[3].str());
                    double y = std::stod(match[4].str());
                    double z = std::stod(match[5].str());
                    double zRot = std::stod(match[6].str());

                    // Round only the integer part for grid coordinates
                    int gridX = static_cast<int>(std::floor(x / 8192.0));
                    int gridY = static_cast<int>(std::floor(y / 8192.0));

                    // Check if coordinate is valid (in DB or customCoordinates)
                    if (isCoordinateValid(db, gridX, gridY, customCoordinates)) {
                        int newGridX = gridX + offset.offsetX;
                        int newGridY = gridY + offset.offsetY;

                        logMessage("Found Script PlaceItem translation: (" + std::to_string(gridX) + ", " +
                            std::to_string(gridY) + ") -> (" + std::to_string(newGridX) + ", " +
                            std::to_string(newGridY) + ")");

                        // New calculation keeping the fractional part
                        double newX = (newGridX * 8192.0) + (x - (gridX * 8192.0));
                        double newY = (newGridY * 8192.0) + (y - (gridY * 8192.0));

                        logMessage("Calculated new coordinates: (" + std::to_string(x) + ", " +
                            std::to_string(y) + ") -> (" + std::to_string(newX) + ", " +
                            std::to_string(newY) + ")");

                        // Mark replacement in replacements
                        replacementsFlag = 1;
                        scriptUpdated = true;

                        // Form the updated command string
                        std::ostringstream formattedCommand;
                        formattedCommand << std::fixed << std::setprecision(3);
                        formattedCommand << commandType << ", " << objectID << ", " << newX << ", " << newY << ", " << z;
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
void processDialoguePlaceItemTranslation(sqlite3* db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates) {

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
                    double x = std::stod(match[3].str());
                    double y = std::stod(match[4].str());
                    double z = std::stod(match[5].str());
                    double zRot = std::stod(match[6].str());

                    // Round only the integer part for grid coordinates
                    int gridX = static_cast<int>(std::floor(x / 8192.0));
                    int gridY = static_cast<int>(std::floor(y / 8192.0));

                    // Check if coordinate is valid (in DB or customCoordinates)
                    if (isCoordinateValid(db, gridX, gridY, customCoordinates)) {
                        int newGridX = gridX + offset.offsetX;
                        int newGridY = gridY + offset.offsetY;

                        logMessage("Found Dialogue PlaceItem translation: (" + std::to_string(gridX) + ", " +
                            std::to_string(gridY) + ") -> (" + std::to_string(newGridX) + ", " +
                            std::to_string(newGridY) + ")");

                        // New calculation keeping the fractional part
                        double newX = (newGridX * 8192.0) + (x - (gridX * 8192.0));
                        double newY = (newGridY * 8192.0) + (y - (gridY * 8192.0));

                        logMessage("Calculated new coordinates: (" + std::to_string(x) + ", " +
                            std::to_string(y) + ") -> (" + std::to_string(newX) + ", " +
                            std::to_string(newY) + ")");

                        // Mark replacement in replacements
                        replacementsFlag = 1;

                        // Form the updated command string
                        std::ostringstream formattedCommand;
                        formattedCommand << std::fixed << std::setprecision(3);
                        formattedCommand << commandType << ", " << objectID << ", " << newX << ", " << newY << ", " << z;
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
void processScriptPlaceItemCellTranslation(sqlite3* db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, std::vector<std::string>& updatedScriptIDs, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates) {

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
                    double x = std::stod(match[4].str());
                    double y = std::stod(match[5].str());
                    double z = std::stod(match[6].str());
                    double zRot = std::stod(match[7].str());

                    // Round only the integer part for grid coordinates
                    int gridX = static_cast<int>(std::floor(x / 8192.0));
                    int gridY = static_cast<int>(std::floor(y / 8192.0));

                    // Check if coordinate is valid (in DB or customCoordinates)
                    if (isCoordinateValid(db, gridX, gridY, customCoordinates)) {
                        int newGridX = gridX + offset.offsetX;
                        int newGridY = gridY + offset.offsetY;

                        logMessage("Found Script PlaceItemCell translation: (" + std::to_string(gridX) + ", " +
                            std::to_string(gridY) + ") -> (" + std::to_string(newGridX) + ", " +
                            std::to_string(newGridY) + ")");

                        // New calculation keeping the fractional part
                        double newX = (newGridX * 8192.0) + (x - (gridX * 8192.0));
                        double newY = (newGridY * 8192.0) + (y - (gridY * 8192.0));

                        logMessage("Calculated new coordinates: (" + std::to_string(x) + ", " +
                            std::to_string(y) + ") -> (" + std::to_string(newX) + ", " +
                            std::to_string(newY) + ")");

                        // Mark replacement in replacements
                        replacementsFlag = 1;
                        scriptUpdated = true;

                        // Form the updated command string
                        std::ostringstream formattedCommand;
                        formattedCommand << std::fixed << std::setprecision(3);
                        formattedCommand << commandType << ", " << objectID << ", " << cellID << ", " << newX << ", " << newY << ", " << z;
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
void processDialoguePlaceItemCellTranslation(sqlite3* db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates) {

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
                    double x = std::stod(match[4].str());
                    double y = std::stod(match[5].str());
                    double z = std::stod(match[6].str());
                    double zRot = std::stod(match[7].str());

                    // Round only the integer part for grid coordinates
                    int gridX = static_cast<int>(std::floor(x / 8192.0));
                    int gridY = static_cast<int>(std::floor(y / 8192.0));

                    // Check if coordinate is valid (in DB or customCoordinates)
                    if (isCoordinateValid(db, gridX, gridY, customCoordinates)) {
                        int newGridX = gridX + offset.offsetX;
                        int newGridY = gridY + offset.offsetY;

                        logMessage("Found Dialogue PlaceItemCell translation: (" + std::to_string(gridX) + ", " +
                            std::to_string(gridY) + ") -> (" + std::to_string(newGridX) + ", " +
                            std::to_string(newGridY) + ")");

                        // New calculation keeping the fractional part
                        double newX = (newGridX * 8192.0) + (x - (gridX * 8192.0));
                        double newY = (newGridY * 8192.0) + (y - (gridY * 8192.0));

                        logMessage("Calculated new coordinates: (" + std::to_string(x) + ", " +
                            std::to_string(y) + ") -> (" + std::to_string(newX) + ", " +
                            std::to_string(newY) + ")");

                        // Mark replacement in replacements
                        replacementsFlag = 1;

                        // Form the updated command string
                        std::ostringstream formattedCommand;
                        formattedCommand << std::fixed << std::setprecision(3);
                        formattedCommand << commandType << ", " << objectID << ", " << cellID << ", " << newX << ", " << newY << ", " << z;
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
void processTranslation(ordered_json& jsonData, const GridOffset& offset, int& replacementsFlag) {
    // Check if the 'references' key exists and is an array
    if (!jsonData.contains("references") || !jsonData["references"].is_array()) {
        logMessage("References key is missing or is not an array in JSON.");
        return;
    }

    // Loop through each reference in the references array
    for (auto& reference : jsonData["references"]) {
        // Check if the reference contains "temporary" and "translation", and if translation is an array of at least 2 elements
        if (reference.contains("temporary") &&
            reference.contains("translation") &&
            reference["translation"].is_array() &&
            reference["translation"].size() >= 2) {

            logMessage("Processing reference: " + reference.value("id", "Unknown ID"));

            // Log the original translation values before update
            double originalX = reference["translation"][0].get<double>();
            double originalY = reference["translation"][1].get<double>();
            logMessage("Found translation: X=" + std::to_string(originalX) + ", Y=" + std::to_string(originalY));

            // Apply the offset to the X and Y values (multiplied by 8192 for scaling)
            reference["translation"][0] = originalX + offset.offsetX * 8192;
            reference["translation"][1] = originalY + offset.offsetY * 8192;

            // Mark that a replacement has been made
            replacementsFlag = 1;

            // Log the updated translation values after modification
            double updatedX = reference["translation"][0].get<double>();
            double updatedY = reference["translation"][1].get<double>();
            logMessage("Calculated new coordinates: X=" + std::to_string(updatedX) + ", Y=" + std::to_string(updatedY));
        }
        else {
            logMessage("No valid temporary or translation array found in reference: " + reference.value("id", "Unknown ID"));
        }
    }
}

// Function to process coordinates for Cell, Landscape, and PathGrid types
void processGridValues(sqlite3* db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates) {

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
            logMessage("Grid key is missing for type: " + typeName);
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
        if (isCoordinateValid(db, gridX, gridY, customCoordinates)) {
            int newGridX = gridX + offset.offsetX;
            int newGridY = gridY + offset.offsetY;

            logMessage("Updated grid coordinates for (" + typeName + "): (" + std::to_string(gridX) + ", " + std::to_string(gridY) +
                ") -> (" + std::to_string(newGridX) + ", " + std::to_string(newGridY) + ")");

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
                processTranslation(item, offset, replacementsFlag);
            }

            // Mark that a replacement has been made
            replacementsFlag = 1;

        }
        else {
            logMessage("No matches found in database for: " + typeName + " (" + std::to_string(gridX) + ", " + std::to_string(gridY)+")");
        }
    }
}

// Function to log updated script IDs
void logUpdatedScriptIDs(const std::vector<std::string>& updatedScriptIDs) {
    if (updatedScriptIDs.empty()) {
        logMessage("\nNo scripts were updated.");
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

    logMessage("\nUpdated scripts list:");
    for (const auto& id : uniqueIDs) {
        logMessage("- Script ID: " + id);
    }
}

// Saves modified JSON data to file and logs success message
bool saveJsonToFile(const std::filesystem::path& jsonFilePath, const ordered_json& inputData) {
    std::ofstream outputFile(jsonFilePath);
    if (outputFile) {
        outputFile << std::setw(2) << inputData;
        logMessage("\nModified JSON saved as: " + jsonFilePath.string() + "\n");
        return true;
    }
    return false;
}

// Executes command to convert JSON file to ESP/ESM format and logs success or failure
bool convertJsonToEsp(const std::filesystem::path& jsonFilePath, const std::filesystem::path& espFilePath) {
    std::string command = "tes3conv.exe \"" + jsonFilePath.string() + "\" \"" + espFilePath.string() + "\"";
    if (std::system(command.c_str()) != 0) {
        return false;
    }
    logMessage("Final conversion to ESM/ESP successful: " + espFilePath.string() + "\n");
    return true;
}

// Main function
int main() {
    // Display program information
    std::cout << PROGRAM_NAME << "\n" << PROGRAM_VERSION << "\n" << PROGRAM_AUTHOR << "\n\n";

    // Clear previous log entries
    clearLogFile("tes3_ab_log.txt");

    // Check if database file exists
    std::filesystem::path dbFilePath = "tes3_ab_cell_x-y_data.db";
    if (!std::filesystem::exists(dbFilePath)) {
        logErrorAndExit(nullptr, "Database file 'tes3_ab_cell_x-y_data.db' not found.\n");
    }

    sqlite3* db = nullptr;

    // Attempt to open database
    if (sqlite3_open(dbFilePath.string().c_str(), &db)) {
        logErrorAndExit(db, "Failed to open database: " + std::string(sqlite3_errmsg(db)) + "\n");
    }
    logMessage("Database opened successfully...");

    // Check if the custom grid coordinates file exists
    std::filesystem::path customFilePath = "tes3_ab_custom_cell_x-y_data.txt";
    if (!std::filesystem::exists(customFilePath)) {
        logErrorAndExit(nullptr, "Custom grid coordinates file 'tes3_ab_custom_cell_x-y_data.txt' not found.\n");
    }

    // Attempt to load the custom grid coordinates
    std::unordered_set<std::pair<int, int>, PairHash> customCoordinates;
    loadCustomGridCoordinates(customFilePath.string(), customCoordinates);
    logMessage("Custom grid coordinates loaded successfully...");

    // Check if tes3conv.exe exists
    std::filesystem::path converterPath = "tes3conv.exe";
    if (!std::filesystem::exists(converterPath)) {
        logErrorAndExit(db, "tes3conv.exe not found. Please download the latest version from\n"
            "https://github.com/Greatness7/tes3conv/releases and place it in\nthe same directory with this program.\n");
    }
    logMessage("tes3conv.exe found...\nInitialisation complete.\n");

    // Get conversion choice from user
    int ConversionChoice = getUserConversionChoice();

    // Get input file path from user
    std::filesystem::path inputFilePath = getInputFilePath();
    std::filesystem::path inputPath(inputFilePath);

    // Define output paths
    std::filesystem::path outputDir = inputPath.parent_path();
    std::filesystem::path jsonFilePath = outputDir / (inputPath.stem() += ".json");

    // Convert the input file to JSON using tes3conv.exe
    std::string command = "tes3conv.exe \"" + inputPath.string() + "\" \"" + jsonFilePath.string() + "\"";
    if (std::system(command.c_str()) != 0) {
        logErrorAndExit(db, "Error converting to JSON. Check tes3conv.exe and the input file.\n");
    }
    logMessage("Conversion to JSON successful: " + jsonFilePath.string());

    // Load the generated JSON file into a JSON object
    std::ifstream inputFile(jsonFilePath);
    if (!inputFile) {
        logErrorAndExit(db, "Failed to open JSON file for reading: " + jsonFilePath.string() + "\n");
    }
    ordered_json inputData;
    inputFile >> inputData;
    inputFile.close();

    // Check if the required dependencies are ordered correctly in the input data
    auto [isValid, validMastersDB] = checkDependencyOrder(inputData);
    if (!isValid) {
        // Remove the temporary JSON file if it exists
        if (std::filesystem::exists(jsonFilePath)) {
            std::filesystem::remove(jsonFilePath);
            logMessage("Temporary JSON file deleted: " + jsonFilePath.string() + "\n");
        }
        logErrorAndExit(db, "Required Parent Masters not found or are in the wrong order.\n");
    }

    // Retrieve the grid offset based on the conversion choice
    GridOffset offset = getGridOffset(ConversionChoice);

    // Prepare replacements for possible undo of the conversion
    int replacementsFlag = 0;

    // Vector to store the IDs of scripts that were updated during processing
    std::vector<std::string> updatedScriptIDs;

    // Search for data and make replacements
    processGridValues(db, inputData, offset, replacementsFlag, customCoordinates);
    processInterriorDoorsTranslation(db, inputData, offset, replacementsFlag, customCoordinates);
    processNpcTravelDestinations(db, inputData, offset, replacementsFlag, customCoordinates);

    processScriptAiEscortTranslation(db, inputData, offset, replacementsFlag, updatedScriptIDs, customCoordinates);
    processScriptAiEscortCellTranslation(db, inputData, offset, replacementsFlag, updatedScriptIDs, customCoordinates);
    processScriptAiFollowTranslation(db, inputData, offset, replacementsFlag, updatedScriptIDs, customCoordinates);
    processScriptAiFollowCellTranslation(db, inputData, offset, replacementsFlag, updatedScriptIDs, customCoordinates);
    processScriptAiTravelTranslation(db, inputData, offset, replacementsFlag, updatedScriptIDs, customCoordinates);
    processScriptPositionTranslation(db, inputData, offset, replacementsFlag, updatedScriptIDs, customCoordinates);
    processScriptPositionCellTranslation(db, inputData, offset, replacementsFlag, updatedScriptIDs, customCoordinates);
    processScriptPlaceItemTranslation(db, inputData, offset, replacementsFlag, updatedScriptIDs, customCoordinates);
    processScriptPlaceItemCellTranslation(db, inputData, offset, replacementsFlag, updatedScriptIDs, customCoordinates);

    processDialogueAiEscortTranslation(db, inputData, offset, replacementsFlag, customCoordinates);
    processDialogueAiEscortCellTranslation(db, inputData, offset, replacementsFlag, customCoordinates);
    processDialogueAiFollowTranslation(db, inputData, offset, replacementsFlag, customCoordinates);
    processDialogueAiFollowCellTranslation(db, inputData, offset, replacementsFlag, customCoordinates);
    processDialogueAiTravelTranslation(db, inputData, offset, replacementsFlag, customCoordinates);
    processDialoguePositionTranslation(db, inputData, offset, replacementsFlag, customCoordinates);
    processDialoguePositionCellTranslation(db, inputData, offset, replacementsFlag, customCoordinates);
    processDialoguePlaceItemTranslation(db, inputData, offset, replacementsFlag, customCoordinates);
    processDialoguePlaceItemCellTranslation(db, inputData, offset, replacementsFlag, customCoordinates);

    // If no replacements were found, cancel the conversion
    if (replacementsFlag == 0) {
        // Remove the temporary JSON file
        if (std::filesystem::exists(jsonFilePath)) {
            std::filesystem::remove(jsonFilePath);
            logMessage("Temporary JSON file deleted: " + jsonFilePath.string() + "\n");
        }
        logErrorAndExit(db, "No replacements found. Conversion canceled.\n");
    }

    // Log updated script IDs
    logUpdatedScriptIDs(updatedScriptIDs);

    // Save the modified JSON data to a new file using the saveJsonToFile function
    std::filesystem::path newJsonFilePath = outputDir / ("CONV_" + std::string(ConversionChoice == 1 ? "BMtoAS" : "AStoBM") + "_" + inputPath.stem().string() + ".json");

    if (!saveJsonToFile(newJsonFilePath, inputData)) {
        logErrorAndExit(db, "Error saving modified JSON file.\n");
    }

    // Convert the JSON back to ESP/ESM format
    std::filesystem::path outputExtension = inputPath.extension();
    std::filesystem::path newEspPath = outputDir / ("CONV_" + std::string(ConversionChoice == 1 ? "BMtoAS" : "AStoBM") + "_" + inputPath.stem().string() + outputExtension.string());

    if (!convertJsonToEsp(newJsonFilePath, newEspPath)) {
        logErrorAndExit(db, "Error converting JSON back to ESM/ESP.\n");
    }

    // Delete both JSON files if conversion succeeds
    if (std::filesystem::exists(jsonFilePath)) std::filesystem::remove(jsonFilePath);
    if (std::filesystem::exists(newJsonFilePath)) std::filesystem::remove(newJsonFilePath);
    logMessage("Temporary JSON files deleted: " + jsonFilePath.string() + "\n                         and: " + newJsonFilePath.string() + "\n");

    // Close the database and finish execution
    sqlite3_close(db);
    logMessage("The ending of the words is ALMSIVI.\n");

    // Wait for the Enter key to finish
    std::cout << "Press Enter to continue...";
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    return 0;
}