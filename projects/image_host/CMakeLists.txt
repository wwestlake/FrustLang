cmake_minimum_required(VERSION 3.15)
project(ImageDemoHost LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(ImageDemoHost
    Source/main.cpp
    ../frust_image_demo/build/frust_image_demo.o
)

target_include_directories(ImageDemoHost PRIVATE vendor)

target_link_libraries(ImageDemoHost PRIVATE legacy_stdio_definitions.lib)
