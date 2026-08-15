cmake_minimum_required(VERSION 3.22)
project(FrustCompiler VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------------------------------------------------------------------
# Locate LLVM (same vcpkg search paths already proven to work for this
# machine's other LLVM-using targets - see dev-toolchain-capabilities memory)
# ------------------------------------------------------------------------------
if(WIN32)
    set(_llvm_search_paths
        "D:/000 Creation Suite/apps/CreationEngine/vcpkg_installed/x64-windows/share/llvm"
        "D:/vcpkg/buildtrees/llvm/x64-windows-rel/lib/cmake/llvm"
        "D:/vcpkg/packages/llvm_x64-windows/share/llvm"
    )
    foreach(_path ${_llvm_search_paths})
        if(EXISTS "${_path}/LLVMConfig.cmake" OR EXISTS "${_path}/llvm-config.cmake")
            set(LLVM_DIR "${_path}" CACHE PATH "LLVM Config Directory" FORCE)
            list(APPEND CMAKE_PREFIX_PATH "${_path}")
            break()
        endif()
    endforeach()

    include_directories(
        "D:/000 Creation Suite/apps/CreationEngine/vcpkg_installed/x64-windows/include"
        "D:/vcpkg/packages/llvm_x64-windows/include"
    )
    link_directories(
        "D:/000 Creation Suite/apps/CreationEngine/vcpkg_installed/x64-windows/lib"
    )
endif()

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
    X86CodeGen
    X86AsmParser
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
# Locate WinFlexBison
# Default is the known-good standalone install this machine already has
# (see the dev-toolchain-capabilities memory record); override with
# -DFRUST_WINFLEXBISON_DIR=<dir> if it ever moves.
# ------------------------------------------------------------------------------
set(FRUST_WINFLEXBISON_DIR "D:/tools/winflexbison" CACHE PATH "Directory containing win_bison.exe / win_flex.exe")

find_program(FRUST_BISON_EXE NAMES win_bison bison PATHS "${FRUST_WINFLEXBISON_DIR}" NO_DEFAULT_PATH)
find_program(FRUST_FLEX_EXE  NAMES win_flex flex   PATHS "${FRUST_WINFLEXBISON_DIR}" NO_DEFAULT_PATH)

if(NOT FRUST_BISON_EXE OR NOT FRUST_FLEX_EXE)
    message(FATAL_ERROR
        "Frust grammar tooling not found (bison=${FRUST_BISON_EXE} flex=${FRUST_FLEX_EXE}). "
        "Set -DFRUST_WINFLEXBISON_DIR=<directory containing win_bison.exe/win_flex.exe>.")
endif()

message(STATUS "Frust grammar: bison=${FRUST_BISON_EXE} flex=${FRUST_FLEX_EXE}")

find_path(FRUST_FLEXLEXER_INCLUDE_DIR NAMES FlexLexer.h PATHS "${FRUST_WINFLEXBISON_DIR}" NO_DEFAULT_PATH)
if(NOT FRUST_FLEXLEXER_INCLUDE_DIR)
    message(FATAL_ERROR "FlexLexer.h not found in ${FRUST_WINFLEXBISON_DIR}.")
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

target_compile_definitions(frust_lang PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(frust_lang PRIVATE ${FRUST_LLVM_LIBS} frate_lib)

set_source_files_properties("${FRUST_PARSER_CPP}" "${FRUST_LEXER_CPP}" PROPERTIES COMPILE_OPTIONS "/W0")

# ------------------------------------------------------------------------------
# frust_compiler: the CLI front end (file mode + a small REPL loop) on top
# of frust_lang. File mode needs Codegen/LLVM directly (to find and JIT-run
# a whole file's `main`), which is a CLI-only concern - ReplSession's public
# one-shot-evaluate() API is all a host app like the JUCE IDE needs.
# ------------------------------------------------------------------------------
add_executable(frust_compiler Main.cpp)

target_include_directories(frust_compiler PRIVATE
    "${FRUST_GEN_DIR}"
    "${FRUST_FLEXLEXER_INCLUDE_DIR}"
)

target_compile_definitions(frust_compiler PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(frust_compiler PRIVATE frust_lang ${FRUST_LLVM_LIBS})
