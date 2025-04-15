cmake_minimum_required(VERSION 3.15)
project(TES3_Anthology_Bloodmoon_Converter)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Disable MSVC warnings
if (MSVC)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

# Enable resource file support on Windows
if (WIN32)
    enable_language(RC)
endif()

# Main source files
set(SOURCES
    "TES3 Anthology Bloodmoon Converter.cpp"
    "database.cpp"
)

# Header files
set(HEADERS
    "Headers/database.h"
    "Headers/sqlite3.h"
    "Headers/json.hpp"
)

# Create executable
add_executable(tes3_ab_converter ${SOURCES} ${HEADERS})

# Include directories
target_include_directories(tes3_ab_converter PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/Headers
)

# Link SQLite3
if (WIN32)
    # Look for library in specified path and link
    find_library(SQLITE3_LIBRARY sqlite3 PATHS "${CMAKE_CURRENT_SOURCE_DIR}/Libraries")
    if (SQLITE3_LIBRARY)
        target_link_libraries(tes3_ab_converter PRIVATE ${SQLITE3_LIBRARY})
    else()
        message(WARNING "SQLite3 library not found in Libraries directory")
    endif()
else()
    find_package(SQLite3 REQUIRED)
    target_link_libraries(tes3_ab_converter PRIVATE SQLite::SQLite3)
endif()

# ----------------------------------------------------------
# Copy required files to output directory after build
# ----------------------------------------------------------

# List of files to copy alongside the executable
set(DATA_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/Libraries/sqlite3.dll"
    "${CMAKE_CURRENT_SOURCE_DIR}/DB/tes3_ab_cell_x-y_data.db"
    "${CMAKE_CURRENT_SOURCE_DIR}/DB/tes3_ab_custom_cell_x-y_data.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/Help/tes3_ab_help.txt"
)

# Add copy command for each file
foreach(file ${DATA_FILES})
    # Extract just the filename
    get_filename_component(filename ${file} NAME)
    
    add_custom_command(TARGET tes3_ab_converter POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${file}"
        "$<TARGET_FILE_DIR:tes3_ab_converter>/${filename}"
        COMMENT "Copying ${filename} to output directory"
    )
endforeach()

# Add optional copy target
add_custom_target(copy_files ALL DEPENDS tes3_ab_converter)