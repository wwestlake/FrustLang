cmake_minimum_required(VERSION 3.22)
project(FrustCompiler VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------------------------------------------------------------------
# Locate LLVM - no hardcoded machine-specific paths here. CMAKE_PREFIX_PATH
# (and, on Windows, FLEX_EXECUTABLE/BISON_EXECUTABLE/FRUST_WINFLEXBISON_DIR
# below) come from the active CMakePresets.json preset instead - see that
# file for the per-platform values.
# ------------------------------------------------------------------------------
find_package(LLVM CONFIG REQUIRED)
message(STATUS "FrustCompiler: Found LLVM ${LLVM_PACKAGE_VERSION} at ${LLVM_DIR}")

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)
add_definitions(${LLVM_DEFINITIONS})
include_directories(${LLVM_INCLUDE_DIRS})

llvm_map_components_to_libnames(FRUST_LLVM_LIBS
    Core
    ExecutionEngine
    OrcJIT
    Support
    native
    nativecodegen
)

# ------------------------------------------------------------------------------
# Route Build Output to the Top-Level /bin and /lib Folders (Debug & Release)
# ------------------------------------------------------------------------------
get_filename_component(REPO_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${REPO_ROOT_DIR}/bin/Debug")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${REPO_ROOT_DIR}/bin/Release")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG   "${REPO_ROOT_DIR}/lib/Debug")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${REPO_ROOT_DIR}/lib/Release")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG   "${REPO_ROOT_DIR}/lib/Debug")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE "${REPO_ROOT_DIR}/lib/Release")

# ------------------------------------------------------------------------------
# Locate flex/bison via CMake's own standard find modules - portable by
# construction. On Linux these find system flex/bison with zero
# configuration. On Windows, FLEX_EXECUTABLE/BISON_EXECUTABLE are set
# directly by the "windows-vcpkg" preset (pointing at WinFlexBison), which
# these find_package() calls respect and skip searching for.
# ------------------------------------------------------------------------------
find_package(FLEX REQUIRED)
find_package(BISON REQUIRED)

set(FRUST_BISON_EXE "${BISON_EXECUTABLE}")
set(FRUST_FLEX_EXE "${FLEX_EXECUTABLE}")
message(STATUS "Frust grammar: bison=${FRUST_BISON_EXE} flex=${FRUST_FLEX_EXE}")

# FlexLexer.h: found automatically on Linux (ships with libfl-dev). On
# Windows, WinFlexBison ships it next to win_flex.exe - FRUST_WINFLEXBISON_DIR
# is set by the "windows-vcpkg" preset and used here only as a search hint,
# so this call still works unmodified on Linux where that variable is unset.
set(FRUST_WINFLEXBISON_DIR "" CACHE PATH "Directory containing FlexLexer.h (Windows/WinFlexBison only)")
find_path(FRUST_FLEXLEXER_INCLUDE_DIR NAMES FlexLexer.h HINTS "${FRUST_WINFLEXBISON_DIR}")
if(NOT FRUST_FLEXLEXER_INCLUDE_DIR)
    message(FATAL_ERROR
        "FlexLexer.h not found. On Windows, set FRUST_WINFLEXBISON_DIR to the "
        "directory containing it (see the windows-vcpkg preset in CMakePresets.json).")
endif()

