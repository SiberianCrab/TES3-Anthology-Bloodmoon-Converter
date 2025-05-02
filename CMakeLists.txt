cmake_minimum_required(VERSION 3.15)
project(TES3_Anthology_Bloodmoon_Converter)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Paths to Directories
set(SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Source Files")
set(HEADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Headers")
set(RESOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Resource Files")
set(LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Libraries")
set(DB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/DB")
set(HELP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Help")

# MSVC-specific settings
if(MSVC)
    add_compile_definitions(
        _CRT_SECURE_NO_WARNINGS
        _SCL_SECURE_NO_WARNINGS
    )
    add_compile_options(/W4 /WX)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Windows-specific configuration
if(WIN32)
    enable_language(RC)
    
    # Resource files
    set(RESOURCE_FILES
        "${RESOURCE_DIR}/tes3_ab_converter.rc"
        "${RESOURCE_DIR}/resource.h"
        "${RESOURCE_DIR}/Icon.ico"
    )
    
    # Parse version info from RC file
    if(EXISTS "${RESOURCE_DIR}/tes3_ab_converter.rc")
        file(STRINGS "${RESOURCE_DIR}/tes3_ab_converter.rc" RC_VERSION_LINES REGEX "FILEVERSION|PRODUCTVERSION")
        foreach(line IN LISTS RC_VERSION_LINES)
            if(line MATCHES "FILEVERSION ([0-9]+,[0-9]+,[0-9]+,[0-9]+)")
                set(VERSION_STRING "${CMAKE_MATCH_1}")
                break()
            endif()
        endforeach()

        if(DEFINED VERSION_STRING)
            string(REPLACE "," "." VERSION_DOTTED "${VERSION_STRING}")
            message(STATUS "Detected version from RC file: ${VERSION_DOTTED}")
            
            # Set project version variables
            string(REPLACE "," ";" VERSION_LIST "${VERSION_STRING}")
            list(GET VERSION_LIST 0 PROJECT_VERSION_MAJOR)
            list(GET VERSION_LIST 1 PROJECT_VERSION_MINOR)
            list(GET VERSION_LIST 2 PROJECT_VERSION_PATCH)
            list(GET VERSION_LIST 3 PROJECT_VERSION_TWEAK)
        endif()
    endif()
endif()

# Source files
set(SOURCES
    "${SOURCE_DIR}/tes3_ab_converter.cpp"
	"${SOURCE_DIR}/arguments.cpp"
    "${SOURCE_DIR}/database.cpp"
    "${SOURCE_DIR}/logger.cpp"
    ${RESOURCE_FILES}
)

# Headers
set(HEADERS
    "${HEADER_DIR}/arguments.h"
    "${HEADER_DIR}/constants.h"
    "${HEADER_DIR}/database.h"
    "${HEADER_DIR}/json.hpp"
	"${HEADER_DIR}/logger.h"
	"${HEADER_DIR}/sqlite3.h"
)

# Create executable
add_executable(tes3_ab_converter ${SOURCES} ${HEADERS})

# Windows-specific icon and version info properties
if(WIN32)
    # Set application icon
    if(EXISTS "${RESOURCE_DIR}/Icon.ico")
        set_target_properties(tes3_ab_converter PROPERTIES
            RC_ICONS "${RESOURCE_DIR}/Icon.ico"
        )
    endif()
    
    # Set version info
    if(DEFINED VERSION_STRING)
        set_target_properties(tes3_ab_converter PROPERTIES
            VERSION ${VERSION_DOTTED}
            SOVERSION ${PROJECT_VERSION_MAJOR}
        )
    endif()
endif()

# Include directories
target_include_directories(tes3_ab_converter PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${HEADER_DIR}
)

# SQLite3 linking
if(WIN32)
    find_library(SQLITE3_LIBRARY sqlite3 
        PATHS "${LIB_DIR}"
        NO_DEFAULT_PATH
        REQUIRED
    )
    target_link_libraries(tes3_ab_converter PRIVATE ${SQLITE3_LIBRARY})
else()
    find_package(SQLite3 REQUIRED)
    target_link_libraries(tes3_ab_converter PRIVATE SQLite::SQLite3)
    target_include_directories(tes3_ab_converter PRIVATE ${SQLite3_INCLUDE_DIRS})
endif()

# Copy required files to output directory after build
set(DATA_FILES
    "${LIB_DIR}/sqlite3.dll"
    "${DB_DIR}/tes3_ab_cell_x-y_data.db"
    "${DB_DIR}/tes3_ab_custom_cell_x-y_data.txt"
    "${HELP_DIR}/tes3_ab_help.txt"
)

foreach(file ${DATA_FILES})
    if(EXISTS "${file}")
        get_filename_component(filename ${file} NAME)
        add_custom_command(TARGET tes3_ab_converter POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${file}"
            "$<TARGET_FILE_DIR:tes3_ab_converter>/${filename}"
            COMMENT "Copying ${filename} to output directory"
        )
    else()
        message(WARNING "File not found: ${file}")
    endif()
endforeach()

# Safe PDB copy for MSVC (works correctly with multi-config generators)
if(MSVC)
    add_custom_command(TARGET tes3_ab_converter POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_PDB_FILE:tes3_ab_converter>"
            "$<TARGET_FILE_DIR:tes3_ab_converter>/$<TARGET_PDB_FILE_NAME:tes3_ab_converter>"
        COMMENT "Copying PDB file to output directory (only if exists)"
        BYPRODUCTS "$<TARGET_PDB_FILE:tes3_ab_converter>"
    )
endif()
