cmake_minimum_required(VERSION 3.22)
project(frate_lib VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------------------------------------------------------------------
# Locate JUCE Framework - via JUCE_PATH (env var or -DJUCE_PATH=...), not a
# hardcoded machine-specific path. Set per-platform in CMakePresets.json
# (see the frust compiler's presets) or export JUCE_PATH in the shell.
# ------------------------------------------------------------------------------
if(NOT TARGET juce::juce_core)
    if(DEFINED JUCE_PATH AND EXISTS "${JUCE_PATH}/CMakeLists.txt")
        add_subdirectory("${JUCE_PATH}" juce_build)
    elseif(DEFINED ENV{JUCE_PATH} AND EXISTS "$ENV{JUCE_PATH}/CMakeLists.txt")
        add_subdirectory("$ENV{JUCE_PATH}" juce_build)
    else()
        message(FATAL_ERROR "JUCE not found - set JUCE_PATH (cache var or env var) to a JUCE checkout.")
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
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${REPO_ROOT_DIR}/bin/Debug")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${REPO_ROOT_DIR}/bin/Release")

# ------------------------------------------------------------------------------
# Add frate_lib Static Library Target
#
# Frust's package manager, client side. Consumer half: FrateConfig (a
# project's frate.json dependency list) + FrateCache (local, machine-wide
# pod cache) + FrateRegistryClient (HTTP fetch from a configurable registry
# URL) + FrateResolver (ties them together: cache-hit or fetch-and-cache).
# Producer half: FratePodBuilder (scaffold a new pod, package an existing
# one into a distributable .frpod - a real zip file: pod.json + source).
#
# The registry URL defaults to lagdaemon.com/frate/api, which doesn't exist
# yet - FrateRegistryClient will fail cleanly with a network error until
# that's stood up. Nothing here depends on it existing: create -> package
# -> FrateCache::installFromPackage() (fully local, no network) already
# closes the whole loop for local development.
# ------------------------------------------------------------------------------
add_library(frate_lib STATIC
    include/frate/PodMetadata.h
    include/frate/FrateConfig.h
    include/frate/FrateCache.h
    include/frate/FrateRegistryClient.h
    include/frate/FrateResolver.h
    include/frate/FratePodBuilder.h
    src/PodMetadataJson.h
    src/PodMetadataJson.cpp
    src/FrateConfig.cpp
    src/FrateCache.cpp
    src/FrateRegistryClient.cpp
    src/FrateResolver.cpp
    src/FratePodBuilder.cpp
)

target_include_directories(frate_lib PUBLIC
    include
)

target_link_libraries(frate_lib PUBLIC
    juce::juce_core
)

# On Linux, juce_core's HTTP support (used by FrateRegistryClient) compiles
# against libcurl's headers (found via pkg-config above) but JUCE's own
# CMake doesn't propagate the actual link dependency to consumers - link it
# explicitly here or every consumer hits "undefined reference to curl_*" at
# link time despite compiling cleanly. Not needed on Windows (JUCE uses
# WinHTTP there instead).
if(UNIX AND NOT APPLE)
    find_package(CURL REQUIRED)
    target_link_libraries(frate_lib PUBLIC CURL::libcurl)
endif()

# ------------------------------------------------------------------------------
# Add frate Executable Target
# ------------------------------------------------------------------------------
add_executable(frate_cli
    src/main.cpp
)
set_target_properties(frate_cli PROPERTIES OUTPUT_NAME "frate")

target_link_libraries(frate_cli PRIVATE
    frate_lib
)
