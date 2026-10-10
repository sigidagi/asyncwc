cmake_minimum_required(VERSION 3.25)

project(asyncwc
    VERSION 1.0.0
    DESCRIPTION "Asynchronous ws:// WebSocket client built on C++20 coroutines and Boost.Asio"
    LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(ASYNCWC_BUILD_TESTS "Build the unit tests" ${PROJECT_IS_TOP_LEVEL})
option(ASYNCWC_BUILD_EXAMPLES "Build the examples" ${PROJECT_IS_TOP_LEVEL})
option(ASYNCWC_ENABLE_COVERAGE "Instrument the build and add the 'coverage' target" OFF)
option(ASYNCWC_ENABLE_CLANG_TIDY "Run clang-tidy on every translation unit while compiling" OFF)
option(ASYNCWC_ENABLE_SANITIZERS "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(ASYNCWC_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(ASYNCWC_INSTALL "Generate the install target" ${PROJECT_IS_TOP_LEVEL})

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

include(GNUInstallDirs)
include(Dependencies)
include(CompilerWarnings)
include(Sanitizers)
include(Coverage)
include(ClangTools)

add_subdirectory(src)

if(ASYNCWC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(ASYNCWC_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(ASYNCWC_INSTALL)
    include(Install)
endif()
