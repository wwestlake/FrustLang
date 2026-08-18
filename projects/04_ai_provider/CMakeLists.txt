cmake_minimum_required(VERSION 3.22)
project(ai_provider VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------------------------------------------------------------------
# Locate JUCE Framework
# ------------------------------------------------------------------------------
if(NOT TARGET juce::juce_core)
    if(EXISTS "D:/JUCE/CMakeLists.txt")
        add_subdirectory("D:/JUCE" juce_build)
    elseif(EXISTS "$ENV{JUCE_PATH}")
        add_subdirectory("$ENV{JUCE_PATH}" juce_build)
    endif()
endif()

# ------------------------------------------------------------------------------
# Route Build Output to the Top-Level /lib Folder (Debug & Release)
# ------------------------------------------------------------------------------
get_filename_component(REPO_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG   "${REPO_ROOT_DIR}/lib/Debug")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${REPO_ROOT_DIR}/lib/Release")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG   "${REPO_ROOT_DIR}/lib/Debug")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE "${REPO_ROOT_DIR}/lib/Release")

# ------------------------------------------------------------------------------
# Add ai_provider Static Library Target
#
# Provider-agnostic AI chat abstraction (AiProvider) with one real
# implementation (OpenAiProvider) and a config loader (AiConfig) that reads
# named profiles - each its own provider type, API key, and model - from a
# JSON file in user app-data (never inside the repo, so a key can never end
# up committed). Add a new provider by subclassing AiProvider and wiring it
# into AiConfig::createProvider(); nothing else needs to change.
# ------------------------------------------------------------------------------
add_library(ai_provider STATIC
    include/ai_provider/AiProvider.h
    include/ai_provider/OpenAiProvider.h
    include/ai_provider/AiConfig.h
    src/OpenAiProvider.cpp
    src/AiConfig.cpp
)

target_include_directories(ai_provider PUBLIC
    include
)

target_link_libraries(ai_provider PUBLIC
    juce::juce_core
)

# Same gap as frate_lib (see its CMakeLists.txt for the full explanation):
# OpenAiProvider's juce::URL HTTP calls compile against libcurl's headers
# via JUCE's pkg-config detection, but JUCE doesn't propagate the actual
# link dependency to consumers on Linux.
if(UNIX AND NOT APPLE)
    find_package(CURL REQUIRED)
    target_link_libraries(ai_provider PUBLIC CURL::libcurl)
endif()
