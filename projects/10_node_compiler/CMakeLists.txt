cmake_minimum_required(VERSION 3.22)
project(NodeCompiler VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------------------------------------------------------------------
# Same LLVM-lookup/frust_lang-linking pattern as frust_plugin_host's
# CMakeLists.txt (see that file's comments for the full explanation of why
# this find_package/llvm_map_components_to_libnames call can't be reused
# from frust_lang's own CMakeLists.txt).
# ------------------------------------------------------------------------------
find_package(LLVM CONFIG REQUIRED)
message(STATUS "NodeCompiler: Found LLVM ${LLVM_PACKAGE_VERSION} at ${LLVM_DIR}")

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)
add_definitions(${LLVM_DEFINITIONS})
include_directories(${LLVM_INCLUDE_DIRS})

llvm_map_components_to_libnames(NODE_COMPILER_LLVM_LIBS
    Core
    ExecutionEngine
    OrcJIT
    Support
    native
    nativecodegen
)

if(NOT TARGET frust_lang)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../01_language_paradigms/02_functional" frust_lang_build)
endif()

get_filename_component(REPO_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG   "${REPO_ROOT_DIR}/lib/Debug")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${REPO_ROOT_DIR}/lib/Release")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG   "${REPO_ROOT_DIR}/lib/Debug")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE "${REPO_ROOT_DIR}/lib/Release")

# ------------------------------------------------------------------------------
# node_compiler: graph JSON -> real .frust source text, validated by
# actually compiling it through frust_lang before returning success. See
# NodeCompiler.h for the full design writeup and graph JSON schema.
# ------------------------------------------------------------------------------
add_library(node_compiler STATIC
    include/node_compiler/NodeCompiler.h
    src/NodeCompiler.cpp
)

target_include_directories(node_compiler PUBLIC
    include
)

target_include_directories(node_compiler PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../01_language_paradigms/02_functional"
    # FRUST_GEN_DIR is a real CACHE variable - see frust_plugin_host's
    # CMakeLists.txt for the full explanation of why this can't be
    # reconstructed from CMAKE_CURRENT_BINARY_DIR.
    "${FRUST_GEN_DIR}"
    "${FRUST_FLEXLEXER_INCLUDE_DIR}"
)

if(WIN32)
    target_compile_definitions(node_compiler PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# frust_lang links juce_core PRIVATEly (via frate_lib) - link it
# explicitly here too, same reasoning as frust_plugin_host's manifest
# code (juce::var/juce::JSON for the graph format).
target_link_libraries(node_compiler PUBLIC frust_lang PRIVATE ${NODE_COMPILER_LLVM_LIBS} juce::juce_core)

install(TARGETS node_compiler ARCHIVE DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)

# ------------------------------------------------------------------------------
# Verification harness - compiles a handful of real graph JSON files,
# writes the generated .frust source to disk, and (via frust_plugin_host)
# actually loads and runs it, checking the numeric result. See
# examples/*.json and run_examples.cpp.
# ------------------------------------------------------------------------------
if(NOT TARGET frust_plugin_host)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../09_frust_plugin_host" frust_plugin_host_build)
endif()

add_executable(node_compiler_example examples/run_examples.cpp)
target_link_libraries(node_compiler_example PRIVATE node_compiler frust_plugin_host)
set_target_properties(node_compiler_example PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_BINARY_DIR}/examples/Debug"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_BINARY_DIR}/examples/Release"
)
