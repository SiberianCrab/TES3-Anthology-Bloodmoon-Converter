#pragma once
#include <fstream>
#include <unordered_set>
#include <vector>
#include <regex>
#include <string>
#include <utility>

#include "json.hpp"

#include "ab_coord_processor.h"
#include "ab_database.h"
#include "ab_options.h"

// Function to process translations for interior door coordinates
void processInteriorDoorsTranslation(const Database& db, ordered_json& inputData, const GridOffset& offset,
    int& replacementsFlag, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates,
    const ProgramOptions& options, std::ofstream& logFile);

// Function to process NPC Travel Service coordinates
void processNpcTravelDestinations(const Database& db, ordered_json& inputData, const GridOffset& offset,
    int& replacementsFlag, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates,
    const ProgramOptions& options, std::ofstream& logFile);

// Function to process Script AI Escort translation
void processScriptAiEscortTranslation(const Database& db, ordered_json& inputData, const GridOffset& offset,
    int& replacementsFlag, std::vector<std::string>& updatedScriptIDs,
    const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates,
    const ProgramOptions& options, std::ofstream& logFile);

// Function to process Dialogue AI Escort translation
void processDialogueAiEscortTranslation(const Database& db, ordered_json& inputData, const GridOffset& offset,
    int& replacementsFlag, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates,
    const ProgramOptions& options, std::ofstream& logFile);

// Function to process Script AI Escort Cell translation
void processScriptAiEscortCellTranslation(const Database& db, ordered_json& inputData, const GridOffset& offset,
    int& replacementsFlag, std::vector<std::string>& updatedScriptIDs,
    const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates,
    const ProgramOptions& options, std::ofstream& logFile);

// Function to process Dialogue AI Escort Cell translation
void processDialogueAiEscortCellTranslation(const Database& db, ordered_json& inputData, const GridOffset& offset,
    int& replacementsFlag, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates,
    const ProgramOptions& options, std::ofstream& logFile);

// Function to process Script AI Follow translation
void processScriptAiFollowTranslation(const Database& db, ordered_json& inputData, const GridOffset& offset,
    int& replacementsFlag, std::vector<std::string>& updatedScriptIDs,
    const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates,
    const ProgramOptions& options, std::ofstream& logFile);

// Function to process Dialogue AI Follow translation
void processDialogueAiFollowTranslation(const Database& db, ordered_json& inputData, const GridOffset& offset,
    int& replacementsFlag, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates,
    const ProgramOptions& options, std::ofstream& logFile);

// Function to process Script AI Follow Cell translation
void processScriptAiFollowCellTranslation(const Database& db, ordered_json& inputData, const GridOffset& offset,
    int& replacementsFlag, std::vector<std::string>& updatedScriptIDs,
    const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates,
    const ProgramOptions& options, std::ofstream& logFile);

// Function to process Dialogue AI Follow Cell translation
void processDialogueAiFollowCellTranslation(const Database& db, ordered_json& inputData, const GridOffset& offset,
    int& replacementsFlag, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates,
    const ProgramOptions& options, std::ofstream& logFile);

// Function to process Script AI Travel translation
void processScriptAiTravelTranslation(const Database& db, ordered_json& inputData, const GridOffset& offset,
    int& replacementsFlag, std::vector<std::string>& updatedScriptIDs,
    const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates,
    const ProgramOptions& options, std::ofstream& logFile);

// Function to process Dialogue AI Travel translation
void processDialogueAiTravelTranslation(const Database& db, ordered_json& inputData, const GridOffset& offset,
    int& replacementsFlag, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates,
    const ProgramOptions& options, std::ofstream& logFile);

// Function to process Script Position translation
void processScriptPositionTranslation(const Database& db, ordered_json& inputData, const GridOffset& offset,
    int& replacementsFlag, std::vector<std::string>& updatedScriptIDs,
    const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates,
    const ProgramOptions& options, std::ofstream& logFile);

// Function to process Dialogue Position translation
void processDialoguePositionTranslation(const Database& db, ordered_json& inputData, const GridOffset& offset,
    int& replacementsFlag, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates,
    const ProgramOptions& options, std::ofstream& logFile);

// Function to process Script PositionCell translation
void processScriptPositionCellTranslation(const Database& db, ordered_json& inputData, const GridOffset& offset,
    int& replacementsFlag, std::vector<std::string>& updatedScriptIDs,
    const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates,
    const ProgramOptions& options, std::ofstream& logFile);

// Function to process Dialogue PositionCell translation
void processDialoguePositionCellTranslation(const Database& db, ordered_json& inputData, const GridOffset& offset,
    int& replacementsFlag, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates,
    const ProgramOptions& options, std::ofstream& logFile);

// Function to process Script PlaceItem translation
void processScriptPlaceItemTranslation(const Database& db, ordered_json& inputData, const GridOffset& offset,
    int& replacementsFlag, std::vector<std::string>& updatedScriptIDs,
    const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates,
    const ProgramOptions& options, std::ofstream& logFile);

// Function to process Dialogue PlaceItem translation
void processDialoguePlaceItemTranslation(const Database& db, ordered_json& inputData, const GridOffset& offset,
    int& replacementsFlag, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates,
    const ProgramOptions& options, std::ofstream& logFile);

// Function to process Script PlaceItemCell translation
void processScriptPlaceItemCellTranslation(const Database& db, ordered_json& inputData, const GridOffset& offset,
    int& replacementsFlag, std::vector<std::string>& updatedScriptIDs,
    const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates,
    const ProgramOptions& options, std::ofstream& logFile);

// Function to process Dialogue PlaceItemCell translation
void processDialoguePlaceItemCellTranslation(const Database& db, ordered_json& inputData, const GridOffset& offset,
    int& replacementsFlag, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates,
    const ProgramOptions& options, std::ofstream& logFile);

// Function to search and update the translation block inside the references object
void processTranslation(ordered_json& jsonData, const GridOffset& offset, int& replacementsFlag,
    const ProgramOptions& options, std::ofstream& logFile);

// Function to process coordinates for Cell, Landscape, and PathGrid types
void processGridValues(const Database& db, ordered_json& inputData, const GridOffset& offset,
    int& replacementsFlag, const std::unordered_set<std::pair<int, int>, PairHash>& customCoordinates,
    const ProgramOptions& options, std::ofstream& logFile);

// Function to log updated script IDs
void logUpdatedScriptIDs(const std::vector<std::string>& updatedScriptIDs, std::ofstream& logFile);