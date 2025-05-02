#pragma once
#include <filesystem>
#include <string>
#include <vector>

#include <json.hpp>

// Define program metadata constants
const std::string PROGRAM_NAME = "TES3 Anthology Bloodmoon Converter";
const std::string PROGRAM_VERSION = "V 1.3.0";
const std::string PROGRAM_AUTHOR = "by SiberianCrab";
const std::string PROGRAM_TESTER = "Beta testing by ShulShagana";

// Define tes3conv constants for Windows|Linux
#ifdef _WIN32
const std::string TES3CONV_COMMAND = "tes3conv.exe";
#else
const std::string TES3CONV_COMMAND = "./tes3conv";
#endif

// Define an alias for ordered_json type from the nlohmann library
using ordered_json = nlohmann::ordered_json;

// Structure for storing program configuration options
struct ProgramOptions {
    bool batchMode = false;
    bool silentMode = false;
    std::vector<std::filesystem::path> inputFiles;
    int conversionType = 0;
};

// Function to parse command-line arguments
ProgramOptions parseArguments(int argc, char* argv[]);