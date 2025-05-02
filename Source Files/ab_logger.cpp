#include <iostream>
#include <fstream>
#include <cstdlib>
#include <limits>

#include "ab_logger.h"

// Function to log messages to both a log file and console
void logMessage(const std::string& message, std::ofstream& logFile) {
    std::cout << message << std::endl;
    logFile << message << std::endl;
}

// Function to clear log file
void logClear() {
    std::ofstream ofs("tes3_ab.log", std::ofstream::trunc);
    ofs.close();
}

// Function to log errors, close the database and terminate the program
[[noreturn]] void logErrorAndExit(const std::string& errorMessage, std::ofstream& logFile) {
    std::cerr << errorMessage;
    logFile << errorMessage;
    logFile.close();

#ifndef __linux__
    std::cout << "\nPress Enter to exit...";
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
#endif

    std::exit(EXIT_FAILURE);
}
