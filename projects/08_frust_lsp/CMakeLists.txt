cmake_minimum_required(VERSION 3.22)
project(FrustLsp VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------------------------------------------------------------------
# frust_lsp links frust_lang directly (Lexer/Parser/AstArena/AST.h) - the
# exact same low-level parsing API frust_compiler's Main.cpp uses, not
# ReplSession (that API only returns numeric one-shot eval results, the
# wrong shape for a language server needing full parse trees). Bringing in
# frust_lang via add_subdirectory also transitively brings in LLVM,
# flex/bison, and JUCE (frust_lang -> frate_lib -> juce_core) - all
# resolved via CMAKE_PREFIX_PATH/JUCE_PATH/FLEX_EXECUTABLE/BISON_EXECUTABLE
# supplied by CMakePresets.json, same as every other project in this repo
# that depends on frust_lang (see the IDE's CMakeLists.txt for the same
# pattern). frust_lsp's own code never touches Codegen.h/LLVM directly, but
# there's no value in carving out a codegen-free subset of frust_lang just
# for this - not worth the maintenance cost.
# ------------------------------------------------------------------------------
if(NOT TARGET frust_lang)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../01_language_paradigms/02_functional" frust_lang_build)
endif()

# ------------------------------------------------------------------------------
# Route Build Output to the Top-Level /bin Folder (Debug & Release)
# ------------------------------------------------------------------------------
get_filename_component(REPO_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${REPO_ROOT_DIR}/bin/Debug")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${REPO_ROOT_DIR}/bin/Release")

# ------------------------------------------------------------------------------
# frust_lsp: a Language Server Protocol implementation for Frust, speaking
# LSP over stdio (Content-Length-framed JSON-RPC) so any LSP-aware editor
# can use it - the VSCode extension in ../07_vscode_frust launches this as
# a subprocess via vscode-languageclient.
# ------------------------------------------------------------------------------
add_executable(frust_lsp Main.cpp)

target_include_directories(frust_lsp PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../01_language_paradigms/02_functional"
    # FRUST_GEN_DIR is now a real CACHE variable (02_functional/
    # CMakeLists.txt) holding wherever frust_lang's generated parser.hpp
    # actually lives - was previously reconstructed by assuming this
    # file's own CMAKE_CURRENT_BINARY_DIR/frust_lang_build/generated,
    # which broke as soon as a THIRD project (node_compiler) started
    # pulling in both frust_lang and a consumer of it at different
    # nesting levels. Fixed at the source (a real CACHE var) rather than
    # patched per-consumer.
    "${FRUST_GEN_DIR}"
    "${FRUST_FLEXLEXER_INCLUDE_DIR}"
)

if(WIN32)
    target_compile_definitions(frust_lsp PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# frust_lang links juce_core PRIVATEly (via frate_lib), so its include
# paths don't propagate here - frust_lsp uses juce::var/juce::JSON
# directly for LSP's JSON-RPC messages, so link it explicitly too.
target_link_libraries(frust_lsp PRIVATE frust_lang juce::juce_core)

# Same shared-drive install location as frust_compiler/frate - see
# 01_language_paradigms/02_functional/CMakeLists.txt for the full
# explanation of CMAKE_INSTALL_PREFIX on Linux.
install(TARGETS frust_lsp RUNTIME DESTINATION bin)
