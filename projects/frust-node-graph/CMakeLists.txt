cmake_minimum_required(VERSION 3.22)
project(FrustNodeGraph VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------------------------------------------------------------------
# frust_node_graph: the FRust node graph document - node definitions, graph
# model, schematic JSON load/save and structural validation. juce_core only:
# no LLVM, no compiler, no GUI, so the IDE's node designer, command-line
# tools and other applications (the Electronics Workbench's programmable
# components, later) can all load and inspect graphs. node_compiler stays
# the compiler; this library never generates code.
#
# Locate JUCE the same way frate does (JUCE_PATH cache or env var) when
# configured on its own; a parent project that already added JUCE wins.
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

get_filename_component(FRUST_NODE_GRAPH_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

add_library(frust_node_graph STATIC
    include/frust_node_graph/NodeDefinitions.h
    include/frust_node_graph/NodeGraph.h
    src/NodeDefinitions.cpp
    src/NodeGraph.cpp
)
target_include_directories(frust_node_graph PUBLIC include)
target_link_libraries(frust_node_graph PUBLIC juce::juce_core)
if(WIN32)
    target_compile_definitions(frust_node_graph PUBLIC NOMINMAX)
endif()
set_target_properties(frust_node_graph PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${FRUST_NODE_GRAPH_REPO_ROOT}/lib/Debug"
    ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${FRUST_NODE_GRAPH_REPO_ROOT}/lib/Release"
)

# Regression tests (no window, no compiler) and a command-line inspector that
# loads a schematic, validates it and prints its structure.
foreach(tool frust_node_graph_tests frust_node_graph_inspect)
    if(tool STREQUAL "frust_node_graph_tests")
        add_executable(${tool} tests/NodeGraphTests.cpp)
        target_compile_definitions(${tool} PRIVATE
            "FRUST_NODE_GRAPH_FIXTURES=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures\"")
    else()
        add_executable(${tool} tools/Inspect.cpp)
    endif()
    target_link_libraries(${tool} PRIVATE frust_node_graph)
    set_target_properties(${tool} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${FRUST_NODE_GRAPH_REPO_ROOT}/bin/Debug"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${FRUST_NODE_GRAPH_REPO_ROOT}/bin/Release"
    )
endforeach()
