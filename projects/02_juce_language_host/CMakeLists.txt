cmake_minimum_required(VERSION 3.22)
project(LagDaemonResearchIDE VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------------------------------------------------------------------
# Locate Local JUCE Installation & juce_docking Library
# ------------------------------------------------------------------------------
if(EXISTS "D:/JUCE/CMakeLists.txt")
    message(STATUS "LagDaemonResearchIDE: Using local JUCE installation at D:/JUCE")
    add_subdirectory("D:/JUCE" juce_build)
endif()

if(EXISTS "D:/000 Tech Research/projects/03_juce_docking_system/CMakeLists.txt")
    message(STATUS "LagDaemonResearchIDE: Adding juce_docking library")
    add_subdirectory("D:/000 Tech Research/projects/03_juce_docking_system" juce_docking_build)
endif()

if(EXISTS "D:/000 Tech Research/projects/01_language_paradigms/02_functional/CMakeLists.txt")
    message(STATUS "LagDaemonResearchIDE: Adding frust_lang library")
    add_subdirectory("D:/000 Tech Research/projects/01_language_paradigms/02_functional" frust_lang_build)
endif()

if(EXISTS "D:/000 Tech Research/projects/04_ai_provider/CMakeLists.txt")
    message(STATUS "LagDaemonResearchIDE: Adding ai_provider library")
    add_subdirectory("D:/000 Tech Research/projects/04_ai_provider" ai_provider_build)
endif()

if(EXISTS "D:/000 Tech Research/projects/05_frate/CMakeLists.txt")
    if(NOT TARGET frate_lib)
        message(STATUS "LagDaemonResearchIDE: Adding frate_lib library")
        add_subdirectory("D:/000 Tech Research/projects/05_frate" frate_build)
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

juce_generate_juce_header(LagDaemonResearchIDE)
