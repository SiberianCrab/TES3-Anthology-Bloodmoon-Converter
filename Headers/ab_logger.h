#pragma once
#include <fstream>
#include <string>

// Log messages to both a log file and console
void logMessage(const std::string& message, std::ofstream& logFile);

// Clear log file
void logClear();

// Log errors, close the database and terminate the program
[[noreturn]] void logErrorAndExit(const std::string& errorMessage, std::ofstream& logFile);

