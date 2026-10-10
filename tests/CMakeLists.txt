find_package(GTest 1.14 CONFIG QUIET)
if(NOT GTest_FOUND)
    message(STATUS "asyncwc: GoogleTest not found on the system, fetching it")
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.17.0
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(googletest)
    add_library(GTest::gtest ALIAS gtest)
endif()

add_executable(asyncwc_tests
    main.cpp
    support/test_server.cpp
    async_mutex_test.cpp
    backoff_test.cpp
    client_test.cpp
    config_test.cpp
    connection_test.cpp
    error_test.cpp
    logging_test.cpp
    message_test.cpp
    url_test.cpp)

# The tests also exercise the internal helpers under src/detail.
target_include_directories(asyncwc_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/src")

target_link_libraries(asyncwc_tests PRIVATE asyncwc::asyncwc GTest::gtest)

asyncwc_set_warnings(asyncwc_tests)
asyncwc_enable_clang_tidy(asyncwc_tests)

include(GoogleTest)
gtest_discover_tests(asyncwc_tests
    DISCOVERY_TIMEOUT 30
    PROPERTIES TIMEOUT 60)
