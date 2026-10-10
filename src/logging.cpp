#include "asyncwc/logging.hpp"

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <mutex>
#include <utility>

namespace asyncwc {
namespace {

constexpr auto logger_name = "asyncwc";

struct LoggerSlot {
    std::mutex mutex;
    std::shared_ptr<spdlog::logger> logger;
};

LoggerSlot& slot() {
    static LoggerSlot instance;
    return instance;
}

std::shared_ptr<spdlog::logger> make_default_logger() {
    if (auto registered = spdlog::get(logger_name)) {
        return registered;
    }
    return std::make_shared<spdlog::logger>(
        logger_name, std::make_shared<spdlog::sinks::stderr_color_sink_mt>());
}

} // namespace

std::shared_ptr<spdlog::logger> logger() {
    auto& current = slot();
    const std::scoped_lock lock{current.mutex};
    if (!current.logger) {
        current.logger = make_default_logger();
    }
    return current.logger;
}

void set_logger(std::shared_ptr<spdlog::logger> logger) {
    auto& current = slot();
    const std::scoped_lock lock{current.mutex};
    current.logger = std::move(logger);
}

} // namespace asyncwc