# ------------------------------------------------------------------------------
# Generate the lexer/parser from grammar/frust.l and grammar/frust.y
# ------------------------------------------------------------------------------
set(FRUST_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${FRUST_GEN_DIR}")

set(FRUST_PARSER_CPP "${FRUST_GEN_DIR}/parser.cpp")
set(FRUST_PARSER_HPP "${FRUST_GEN_DIR}/parser.hpp")
set(FRUST_LOCATION_HH "${FRUST_GEN_DIR}/location.hh")
set(FRUST_LEXER_CPP "${FRUST_GEN_DIR}/lexer.cpp")

add_custom_command(
    OUTPUT "${FRUST_PARSER_CPP}" "${FRUST_PARSER_HPP}" "${FRUST_LOCATION_HH}"
    COMMAND "${FRUST_BISON_EXE}"
            "--defines=${FRUST_PARSER_HPP}"
            "--output=${FRUST_PARSER_CPP}"
            "${CMAKE_CURRENT_SOURCE_DIR}/grammar/frust.y"
    WORKING_DIRECTORY "${FRUST_GEN_DIR}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/grammar/frust.y"
    COMMENT "Frust: generating parser from frust.y (bison)"
    VERBATIM
)

add_custom_command(
    OUTPUT "${FRUST_LEXER_CPP}"
    COMMAND "${FRUST_FLEX_EXE}"
            "--outfile=${FRUST_LEXER_CPP}"
            "${CMAKE_CURRENT_SOURCE_DIR}/grammar/frust.l"
    WORKING_DIRECTORY "${FRUST_GEN_DIR}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/grammar/frust.l" "${FRUST_PARSER_HPP}"
    COMMENT "Frust: generating lexer from frust.l (flex)"
    VERBATIM
)

# ------------------------------------------------------------------------------
# frust_lang: the actual reusable compiler/JIT - lexer, parser, AST, codegen,
# and the ReplSession public API. This is what a host app (the JUCE IDE)
# links directly for in-process evaluation - see ReplSession.h; nothing
# LLVM/AST/bison-shaped leaks into its public interface, so linking this
# library is all a consumer needs, no separate LLVM include/link setup.
# ------------------------------------------------------------------------------
add_library(frust_lang STATIC
    AST.h
    Lexer.h
    Codegen.h
    ReplSession.h
    ReplSession.cpp
    ModuleLoader.h
    ModuleLoader.cpp
    "${FRUST_PARSER_CPP}"
    "${FRUST_LEXER_CPP}"
)

target_include_directories(frust_lang
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${FRUST_GEN_DIR}" "${FRUST_FLEXLEXER_INCLUDE_DIR}"
)

if(NOT TARGET frate_lib)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../05_frate" frate_build)
endif()

if(WIN32)
    target_compile_definitions(frust_lang PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
target_link_libraries(frust_lang PRIVATE ${FRUST_LLVM_LIBS} frate_lib)

# Generated parser/lexer code isn't ours to keep warning-clean - silence it,
# with the right flag for whichever compiler is actually running.
set_source_files_properties("${FRUST_PARSER_CPP}" "${FRUST_LEXER_CPP}" PROPERTIES
    COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/W0,-w>")

# ------------------------------------------------------------------------------
# frust_compiler: the CLI front end (file mode + a small REPL loop) on top
# of frust_lang. File mode needs Codegen/LLVM directly (to find and JIT-run
# a whole file's `main`), which is a CLI-only concern - ReplSession's public
# one-shot-evaluate() API is all a host app like the JUCE IDE needs.
# ------------------------------------------------------------------------------
add_executable(frust_compiler Main.cpp)

# Main.cpp's FRUST_RUNTIME_EXPORT symbols (frust_format_i64 etc.) need to be
# visible to the JIT's runtime symbol lookup against this process's own
# image. dllexport alone handles that on Windows, but on Linux an
# executable's global symbols aren't in the dynamic symbol table by default
# regardless of visibility attributes - ENABLE_EXPORTS adds -rdynamic there
# (a no-op on Windows, where dllexport is already doing the job).
set_target_properties(frust_compiler PROPERTIES ENABLE_EXPORTS ON)

target_include_directories(frust_compiler PRIVATE
    "${FRUST_GEN_DIR}"
    "${FRUST_FLEXLEXER_INCLUDE_DIR}"
)

if(WIN32)
    target_compile_definitions(frust_compiler PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
target_link_libraries(frust_compiler PRIVATE frust_lang ${FRUST_LLVM_LIBS})
