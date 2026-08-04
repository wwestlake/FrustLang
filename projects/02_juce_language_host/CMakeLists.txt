cmake_minimum_required(VERSION 3.22)
project(JuceLanguageHost VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------------------------------------------------------------------
# Locate LLVM Package (Windows or Linux)
# ------------------------------------------------------------------------------
if(WIN32)
    set(_default_llvm_dir "D:/vcpkg/packages/llvm_x64-windows")
    if(EXISTS "${_default_llvm_dir}" AND NOT LLVM_DIR)
        set(LLVM_DIR "${_default_llvm_dir}/share/llvm")
        list(APPEND CMAKE_PREFIX_PATH "${_default_llvm_dir}")
    endif()
elseif(UNIX)
    set(_default_llvm_dir "$ENV{HOME}/llvm-lagdaemon-install")
    if(EXISTS "${_default_llvm_dir}" AND NOT LLVM_DIR)
        set(LLVM_DIR "${_default_llvm_dir}/lib/cmake/llvm")
        list(APPEND CMAKE_PREFIX_PATH "${_default_llvm_dir}")
    endif()
endif()

find_package(LLVM CONFIG QUIET)
if(LLVM_FOUND)
    message(STATUS "JuceLanguageHost: Found LLVM ${LLVM_PACKAGE_VERSION} at ${LLVM_DIR}")
    list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
    include(AddLLVM)
    add_definitions(${LLVM_DEFINITIONS})
    include_directories(${LLVM_INCLUDE_DIRS})
else()
    message(WARNING "JuceLanguageHost: LLVM not found. Build will proceed without LLVM JIT integration.")
endif()

# ------------------------------------------------------------------------------
# Locate Local JUCE Installation
# ------------------------------------------------------------------------------
if(EXISTS "D:/JUCE/CMakeLists.txt")
    message(STATUS "JuceLanguageHost: Using local JUCE installation at D:/JUCE")
    add_subdirectory("D:/JUCE" juce_build)
elseif(EXISTS "$ENV{JUCE_PATH}")
    add_subdirectory("$ENV{JUCE_PATH}" juce_build)
else()
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.0
    )
    FetchContent_MakeAvailable(JUCE)
endif()

# ------------------------------------------------------------------------------
# Add JUCE GUI Application Target
# ------------------------------------------------------------------------------
juce_add_gui_app(JuceLanguageHost
    PRODUCT_NAME "LagDaemon Language Host Workbench"
    VERSION 0.1.0
)

target_sources(JuceLanguageHost PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/MainComponent.h
)

target_compile_definitions(JuceLanguageHost PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(JuceLanguageHost PRIVATE
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
)

if(LLVM_FOUND)
    target_compile_definitions(JuceLanguageHost PRIVATE HAS_LLVM=1)
    target_link_libraries(JuceLanguageHost PRIVATE
        LLVMOrcJIT
        LLVMExecutionEngine
        LLVMSupport
        LLVMCore
        LLVMX86CodeGen
    )
endif()

juce_generate_juce_header(JuceLanguageHost)
