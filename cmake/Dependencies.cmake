include_guard(GLOBAL)

include(FetchContent)

find_package(Threads REQUIRED)

# Asio and Beast are header-only. Boost.URL (compiled) parses the ws:// URLs.
find_package(Boost 1.83 REQUIRED CONFIG COMPONENTS url)

# Prefer a system installation of spdlog and fall back to fetching it.
find_package(spdlog 1.12 CONFIG QUIET)
if(NOT spdlog_FOUND)
    message(STATUS "asyncwc: spdlog not found on the system, fetching it")
    set(SPDLOG_INSTALL ${ASYNCWC_INSTALL} CACHE BOOL "" FORCE)
    FetchContent_Declare(spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG v1.15.3
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(spdlog)
endif()
