set(ASYNCWC_GENERATED_INCLUDE_DIR "${PROJECT_BINARY_DIR}/generated/include")
set(ASYNCWC_GENERATED_INCLUDE_DIR "${ASYNCWC_GENERATED_INCLUDE_DIR}" PARENT_SCOPE)

configure_file(
    "${PROJECT_SOURCE_DIR}/include/asyncwc/version.hpp.in"
    "${ASYNCWC_GENERATED_INCLUDE_DIR}/asyncwc/version.hpp"
    @ONLY)

add_library(asyncwc
    backoff.cpp
    client.cpp
    config.cpp
    connection.cpp
    error.cpp
    logging.cpp
    message.cpp
    url.cpp)
add_library(asyncwc::asyncwc ALIAS asyncwc)

target_include_directories(asyncwc
    PUBLIC
        "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
        "$<BUILD_INTERFACE:${ASYNCWC_GENERATED_INCLUDE_DIR}>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}")

# std::expected is C++23; the coroutine machinery itself only needs C++20.
target_compile_features(asyncwc PUBLIC cxx_std_23)

target_link_libraries(asyncwc
    PUBLIC
        Boost::headers
        spdlog::spdlog
        Threads::Threads
    PRIVATE
        Boost::url)

target_compile_definitions(asyncwc PUBLIC BOOST_ASIO_NO_DEPRECATED)

set_target_properties(asyncwc PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    EXPORT_NAME asyncwc)

asyncwc_set_warnings(asyncwc)
asyncwc_enable_clang_tidy(asyncwc)
