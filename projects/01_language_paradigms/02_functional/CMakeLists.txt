cmake_minimum_required(VERSION 3.22)
project(FrustCompiler VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Release builds receive the tag version from the workflow. Source builds
# remain honest about not being a packaged release.
set(FRUST_BUILD_VERSION "development" CACHE STRING "FRust CLI build version")

# ------------------------------------------------------------------------------
# Locate LLVM from an already-installed package tree. Do not use vcpkg's
# CMake toolchain integration here: that path can decide to run vcpkg and
# rebuild LLVM. Frust consumes the existing vcpkg_installed/<triplet> tree
# produced by Creation Engine's one-time LLVM setup, then fails loudly if it
# cannot find LLVM there.
# ------------------------------------------------------------------------------
set(FRUST_VCPKG_INSTALLED_DIR "" CACHE PATH "Existing vcpkg_installed/<triplet> directory containing LLVM; Frust never runs vcpkg.")

if(WIN32)
    if(CMAKE_TOOLCHAIN_FILE MATCHES "[/\\\\]vcpkg\\.cmake$")
        message(FATAL_ERROR
            "Frust must not be configured with the vcpkg CMake toolchain file. "
            "That integration can rebuild LLVM. Use the windows-vcpkg preset, "
            "which points at the existing vcpkg_installed/x64-windows tree instead.")
    endif()

    if(NOT FRUST_VCPKG_INSTALLED_DIR)
        set(FRUST_VCPKG_INSTALLED_DIR "D:/000 Creation Suite/apps/CreationEngine/vcpkg_installed/x64-windows" CACHE PATH
            "Existing vcpkg_installed/<triplet> directory containing LLVM; Frust never runs vcpkg." FORCE)
    endif()

    if(NOT EXISTS "${FRUST_VCPKG_INSTALLED_DIR}/share/llvm/LLVMConfig.cmake")
        message(FATAL_ERROR
            "FRUST_VCPKG_INSTALLED_DIR must point at an existing vcpkg_installed/x64-windows tree "
            "that already contains share/llvm/LLVMConfig.cmake. Frust will not run vcpkg or rebuild LLVM.")
    endif()

    list(PREPEND CMAKE_PREFIX_PATH "${FRUST_VCPKG_INSTALLED_DIR}")
endif()

find_package(LLVM CONFIG REQUIRED)
message(STATUS "FrustCompiler: Found LLVM ${LLVM_PACKAGE_VERSION} at ${LLVM_DIR}")

if(WIN32)
    cmake_path(ABSOLUTE_PATH FRUST_VCPKG_INSTALLED_DIR NORMALIZE OUTPUT_VARIABLE FRUST_VCPKG_INSTALLED_ABS)
    cmake_path(ABSOLUTE_PATH LLVM_DIR NORMALIZE OUTPUT_VARIABLE FRUST_LLVM_DIR_ABS)
    cmake_path(IS_PREFIX FRUST_VCPKG_INSTALLED_ABS "${FRUST_LLVM_DIR_ABS}" NORMALIZE FRUST_LLVM_FROM_INSTALLED_TREE)
    if(NOT FRUST_LLVM_FROM_INSTALLED_TREE)
        message(FATAL_ERROR
            "LLVM was found at '${LLVM_DIR}', outside FRUST_VCPKG_INSTALLED_DIR='${FRUST_VCPKG_INSTALLED_DIR}'. "
            "Refusing to configure because Frust must consume the existing LLVM install only, never a source/buildtree/toolchain path.")
    endif()
endif()

# frate's AOT linker needs LLVM only when a pod pulls in the plugin host.
# Keep the build's discovered install root as a fallback; packaged builds can
# override it at runtime with FRUST_LLVM_ROOT.
get_filename_component(FRUST_LLVM_SHARE_DIR "${LLVM_DIR}" DIRECTORY)
get_filename_component(FRUST_LLVM_ROOT "${FRUST_LLVM_SHARE_DIR}" DIRECTORY)

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
# CACHE (not a plain variable) so consumers pulled in via
# add_subdirectory() from a DIFFERENT parent than expected (e.g.
# node_compiler's CMakeLists.txt adds both frust_lang and
# frust_plugin_host directly - frust_plugin_host can no longer assume
# its own CMAKE_CURRENT_BINARY_DIR/frust_lang_build/generated is where
# the real generated parser lives, since that add_subdirectory call
# happened at node_compiler's level, not frust_plugin_host's) can just
# reference ${FRUST_GEN_DIR} directly instead of reconstructing this
# path from an assumed binary-dir name - real, reproduced bug fixed
# here rather than patched around per-consumer.
set(FRUST_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated" CACHE PATH "Directory holding frust_lang's generated parser.hpp/lexer.cpp" FORCE)
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
    ASTHash.h
    Lexer.h
    Codegen.h
    Diagnostics.h
    CompilerApi.h
    CompilerApi.cpp
    HostEnvironment.h
    MemoryEnvironment.h
    EnvironmentCompile.h
    EnvironmentCompile.cpp
    "${CMAKE_CURRENT_SOURCE_DIR}/../../05_frate/include/frate/PodBuild.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../05_frate/src/PodBuild.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../05_frate/include/frate/PodEnvironment.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../05_frate/src/PodEnvironment.cpp"
    ReplSession.h
    ReplSession.cpp
    ModuleLoader.h
    ModuleLoader.cpp
    # The disk half of module loading. Its own object file inside frust_lang: a host that never
    # references it (an application using a HostEnvironment) does not get it linked in.
    DiskLoader.h
    DiskLoader.cpp
    "${FRUST_PARSER_CPP}"
    "${FRUST_LEXER_CPP}"
)

target_include_directories(frust_lang
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
           "${CMAKE_CURRENT_SOURCE_DIR}/../../05_frate/include"
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
# frust_runtime: the frust_* C exports core/src declares via extern fn and
# calls directly (frust_print_str, frust_format_*, frust_buf_*, plus the
# quote/unquote/build_time AST/JIT primitives). Extracted from what used to
# be duplicated verbatim in frust_compiler's AND the IDE's own Main.cpp - a real static
# library now, so a frate build-produced standalone linked executable has
# something to actually link these symbols from, not just JIT-time process-
# image lookup. Every consumer (frust_compiler, the IDE, and frate's own AOT
# link step) links this ONE copy instead of each defining its own. The basic
# exports and optional LLVM-backed metaprogramming exports are separate object
# files, so regular AOT programs do not pull LLVM unnecessarily.
# ------------------------------------------------------------------------------
add_library(frust_runtime STATIC RuntimeBasic.cpp RuntimeMeta.cpp)
target_include_directories(frust_runtime PRIVATE "${FRUST_GEN_DIR}" "${FRUST_FLEXLEXER_INCLUDE_DIR}")
if(WIN32)
    target_compile_definitions(frust_runtime PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
if(MSVC)
    # RuntimeBasic.cpp and RuntimeMeta.cpp share the target PDB in Debug.
    # /FS serializes that PDB write when MSBuild compiles them concurrently.
    target_compile_options(frust_runtime PRIVATE /FS)
endif()
target_link_libraries(frust_runtime PRIVATE frust_lang ${FRUST_LLVM_LIBS})

# ------------------------------------------------------------------------------
# frust_compiler: the CLI front end (file mode + a small REPL loop) on top
# of frust_lang. File mode needs Codegen/LLVM directly (to find and JIT-run
# a whole file's `main`), which is a CLI-only concern - ReplSession's public
# one-shot-evaluate() API is all a host app like the JUCE IDE needs.
# ------------------------------------------------------------------------------
add_executable(frust_compiler Main.cpp)
target_compile_definitions(frust_compiler PRIVATE
    "FRUST_BUILD_VERSION=\"${FRUST_BUILD_VERSION}\"")

# Main.cpp's FRUST_RUNTIME_EXPORT symbols (frust_format_i64 etc.) need to be
# visible to the JIT's runtime symbol lookup against this process's own
# image. dllexport alone handles that on Windows, but on Linux an
# executable's global symbols aren't in the dynamic symbol table by default
# regardless of visibility attributes - ENABLE_EXPORTS adds -rdynamic there
# (a no-op on Windows, where dllexport is already doing the job).
set_target_properties(frust_compiler PROPERTIES OUTPUT_NAME "frust_compiler_x" ENABLE_EXPORTS ON)

target_include_directories(frust_compiler PRIVATE
    "${FRUST_GEN_DIR}"
    "${FRUST_FLEXLEXER_INCLUDE_DIR}"
)

if(WIN32)
    target_compile_definitions(frust_compiler PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
target_link_libraries(frust_compiler PRIVATE frust_lang frust_runtime ${FRUST_LLVM_LIBS})

# ------------------------------------------------------------------------------
# frust_compiler_api_smoke: checks the embeddable compiler (CompilerApi.h) -
# source text in, diagnostics and object bytes out, and that it touches no file.
# Exit code 0 = pass.
# ------------------------------------------------------------------------------
add_executable(frust_compiler_api_smoke tests/CompilerApiSmoke.cpp)
target_include_directories(frust_compiler_api_smoke PRIVATE "${FRUST_GEN_DIR}" "${FRUST_FLEXLEXER_INCLUDE_DIR}")
if(WIN32)
    target_compile_definitions(frust_compiler_api_smoke PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
target_link_libraries(frust_compiler_api_smoke PRIVATE frust_lang frust_runtime ${FRUST_LLVM_LIBS})

# frate_pod_build_smoke: building a pod entirely in memory (frate/PodBuild.h,
# frate/PodArchive.h) - pod files in, diagnostics and object bytes out, no file touched.
add_executable(frate_pod_build_smoke "${CMAKE_CURRENT_SOURCE_DIR}/../../05_frate/tests/PodBuildSmoke.cpp")
target_include_directories(frate_pod_build_smoke PRIVATE "${FRUST_GEN_DIR}" "${FRUST_FLEXLEXER_INCLUDE_DIR}")
if(WIN32)
    target_compile_definitions(frate_pod_build_smoke PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
target_link_libraries(frate_pod_build_smoke PRIVATE frust_lang frust_runtime frate_lib ${FRUST_LLVM_LIBS})

# frust_environment_smoke: the compiler and Frate over a HostEnvironment (files and log through the
# environment only) - see HostEnvironment.h.
add_executable(frust_environment_smoke tests/EnvironmentSmoke.cpp)
target_include_directories(frust_environment_smoke PRIVATE "${FRUST_GEN_DIR}" "${FRUST_FLEXLEXER_INCLUDE_DIR}")
if(WIN32)
    target_compile_definitions(frust_environment_smoke PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
target_link_libraries(frust_environment_smoke PRIVATE frust_lang frust_runtime frate_lib ${FRUST_LLVM_LIBS})

# ------------------------------------------------------------------------------
# Install - on Linux, CMAKE_INSTALL_PREFIX is set by the "linux" preset to
# the shared drive (/mnt/dev/common/user), so `ninja install` (or
# `cmake --build --target install`) puts frust_compiler on every Pi's PATH
# via one shared copy, no per-machine build needed. RUNTIME/ARCHIVE are the
# standard CMake install-component names - RUNTIME is executables (-> bin),
# ARCHIVE is static libs (-> lib).
# ------------------------------------------------------------------------------
install(TARGETS frust_compiler RUNTIME DESTINATION bin)
install(TARGETS frust_lang ARCHIVE DESTINATION lib)
install(TARGETS frust_runtime ARCHIVE DESTINATION lib)
