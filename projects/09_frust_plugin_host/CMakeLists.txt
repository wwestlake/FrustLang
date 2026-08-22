cmake_minimum_required(VERSION 3.22)
project(FrustPluginHost VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------------------------------------------------------------------
# Locate LLVM - frust_lang links its own LLVM component libs PRIVATEly (see
# its CMakeLists.txt), so they don't propagate to us via target_link_libraries
# even though we link frust_lang - same reason frust_compiler's own
# CMakeLists.txt does this exact find_package/llvm_map_components_to_libnames
# call itself instead of trying to reuse frust_lang's. FRUST_LLVM_LIBS is
# also a plain (non-CACHE) variable scoped to frust_lang's own CMakeLists.txt
# - add_subdirectory() below doesn't make it visible here either.
# ------------------------------------------------------------------------------
find_package(LLVM CONFIG REQUIRED)
message(STATUS "FrustPluginHost: Found LLVM ${LLVM_PACKAGE_VERSION} at ${LLVM_DIR}")

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)
add_definitions(${LLVM_DEFINITIONS})
include_directories(${LLVM_INCLUDE_DIRS})

llvm_map_components_to_libnames(FRUST_PLUGIN_HOST_LLVM_LIBS
    Core
    ExecutionEngine
    OrcJIT
    Support
    native
    nativecodegen
)

# ------------------------------------------------------------------------------
# frust_plugin_host links frust_lang directly (Lexer/Parser/AstArena/AST.h/
# Codegen.h) - same pattern as frust_compiler/frust_lsp.
# ------------------------------------------------------------------------------
if(NOT TARGET frust_lang)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../01_language_paradigms/02_functional" frust_lang_build)
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
# frust_plugin_host: embed Frust in any C/C++ app - load/unload/hot-reload
# .frust source as isolated JIT-compiled plugins, or just use it as a
# general hosted scripting language. See FrustPluginHost.h for the full
# design writeup.
# ------------------------------------------------------------------------------
add_library(frust_plugin_host STATIC
    include/frust_plugin_host/FrustPluginHost.h
    src/FrustPluginHost.cpp
    include/frust_plugin_host/FrustPluginManifest.h
    src/FrustPluginManifest.cpp
)

target_include_directories(frust_plugin_host PUBLIC
    include
)

target_include_directories(frust_plugin_host PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../01_language_paradigms/02_functional"
    # FRUST_GEN_DIR is a real CACHE variable (02_functional/CMakeLists.txt)
    # holding wherever frust_lang's generated parser.hpp actually lives -
    # NOT necessarily a subdirectory of this file's own
    # CMAKE_CURRENT_BINARY_DIR (a real, reproduced bug when this library
    # is pulled in via add_subdirectory() from node_compiler, which adds
    # frust_lang itself first at ITS OWN level).
    "${FRUST_GEN_DIR}"
    "${FRUST_FLEXLEXER_INCLUDE_DIR}"
)

if(WIN32)
    target_compile_definitions(frust_plugin_host PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# frust_lang links juce_core PRIVATEly (via frate_lib), so it doesn't
# propagate here - FrustPluginManifest.cpp uses juce::var/juce::JSON
# directly for plugin.json (same reasoning/pattern as frust_lsp's
# CMakeLists.txt for its LSP JSON-RPC messages).
target_link_libraries(frust_plugin_host PUBLIC frust_lang PRIVATE ${FRUST_PLUGIN_HOST_LLVM_LIBS} juce::juce_core)

install(TARGETS frust_plugin_host ARCHIVE DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)

# ------------------------------------------------------------------------------
# A real end-to-end usage example, doubling as the verification harness
# for this library - see examples/host_example.cpp for what it actually
# checks (host->plugin calls, plugin->host extern calls, unload, and a
# genuine hot-reload where the plugin source changes on disk between
# calls).
# ------------------------------------------------------------------------------
add_executable(frust_plugin_host_example examples/host_example.cpp)
target_link_libraries(frust_plugin_host_example PRIVATE frust_plugin_host)
set_target_properties(frust_plugin_host_example PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_BINARY_DIR}/examples/Debug"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_BINARY_DIR}/examples/Release"
)

# Phase 2 verification harness - plugin manifest + lifecycle convenience
# wrappers. See lifecycle_example.cpp/lifecycle_plugin.frust/.json.
add_executable(frust_plugin_host_lifecycle_example examples/lifecycle_example.cpp)
target_link_libraries(frust_plugin_host_lifecycle_example PRIVATE frust_plugin_host)
set_target_properties(frust_plugin_host_lifecycle_example PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_BINARY_DIR}/examples/Debug"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_BINARY_DIR}/examples/Release"
)

# AST-content-hash reload skip (FRUST_LANG_SPEC.md 1.4) verification
# harness. See reload_example.cpp/reload_plugin_v1.frust/reload_plugin_v2.frust.
add_executable(frust_plugin_host_reload_example examples/reload_example.cpp)
target_link_libraries(frust_plugin_host_reload_example PRIVATE frust_plugin_host)
set_target_properties(frust_plugin_host_reload_example PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_BINARY_DIR}/examples/Debug"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_BINARY_DIR}/examples/Release"
)

# Automation-layer host-call verification harness (FRUST_LANG_SPEC.md
# section 4) - proves the exact mechanism the JUCE IDE's AutomationPanel
# uses: a native host constructing an interface-implementing struct and
# calling its methods directly by symbol name, no interface/vtable
# knowledge needed host-side. See automation_host_example.cpp.
add_executable(frust_plugin_host_automation_example examples/automation_host_example.cpp)
target_link_libraries(frust_plugin_host_automation_example PRIVATE frust_plugin_host)
set_target_properties(frust_plugin_host_automation_example PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_BINARY_DIR}/examples/Debug"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_BINARY_DIR}/examples/Release"
)

# Event subscription verification harness (docs/17-Plugin-Automation-Layer.md).
# See event_example.cpp/event_plugin.frust.
add_executable(frust_plugin_host_event_example examples/event_example.cpp)
target_link_libraries(frust_plugin_host_event_example PRIVATE frust_plugin_host)
set_target_properties(frust_plugin_host_event_example PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_BINARY_DIR}/examples/Debug"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_BINARY_DIR}/examples/Release"
)

# Plugin-to-plugin service discovery verification harness
# (docs/17-Plugin-Automation-Layer.md section 4.3). See
# service_example.cpp/service_provider.frust/service_consumer.frust.
add_executable(frust_plugin_host_service_example examples/service_example.cpp)
target_link_libraries(frust_plugin_host_service_example PRIVATE frust_plugin_host)
set_target_properties(frust_plugin_host_service_example PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_BINARY_DIR}/examples/Debug"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_BINARY_DIR}/examples/Release"
)

# Host-advertised-capabilities verification harness: the same service
# registry used the other direction - the host registers real file I/O
# capabilities, a plugin discovers and drives them entirely on its own.
# See host_file_service_example.cpp/file_reader_plugin.frust/sample_data.csv.
add_executable(frust_plugin_host_file_service_example examples/host_file_service_example.cpp)
target_link_libraries(frust_plugin_host_file_service_example PRIVATE frust_plugin_host)
set_target_properties(frust_plugin_host_file_service_example PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_BINARY_DIR}/examples/Debug"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_BINARY_DIR}/examples/Release"
)
