cmake_minimum_required(VERSION 3.22)
project(LagDaemonResearchIDE VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------------------------------------------------------------------
# Locate JUCE - via JUCE_PATH (cache var or env var), not a hardcoded
# machine-specific path (same pattern as frate/juce_docking/ai_provider's
# CMakeLists.txt). Sibling projects below are referenced by relative path
# instead - they live in this same repo on every platform, so there's
# nothing machine-specific to resolve there at all.
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

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../03_juce_docking_system/CMakeLists.txt")
    message(STATUS "LagDaemonResearchIDE: Adding juce_docking library")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../03_juce_docking_system" juce_docking_build)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../01_language_paradigms/02_functional/CMakeLists.txt")
    message(STATUS "LagDaemonResearchIDE: Adding frust_lang library")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../01_language_paradigms/02_functional" frust_lang_build)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../04_ai_provider/CMakeLists.txt")
    message(STATUS "LagDaemonResearchIDE: Adding ai_provider library")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../04_ai_provider" ai_provider_build)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../05_frate/CMakeLists.txt")
    if(NOT TARGET frate_lib)
        message(STATUS "LagDaemonResearchIDE: Adding frate_lib library")
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../05_frate" frate_build)
    endif()
endif()

# ------------------------------------------------------------------------------
# Route Build Output to the Top-Level /bin Folder (Debug & Release)
# Placed after the JUCE / juce_docking subdirectories so their own internal
# tool targets (e.g. juceaide) are unaffected and stay in the build tree.
# ------------------------------------------------------------------------------
get_filename_component(REPO_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${REPO_ROOT_DIR}/bin/Debug")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${REPO_ROOT_DIR}/bin/Release")

# ------------------------------------------------------------------------------
# Add JUCE GUI Application Target
# ------------------------------------------------------------------------------
juce_add_gui_app(LagDaemonResearchIDE
    PRODUCT_NAME "LagDaemon Language Research IDE"
    VERSION 0.1.0
)

# juce_add_gui_app names the output binary after PRODUCT_NAME by default
# (fine for a double-click desktop app, unusable from a shell - spaces
# need quoting every time). Same OUTPUT_NAME override pattern frate_cli
# already uses - PRODUCT_NAME stays as the pretty display name (window
# title, About dialog), the actual binary gets a short CLI-friendly name.
set_target_properties(LagDaemonResearchIDE PROPERTIES OUTPUT_NAME "frust_ide")

target_sources(LagDaemonResearchIDE PRIVATE
    Source/Main.cpp
    Source/WorkbenchComponent.h
    Source/WorkbenchComponent.cpp
    Source/Auth/DesktopAuthSession.h
    Source/Auth/DesktopAuthSession.cpp
    Source/FileTreePanel.h
    Source/FileTreePanel.cpp
    Source/EditorTabComponent.h
    Source/EditorTabComponent.cpp
    Source/FrustTokeniser.h
    Source/FrustTokeniser.cpp
    Source/MetadataPanel.h
    Source/MetadataPanel.cpp
    Source/ConsolePanel.h
    Source/ConsolePanel.cpp
    Source/TerminalPanel.h
    Source/TerminalPanel.cpp
    Source/TerminalInstance.h
    Source/TerminalInstance.cpp
    Source/ContextPanel.h
    Source/ContextPanel.cpp
    Source/AiChatPanel.h
    Source/AiChatPanel.cpp
    Source/FratePanel.h
    Source/FratePanel.cpp
    Source/CustomSplitterBar.h
)

target_compile_definitions(LagDaemonResearchIDE PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(LagDaemonResearchIDE PRIVATE
    juce_docking
    frust_lang
    ai_provider
    frate_lib
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
)

# See juce_docking's CMakeLists.txt for the full explanation - this target
# also compiles juce_gui_extra.cpp itself (JUCE recompiles each module per
# linking target on Linux) and needs the same GTK3 include path.
if(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
    target_include_directories(LagDaemonResearchIDE PRIVATE ${GTK3_INCLUDE_DIRS})
endif()

juce_generate_juce_header(LagDaemonResearchIDE)
