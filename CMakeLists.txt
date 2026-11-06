cmake_minimum_required(VERSION 3.23)

project(
    TruffleStudio
    VERSION 0.1.0
    DESCRIPTION "Graphics authoring application built on the Truffle framework"
    LANGUAGES C CXX)

if(NOT APPLE)
    message(FATAL_ERROR "Truffle Studio currently targets macOS (Apple Silicon) with Metal only")
endif()
enable_language(OBJCXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OBJCXX_STANDARD 20)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_EXTENSIONS OFF)

option(STUDIO_WARNINGS_AS_ERRORS "Treat Studio and Truffle warnings as errors" OFF)

include(CTest)
list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(StudioOutput)
include(StudioDependencies)

add_subdirectory(src)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
