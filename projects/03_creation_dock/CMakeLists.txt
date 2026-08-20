cmake_minimum_required(VERSION 3.22)
project(CreationDock VERSION 0.1.0 LANGUAGES C CXX)

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
# Route Build Output to the Top-Level /bin and /lib Folders (Debug & Release)
# ------------------------------------------------------------------------------
get_filename_component(REPO_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${REPO_ROOT_DIR}/bin/Debug")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${REPO_ROOT_DIR}/bin/Release")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG   "${REPO_ROOT_DIR}/lib/Debug")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${REPO_ROOT_DIR}/lib/Release")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG   "${REPO_ROOT_DIR}/lib/Debug")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE "${REPO_ROOT_DIR}/lib/Release")

# ------------------------------------------------------------------------------
# Add CreationDock Static Library Target
# ------------------------------------------------------------------------------
add_library(CreationDock STATIC
    src/DockPanel.cpp
    src/DockTab.cpp
    src/DockTabStrip.cpp
    src/DockZone.cpp
    src/DockOverlay.cpp
    src/DockSplitter.cpp
    src/FloatingDockWindow.cpp
    src/DockContainer.cpp
    src/DockManager.cpp
)

# juce_gui_extra.cpp gets compiled fresh here too (see the GTK3 comment
# below), and JUCE_WEB_BROWSER defaults to 1 unless a consumer opts out -
# the IDE target already sets 0 for its own compile of the same module,
# but that define doesn't propagate to CreationDock's independent one.
# CreationDock has no actual use for WebBrowserComponent; without this,
# gui_extra.cpp unconditionally requires webkit2gtk headers on Linux.
target_compile_definitions(CreationDock PUBLIC JUCE_WEB_BROWSER=0)

target_include_directories(CreationDock PUBLIC
    include
)

target_link_libraries(CreationDock PUBLIC
    juce::juce_core
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
)

# On Linux, JUCE modules are recompiled from source for every target that
# links them (no shared "libjuce" - each target gets its own translation
# units) - so juce_gui_extra.cpp gets compiled here too, and needs GTK3's
# headers, which live under a non-default include path
# (/usr/include/gtk-3.0/gtk/gtk.h) that installing libgtk-3-dev alone
# doesn't add to the compiler's default search. Not needed on Windows/Mac.
if(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
    target_include_directories(CreationDock PRIVATE ${GTK3_INCLUDE_DIRS})
endif()
