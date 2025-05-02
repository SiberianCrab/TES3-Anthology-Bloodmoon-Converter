#include "ab_data_processor.h"
#include "ab_logger.h"

// Function to process translations for interior door coordinates
void processInteriorDoorsTranslation(const Database& db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates, const ProgramOptions& options, std::ofstream& logFile) {

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
                            if (isCoordinateValid(db, gridX, gridY, customCoordinates, options, logFile)) {
                                int newGridX = gridX + offset.offsetX;
                                int newGridY = gridY + offset.offsetY;

                                if (!options.silentMode) {
                                    logMessage("Found: Interior Door translation -> grid (" + std::to_string(gridX) + ", " + std::to_string(gridY) +
                                               ") | coordinates (" + std::to_string(destX) + ", " + std::to_string(destY) + ")", logFile);
                                }

                                // New calculation keeping the fractional part for destination translation
                                double newDestX = (newGridX * 8192.0) + (destX - (gridX * 8192.0));
                                double newDestY = (newGridY * 8192.0) + (destY - (gridY * 8192.0));

                                // Mark the replacement in replacements
                                replacementsFlag = 1;

                                // Save to original fields
                                reference["destination"]["translation"][0] = newDestX;
                                reference["destination"]["translation"][1] = newDestY;

                                if (!options.silentMode) {
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
}

// Function to process NPC Travel Service coordinates
void processNpcTravelDestinations(const Database& db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates, const ProgramOptions& options, std::ofstream& logFile) {

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
                        if (isCoordinateValid(db, gridX, gridY, customCoordinates, options, logFile)) {
                            int newGridX = gridX + offset.offsetX;
                            int newGridY = gridY + offset.offsetY;

                            if (!options.silentMode) {
                                logMessage("Found: NPC 'Travel Service' translation -> grid (" + std::to_string(gridX) + ", " + std::to_string(gridY) +
                                           ") | coordinates (" + std::to_string(destX) + ", " + std::to_string(destY) + ")", logFile);
                            }

                            // New calculation keeping the fractional part for destination coordinates
                            double newDestX = (newGridX * 8192.0) + (destX - (gridX * 8192.0));
                            double newDestY = (newGridY * 8192.0) + (destY - (gridY * 8192.0));

                            // Mark the replacement in replacements
                            replacementsFlag = 1;

                            // Save to original fields
                            destination["translation"][0] = newDestX;
                            destination["translation"][1] = newDestY;

                            if (!options.silentMode) {
                                logMessage("Calculating: new destination ------------> grid (" + std::to_string(newGridX) + ", " + std::to_string(newGridY) +
                                           ") | coordinates (" + std::to_string(newDestX) + ", " + std::to_string(newDestY) + ")", logFile);
                            }
                        }
                    }
                }
            }
        }
    }
}

