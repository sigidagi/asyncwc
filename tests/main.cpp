#include "asyncwc/logging.hpp"

#include <gtest/gtest.h>

#include <cstdlib>

// Library logging is silenced by default; run with e.g. ASYNCWC_LOG_LEVEL=debug to see it.
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    const char* level = std::getenv("ASYNCWC_LOG_LEVEL"); // NOLINT(concurrency-mt-unsafe)
    asyncwc::logger()->set_level(level != nullptr ? spdlog::level::from_str(level)
                                                  : spdlog::level::off);
    return RUN_ALL_TESTS();
}
