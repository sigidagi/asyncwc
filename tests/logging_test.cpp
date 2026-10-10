#include "asyncwc/logging.hpp"

#include <gtest/gtest.h>

#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

namespace asyncwc {
namespace {

class LoggingTest : public ::testing::Test {
protected:
    void SetUp() override { saved_ = logger(); }

    void TearDown() override {
        spdlog::drop("asyncwc");
        set_logger(saved_);
    }

private:
    std::shared_ptr<spdlog::logger> saved_;
};

TEST_F(LoggingTest, DefaultLoggerIsNamedAsyncwc) {
    set_logger(nullptr);
    const auto log = logger();
    ASSERT_NE(log, nullptr);
    EXPECT_EQ(log->name(), "asyncwc");
    EXPECT_EQ(logger(), log); // created once, then reused
}

TEST_F(LoggingTest, SetLoggerReplacesTheLogger) {
    auto custom =
        std::make_shared<spdlog::logger>("custom", std::make_shared<spdlog::sinks::null_sink_mt>());
    set_logger(custom);
    EXPECT_EQ(logger(), custom);
}

TEST_F(LoggingTest, DefaultPrefersARegisteredAsyncwcLogger) {
    auto registered = std::make_shared<spdlog::logger>(
        "asyncwc", std::make_shared<spdlog::sinks::null_sink_mt>());
    spdlog::register_logger(registered);
    set_logger(nullptr);
    EXPECT_EQ(logger(), registered);
}

} // namespace
} // namespace asyncwc
