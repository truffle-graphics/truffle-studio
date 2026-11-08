cmake_minimum_required(VERSION 3.23)

project(
    TruffleStudio
    VERSION 0.1.0
    DESCRIPTION "Graphics authoring application built on the Truffle framework"
    LANGUAGES C CXX)

if(NOT APPLE AND NOT WIN32)
    message(FATAL_ERROR "Truffle Studio supports macOS (Apple Silicon) and Windows")
endif()
if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OBJCXX_STANDARD 20)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_EXTENSIONS OFF)

option(TFLST_WARNINGS_AS_ERRORS "Treat Studio and Truffle warnings as errors" OFF)
option(TFLST_BUILD_APP "Build the native Studio application" ON)

include(CTest)
list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(StudioOutput)
include(StudioDependencies)

add_subdirectory(src)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