// Function to process Script AI Escort translation
void processScriptAiEscortTranslation(const Database& db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, std::vector<std::string>& updatedScriptIDs, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates, const ProgramOptions& options, std::ofstream& logFile) {

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
                    if (isCoordinateValid(db, gridX, gridY, customCoordinates, options, logFile)) {
                        int newGridX = gridX + offset.offsetX;
                        int newGridY = gridY + offset.offsetY;

                        if (!options.silentMode) {
                            logMessage("Found: Script 'AI Escort' translation -> grid (" + std::to_string(gridX) + ", " + std::to_string(gridY) +
                                       ") | coordinates (" + std::to_string(destX) + ", " + std::to_string(destY) + ")", logFile);
                        }

                        // New calculation keeping the fractional part
                        double newDestX = (newGridX * 8192.0) + (destX - (gridX * 8192.0));
                        double newDestY = (newGridY * 8192.0) + (destY - (gridY * 8192.0));

                        if (!options.silentMode) {
                            logMessage("Calculating: new destination ----------> grid (" + std::to_string(newGridX) + ", " + std::to_string(newGridY) +
                                       ") | coordinates (" + std::to_string(newDestX) + ", " + std::to_string(newDestY) + ")", logFile);
                        }

                        // Mark replacement in replacements
                        replacementsFlag = 1;
                        scriptUpdated = true;

                        // Form the updated command string
                        std::ostringstream formattedCommand;
                        formattedCommand << std::fixed << std::setprecision(3);
                        formattedCommand << commandType << ", " << actorID << ", " << duration << ", "
                                         << newDestX << ", " << newDestY << ", " << destZ;

                        if (!resetValue.empty()) {
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
void processDialogueAiEscortTranslation(const Database& db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates, const ProgramOptions& options, std::ofstream& logFile) {

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
                    if (isCoordinateValid(db, gridX, gridY, customCoordinates, options, logFile)) {
                        int newGridX = gridX + offset.offsetX;
                        int newGridY = gridY + offset.offsetY;

                        if (!options.silentMode) {
                            logMessage("Found: Dialogue 'AI Escort' translation -> grid (" + std::to_string(gridX) + ", " + std::to_string(gridY) +
                                       ") | coordinates (" + std::to_string(destX) + ", " + std::to_string(destY) + ")", logFile);
                        }

                        // New calculation keeping the fractional part
                        double newDestX = (newGridX * 8192.0) + (destX - (gridX * 8192.0));
                        double newDestY = (newGridY * 8192.0) + (destY - (gridY * 8192.0));

                        if (!options.silentMode) {
                            logMessage("Calculating: new destination ------------> grid (" + std::to_string(newGridX) + ", " + std::to_string(newGridY) +
                                       ") | coordinates (" + std::to_string(newDestX) + ", " + std::to_string(newDestY) + ")", logFile);
                        }

                        // Mark replacement in replacements
                        replacementsFlag = 1;

                        // Form the updated command string
                        std::ostringstream formattedCommand;
                        formattedCommand << std::fixed << std::setprecision(3);
                        formattedCommand << commandType << ", " << actorID << ", " << duration << ", "
                                         << newDestX << ", " << newDestY << ", " << destZ;

                        if (!resetValue.empty()) {
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
void processScriptAiEscortCellTranslation(const Database& db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, std::vector<std::string>& updatedScriptIDs, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates, const ProgramOptions& options, std::ofstream& logFile) {

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
                    if (isCoordinateValid(db, gridX, gridY, customCoordinates, options, logFile)) {
                        int newGridX = gridX + offset.offsetX;
                        int newGridY = gridY + offset.offsetY;

                        if (!options.silentMode) {
                            logMessage("Found: Script 'AI Escort Cell' translation -> grid (" + std::to_string(gridX) + ", " + std::to_string(gridY) +
                                       ") | coordinates (" + std::to_string(destX) + ", " + std::to_string(destY) + ")", logFile);
                        }

                        // New calculation keeping the fractional part
                        double newDestX = (newGridX * 8192.0) + (destX - (gridX * 8192.0));
                        double newDestY = (newGridY * 8192.0) + (destY - (gridY * 8192.0));

                        if (!options.silentMode) {
                            logMessage("Calculating: new destination ---------------> grid (" + std::to_string(newGridX) + ", " + std::to_string(newGridY) +
                                       ") | coordinates (" + std::to_string(newDestX) + ", " + std::to_string(newDestY) + ")", logFile);
                        }

                        // Mark replacement in replacements
                        replacementsFlag = 1;
                        scriptUpdated = true;

                        // Form the updated command string
                        std::ostringstream formattedCommand;
                        formattedCommand << std::fixed << std::setprecision(3);
                        formattedCommand << commandType << ", " << actorID << ", " << cellID << ", " << duration << ", "
                                         << newDestX << ", " << newDestY << ", " << destZ;

                        if (!resetValue.empty()) {
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
void processDialogueAiEscortCellTranslation(const Database& db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates, const ProgramOptions& options, std::ofstream& logFile) {

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
                    if (isCoordinateValid(db, gridX, gridY, customCoordinates, options, logFile)) {
                        int newGridX = gridX + offset.offsetX;
                        int newGridY = gridY + offset.offsetY;

                        if (!options.silentMode) {
                            logMessage("Found: Dialogue 'AI Escort Cell' translation -> grid (" + std::to_string(gridX) + ", " + std::to_string(gridY) +
                                       ") | coordinates (" + std::to_string(destX) + ", " + std::to_string(destY) + ")", logFile);
                        }

                        // New calculation keeping the fractional part
                        double newDestX = (newGridX * 8192.0) + (destX - (gridX * 8192.0));
                        double newDestY = (newGridY * 8192.0) + (destY - (gridY * 8192.0));

                        if (!options.silentMode) {
                            logMessage("Calculating: new destination -----------------> grid (" + std::to_string(newGridX) + ", " + std::to_string(newGridY) +
                                       ") | coordinates (" + std::to_string(newDestX) + ", " + std::to_string(newDestY) + ")", logFile);
                        }

                        // Mark replacement in replacements
                        replacementsFlag = 1;

                        // Form the updated command string
                        std::ostringstream formattedCommand;
                        formattedCommand << std::fixed << std::setprecision(3);
                        formattedCommand << commandType << ", " << actorID << ", " << cellID << ", " << duration << ", "
                                         << newDestX << ", " << newDestY << ", " << destZ;

                        if (!resetValue.empty()) {
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
void processScriptAiFollowTranslation(const Database& db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, std::vector<std::string>& updatedScriptIDs, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates, const ProgramOptions& options, std::ofstream& logFile) {

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
                    if (isCoordinateValid(db, gridX, gridY, customCoordinates, options, logFile)) {
                        int newGridX = gridX + offset.offsetX;
                        int newGridY = gridY + offset.offsetY;

                        if (!options.silentMode) {
                            logMessage("Found: Script 'AI Follow' translation -> grid (" + std::to_string(gridX) + ", " + std::to_string(gridY) +
                                       ") | coordinates (" + std::to_string(destX) + ", " + std::to_string(destY) + ")", logFile);
                        }

                        // New calculation keeping the fractional part
                        double newDestX = (newGridX * 8192.0) + (destX - (gridX * 8192.0));
                        double newDestY = (newGridY * 8192.0) + (destY - (gridY * 8192.0));

                        if (!options.silentMode) {
                            logMessage("Calculating: new destination ----------> grid (" + std::to_string(newGridX) + ", " + std::to_string(newGridY) +
                                       ") | coordinates (" + std::to_string(newDestX) + ", " + std::to_string(newDestY) + ")", logFile);
                        }

                        // Mark replacement in replacements
                        replacementsFlag = 1;
                        scriptUpdated = true;

                        // Form the updated command string
                        std::ostringstream formattedCommand;
                        formattedCommand << std::fixed << std::setprecision(3);
                        formattedCommand << commandType << ", " << actorID << ", " << duration << ", "
                                         << newDestX << ", " << newDestY << ", " << destZ;

                        if (!resetValue.empty()) {
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
void processDialogueAiFollowTranslation(const Database& db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates, const ProgramOptions& options, std::ofstream& logFile) {

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
                    if (isCoordinateValid(db, gridX, gridY, customCoordinates, options, logFile)) {
                        int newGridX = gridX + offset.offsetX;
                        int newGridY = gridY + offset.offsetY;

                        if (!options.silentMode) {
                            logMessage("Found: Dialogue 'AI Follow' translation -> grid (" + std::to_string(gridX) + ", " + std::to_string(gridY) +
                                       ") | coordinates (" + std::to_string(destX) + ", " + std::to_string(destY) + ")", logFile);
                        }

                        // New calculation keeping the fractional part
                        double newDestX = (newGridX * 8192.0) + (destX - (gridX * 8192.0));
                        double newDestY = (newGridY * 8192.0) + (destY - (gridY * 8192.0));

                        if (!options.silentMode) {
                            logMessage("Calculating: new destination ------------> grid (" + std::to_string(newGridX) + ", " + std::to_string(newGridY) +
                                       ") | coordinates (" + std::to_string(newDestX) + ", " + std::to_string(newDestY) + ")", logFile);
                        }

                        // Mark replacement in replacements
                        replacementsFlag = 1;

                        // Form the updated command string
                        std::ostringstream formattedCommand;
                        formattedCommand << std::fixed << std::setprecision(3);
                        formattedCommand << commandType << ", " << actorID << ", " << duration << ", "
                                         << newDestX << ", " << newDestY << ", " << destZ;

                        if (!resetValue.empty()) {
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
void processScriptAiFollowCellTranslation(const Database& db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, std::vector<std::string>& updatedScriptIDs, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates, const ProgramOptions& options, std::ofstream& logFile) {

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
                    if (isCoordinateValid(db, gridX, gridY, customCoordinates, options, logFile)) {
                        int newGridX = gridX + offset.offsetX;
                        int newGridY = gridY + offset.offsetY;

                        if (!options.silentMode) {
                            logMessage("Found: Script 'AI Follow Cell' translation -> grid (" + std::to_string(gridX) + ", " + std::to_string(gridY) +
                                       ") | coordinates (" + std::to_string(destX) + ", " + std::to_string(destY) + ")", logFile);
                        }

                        // New calculation keeping the fractional part
                        double newDestX = (newGridX * 8192.0) + (destX - (gridX * 8192.0));
                        double newDestY = (newGridY * 8192.0) + (destY - (gridY * 8192.0));

                        if (!options.silentMode) {
                            logMessage("Calculating: new destination ---------------> grid (" + std::to_string(newGridX) + ", " + std::to_string(newGridY) +
                                       ") | coordinates (" + std::to_string(newDestX) + ", " + std::to_string(newDestY) + ")", logFile);
                        }

                        // Mark replacement in replacements
                        replacementsFlag = 1;
                        scriptUpdated = true;

                        // Form the updated command string
                        std::ostringstream formattedCommand;
                        formattedCommand << std::fixed << std::setprecision(3);
                        formattedCommand << commandType << ", " << actorID << ", " << cellID << ", " << duration << ", "
                                         << newDestX << ", " << newDestY << ", " << destZ;

                        if (!resetValue.empty()) {
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
void processDialogueAiFollowCellTranslation(const Database& db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates, const ProgramOptions& options, std::ofstream& logFile) {

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
                    if (isCoordinateValid(db, gridX, gridY, customCoordinates, options, logFile)) {
                        int newGridX = gridX + offset.offsetX;
                        int newGridY = gridY + offset.offsetY;

                        if (!options.silentMode) {
                            logMessage("Found: Dialogue 'AI Follow Cell' translation -> grid (" + std::to_string(gridX) + ", " + std::to_string(gridY) +
                                       ") | coordinates (" + std::to_string(destX) + ", " + std::to_string(destY) + ")", logFile);
                        }

                        // New calculation keeping the fractional part
                        double newDestX = (newGridX * 8192.0) + (destX - (gridX * 8192.0));
                        double newDestY = (newGridY * 8192.0) + (destY - (gridY * 8192.0));

                        if (!options.silentMode) {
                            logMessage("Calculating: new destination -----------------> grid (" + std::to_string(newGridX) + ", " + std::to_string(newGridY) +
                                       ") | coordinates (" + std::to_string(newDestX) + ", " + std::to_string(newDestY) + ")", logFile);
                        }

                        // Mark replacement in replacements
                        replacementsFlag = 1;

                        // Form the updated command string
                        std::ostringstream formattedCommand;
                        formattedCommand << std::fixed << std::setprecision(3);
                        formattedCommand << commandType << ", " << actorID << ", " << cellID << ", " << duration << ", "
                                         << newDestX << ", " << newDestY << ", " << destZ;

                        if (!resetValue.empty()) {
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
void processScriptAiTravelTranslation(const Database& db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, std::vector<std::string>& updatedScriptIDs, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates, const ProgramOptions& options, std::ofstream& logFile) {

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
                    if (isCoordinateValid(db, gridX, gridY, customCoordinates, options, logFile)) {
                        int newGridX = gridX + offset.offsetX;
                        int newGridY = gridY + offset.offsetY;

                        if (!options.silentMode) {
                            logMessage("Found: Script 'AI Travel' translation -> grid (" + std::to_string(gridX) + ", " + std::to_string(gridY) +
                                       ") | coordinates (" + std::to_string(destX) + ", " + std::to_string(destY) + ")", logFile);
                        }

                        // New calculation keeping the fractional part
                        double newDestX = (newGridX * 8192.0) + (destX - (gridX * 8192.0));
                        double newDestY = (newGridY * 8192.0) + (destY - (gridY * 8192.0));

                        if (!options.silentMode) {
                            logMessage("Calculating: new destination ----------> grid (" + std::to_string(newGridX) + ", " + std::to_string(newGridY) +
                                       ") | coordinates (" + std::to_string(newDestX) + ", " + std::to_string(newDestY) + ")", logFile);
                        }

                        // Mark replacement in replacements
                        replacementsFlag = 1;
                        scriptUpdated = true;

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
void processDialogueAiTravelTranslation(const Database& db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates, const ProgramOptions& options, std::ofstream& logFile) {

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
                    if (isCoordinateValid(db, gridX, gridY, customCoordinates, options, logFile)) {
                        int newGridX = gridX + offset.offsetX;
                        int newGridY = gridY + offset.offsetY;

                        if (!options.silentMode) {
                            logMessage("Found: Dialogue 'AI Travel' translation -> grid (" + std::to_string(gridX) + ", " + std::to_string(gridY) +
                                       ") | coordinates (" + std::to_string(destX) + ", " + std::to_string(destY) + ")", logFile);
                        }

                        // New calculation keeping the fractional part
                        double newDestX = (newGridX * 8192.0) + (destX - (gridX * 8192.0));
                        double newDestY = (newGridY * 8192.0) + (destY - (gridY * 8192.0));

                        if (!options.silentMode) {
                            logMessage("Calculating: new destination ------------> grid (" + std::to_string(newGridX) + ", " + std::to_string(newGridY) +
                                       ") | coordinates (" + std::to_string(newDestX) + ", " + std::to_string(newDestY) + ")", logFile);
                        }

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
void processScriptPositionTranslation(const Database& db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, std::vector<std::string>& updatedScriptIDs, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates, const ProgramOptions& options, std::ofstream& logFile) {

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
                    if (isCoordinateValid(db, gridX, gridY, customCoordinates, options, logFile)) {
                        int newGridX = gridX + offset.offsetX;
                        int newGridY = gridY + offset.offsetY;

                        if (!options.silentMode) {
                            logMessage("Found: Script 'Position' translation -> grid (" + std::to_string(gridX) + ", " + std::to_string(gridY) +
                                       ") | coordinates (" + std::to_string(destX) + ", " + std::to_string(destY) + ")", logFile);
                        }

                        // New calculation keeping the fractional part
                        double newDestX = (newGridX * 8192.0) + (destX - (gridX * 8192.0));
                        double newDestY = (newGridY * 8192.0) + (destY - (gridY * 8192.0));

                        if (!options.silentMode) {
                            logMessage("Calculating: new destination ---------> grid (" + std::to_string(newGridX) + ", " + std::to_string(newGridY) +
                                       ") | coordinates (" + std::to_string(newDestX) + ", " + std::to_string(newDestY) + ")", logFile);
                        }

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
void processDialoguePositionTranslation(const Database& db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates, const ProgramOptions& options, std::ofstream& logFile) {

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
                    if (isCoordinateValid(db, gridX, gridY, customCoordinates, options, logFile)) {
                        int newGridX = gridX + offset.offsetX;
                        int newGridY = gridY + offset.offsetY;

                        if (!options.silentMode) {
                            logMessage("Found: Dialogue 'Position' translation -> grid (" + std::to_string(gridX) + ", " + std::to_string(gridY) +
                                       ") | coordinates (" + std::to_string(destX) + ", " + std::to_string(destY) + ")", logFile);
                        }

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
void processScriptPositionCellTranslation(const Database& db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, std::vector<std::string>& updatedScriptIDs, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates, const ProgramOptions& options, std::ofstream& logFile) {

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
                    std::string cellID = match[6].str();

                    // Round only the integer part for grid coordinates
                    int gridX = static_cast<int>(std::floor(destX / 8192.0));
                    int gridY = static_cast<int>(std::floor(destY / 8192.0));

                    // Check if coordinate is valid (in DB or customCoordinates)
                    if (isCoordinateValid(db, gridX, gridY, customCoordinates, options, logFile)) {
                        int newGridX = gridX + offset.offsetX;
                        int newGridY = gridY + offset.offsetY;

                        if (!options.silentMode) {
                            logMessage("Found: Script 'Position Cell' translation -> grid (" + std::to_string(gridX) + ", " + std::to_string(gridY) +
                                       ") | coordinates (" + std::to_string(destX) + ", " + std::to_string(destY) + ")", logFile);
                        }

                        // New calculation keeping the fractional part
                        double newDestX = (newGridX * 8192.0) + (destX - (gridX * 8192.0));
                        double newDestY = (newGridY * 8192.0) + (destY - (gridY * 8192.0));

                        if (!options.silentMode) {
                            logMessage("Calculating: new destination --------------> grid (" + std::to_string(newGridX) + ", " + std::to_string(newGridY) +
                                       ") | coordinates (" + std::to_string(newDestX) + ", " + std::to_string(newDestY) + ")", logFile);
                        }

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
void processDialoguePositionCellTranslation(const Database& db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates, const ProgramOptions& options, std::ofstream& logFile) {

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
                    std::string cellID = match[6].str();

                    // Round only the integer part for grid coordinates
                    int gridX = static_cast<int>(std::floor(destX / 8192.0));
                    int gridY = static_cast<int>(std::floor(destY / 8192.0));

                    // Check if coordinate is valid (in DB or customCoordinates)
                    if (isCoordinateValid(db, gridX, gridY, customCoordinates, options, logFile)) {
                        int newGridX = gridX + offset.offsetX;
                        int newGridY = gridY + offset.offsetY;

                        if (!options.silentMode) {
                            logMessage("Found: Dialogue 'Position Cell' translation -> grid (" + std::to_string(gridX) + ", " + std::to_string(gridY) +
                                       ") | coordinates (" + std::to_string(destX) + ", " + std::to_string(destY) + ")", logFile);
                        }

                        // New calculation keeping the fractional part
                        double newDestX = (newGridX * 8192.0) + (destX - (gridX * 8192.0));
                        double newDestY = (newGridY * 8192.0) + (destY - (gridY * 8192.0));

                        if (!options.silentMode) {
                            logMessage("Calculating: new destination ----------------> grid (" + std::to_string(newGridX) + ", " + std::to_string(newGridY) +
                                       ") | coordinates (" + std::to_string(newDestX) + ", " + std::to_string(newDestY) + ")", logFile);
                        }

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
void processScriptPlaceItemTranslation(const Database& db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, std::vector<std::string>& updatedScriptIDs, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates, const ProgramOptions& options, std::ofstream& logFile) {

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
                    if (isCoordinateValid(db, gridX, gridY, customCoordinates, options, logFile)) {
                        int newGridX = gridX + offset.offsetX;
                        int newGridY = gridY + offset.offsetY;

                        if (!options.silentMode) {
                            logMessage("Found: Script 'Place Item' translation -> grid (" + std::to_string(gridX) + ", " + std::to_string(gridY) +
                                       ") | coordinates (" + std::to_string(destX) + ", " + std::to_string(destY) + ")", logFile);
                        }

                        // New calculation keeping the fractional part
                        double newDestX = (newGridX * 8192.0) + (destX - (gridX * 8192.0));
                        double newDestY = (newGridY * 8192.0) + (destY - (gridY * 8192.0));

                        if (!options.silentMode) {
                            logMessage("Calculating: new destination -----------> grid (" + std::to_string(newGridX) + ", " + std::to_string(newGridY) +
                                       ") | coordinates (" + std::to_string(newDestX) + ", " + std::to_string(newDestY) + ")", logFile);
                        }

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
void processDialoguePlaceItemTranslation(const Database& db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates, const ProgramOptions& options, std::ofstream& logFile) {

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
                    if (isCoordinateValid(db, gridX, gridY, customCoordinates, options, logFile)) {
                        int newGridX = gridX + offset.offsetX;
                        int newGridY = gridY + offset.offsetY;

                        if (!options.silentMode) {
                            logMessage("Found: Dialogue 'Place Item' translation -> grid (" + std::to_string(gridX) + ", " + std::to_string(gridY) +
                                       ") | coordinates (" + std::to_string(destX) + ", " + std::to_string(destY) + ")", logFile);
                        }

                        // New calculation keeping the fractional part
                        double newDestX = (newGridX * 8192.0) + (destX - (gridX * 8192.0));
                        double newDestY = (newGridY * 8192.0) + (destY - (gridY * 8192.0));

                        if (!options.silentMode) {
                            logMessage("Calculating: new destination -------------> grid (" + std::to_string(newGridX) + ", " + std::to_string(newGridY) +
                                       ") | coordinates (" + std::to_string(newDestX) + ", " + std::to_string(newDestY) + ")", logFile);
                        }

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
void processScriptPlaceItemCellTranslation(const Database& db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, std::vector<std::string>& updatedScriptIDs, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates, const ProgramOptions& options, std::ofstream& logFile) {

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
                    if (isCoordinateValid(db, gridX, gridY, customCoordinates, options, logFile)) {
                        int newGridX = gridX + offset.offsetX;
                        int newGridY = gridY + offset.offsetY;

                        if (!options.silentMode) {
                            logMessage("Found: Script 'Place Item Cell' translation -> grid (" + std::to_string(gridX) + ", " + std::to_string(gridY) +
                                       ") | coordinates (" + std::to_string(destX) + ", " + std::to_string(destY) + ")", logFile);
                        }

                        // New calculation keeping the fractional part
                        double newDestX = (newGridX * 8192.0) + (destX - (gridX * 8192.0));
                        double newDestY = (newGridY * 8192.0) + (destY - (gridY * 8192.0));

                        if (!options.silentMode) {
                            logMessage("Calculating: new destination ----------------> grid (" + std::to_string(newGridX) + ", " + std::to_string(newGridY) +
                                       ") | coordinates (" + std::to_string(newDestX) + ", " + std::to_string(newDestY) + ")", logFile);
                        }

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
void processDialoguePlaceItemCellTranslation(const Database& db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates, const ProgramOptions& options, std::ofstream& logFile) {

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
                    if (isCoordinateValid(db, gridX, gridY, customCoordinates, options, logFile)) {
                        int newGridX = gridX + offset.offsetX;
                        int newGridY = gridY + offset.offsetY;

                        if (!options.silentMode) {
                            logMessage("Found: Dialogue 'Place Item Cell' translation -> grid (" + std::to_string(gridX) + ", " + std::to_string(gridY) +
                                       ") | coordinates (" + std::to_string(destX) + ", " + std::to_string(destY) + ")", logFile);
                        }

                        // New calculation keeping the fractional part
                        double newDestX = (newGridX * 8192.0) + (destX - (gridX * 8192.0));
                        double newDestY = (newGridY * 8192.0) + (destY - (gridY * 8192.0));

                        if (!options.silentMode) {
                            logMessage("Calculating: new destination ------------------> grid (" + std::to_string(newGridX) + ", " + std::to_string(newGridY) +
                                       ") | coordinates (" + std::to_string(newDestX) + ", " + std::to_string(newDestY) + ")", logFile);
                        }

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
void processTranslation(ordered_json& jsonData, const GridOffset& offset, int& replacementsFlag, const ProgramOptions& options, std::ofstream& logFile) {
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
            continue;
        }

        // Check if the reference contains "temporary" and "translation", and if translation is an array of at least 2 elements
        if (reference.contains("temporary") &&
            reference.contains("translation") &&
            reference["translation"].is_array() &&
            reference["translation"].size() >= 2) {

            if (!options.silentMode) {
                logMessage("Processing: " + reference.value("id", "Unknown ID"), logFile);
            }

            // Log the original translation values before update
            double originalX = reference["translation"][0].get<double>();
            double originalY = reference["translation"][1].get<double>();

            if (!options.silentMode) {
                logMessage("Found reference coordinates -> X = " + std::to_string(originalX) + ", Y = " + std::to_string(originalY), logFile);
            }

            // Apply the offset to the X and Y values (multiplied by 8192 for scaling)
            reference["translation"][0] = originalX + offset.offsetX * 8192;
            reference["translation"][1] = originalY + offset.offsetY * 8192;

            // Mark that a replacement has been made
            replacementsFlag = 1;

            // Log the updated translation values after modification
            double updatedX = reference["translation"][0].get<double>();
            double updatedY = reference["translation"][1].get<double>();

            if (!options.silentMode) {
                logMessage("Calculating new coordinates -> X = " + std::to_string(updatedX) + ", Y = " + std::to_string(updatedY), logFile);
            }
        }
        else {
            if (!options.silentMode) {
                logMessage("No valid temporary or translation array found in reference: " + reference.value("id", "Unknown ID"), logFile);
            }
        }
    }
}

// Function to process coordinates for Cell, Landscape, and PathGrid types
void processGridValues(const Database& db, ordered_json& inputData, const GridOffset& offset, int& replacementsFlag, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates, const ProgramOptions& options, std::ofstream& logFile) {

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
            logMessage("WARNING - grid key is missing for type: " + typeName, logFile);
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
        if (isCoordinateValid(db, gridX, gridY, customCoordinates, options, logFile)) {
            int newGridX = gridX + offset.offsetX;
            int newGridY = gridY + offset.offsetY;

            if (!options.silentMode) {
                logMessage("Updating grid coordinates for (" + typeName + "): (" + std::to_string(gridX) + ", " + std::to_string(gridY) +
                           ") -> (" + std::to_string(newGridX) + ", " + std::to_string(newGridY) + ")", logFile);
            }

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
                processTranslation(item, offset, replacementsFlag, options, logFile);
            }

            // Mark that a replacement has been made
            replacementsFlag = 1;

        }
    }
}

// Function to log updated script IDs
void logUpdatedScriptIDs(const std::vector<std::string>& updatedScriptIDs, std::ofstream& logFile) {
    if (updatedScriptIDs.empty()) {
        logMessage("No scripts were updated...", logFile);
        return;
    }

    std::unordered_set<std::string> seenIDs;
    std::vector<std::string> uniqueIDs;

    // Filter duplicates
    for (const auto& id : updatedScriptIDs) {
        if (seenIDs.insert(id).second) {
            uniqueIDs.push_back(id);
        }
    }

    logMessage("Updated scripts list:", logFile);
    for (const auto& id : uniqueIDs) {
        logMessage("- Script ID: " + id, logFile);
    }
}