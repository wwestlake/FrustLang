cmake_minimum_required(VERSION 3.22)
project(PlotViewer VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT TARGET juce::juce_core)
    if(DEFINED JUCE_PATH AND EXISTS "${JUCE_PATH}/CMakeLists.txt")
        add_subdirectory("${JUCE_PATH}" juce_build)
    elseif(DEFINED ENV{JUCE_PATH} AND EXISTS "$ENV{JUCE_PATH}/CMakeLists.txt")
        add_subdirectory("$ENV{JUCE_PATH}" juce_build)
    else()
        message(FATAL_ERROR "JUCE not found")
    endif()
endif()

juce_add_gui_app(PlotViewer
    PRODUCT_NAME "PlotViewer"
    VERSION 0.1.0
)

target_sources(PlotViewer PRIVATE
    Source/Main.cpp
    ../plot_viewer_core/build/plot_viewer_core.o
)

target_link_libraries(PlotViewer PRIVATE
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_opengl
    legacy_stdio_definitions.lib
)

juce_generate_juce_header(PlotViewer)
