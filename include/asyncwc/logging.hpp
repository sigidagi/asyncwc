#pragma once

#include <spdlog/logger.h>

#include <memory>

namespace asyncwc {

/// The library wide logger used when `ConnectionOptions::logger` is not set.
///
/// Unless replaced through `set_logger()`, this is the spdlog logger registered under
/// the name "asyncwc" or, if there is none, a colored stderr logger with that name.
/// Adjust verbosity with e.g. `asyncwc::logger()->set_level(spdlog::level::debug)`.
[[nodiscard]] std::shared_ptr<spdlog::logger> logger();

/// Replaces the library wide logger; `nullptr` restores the default. Thread-safe.
void set_logger(std::shared_ptr<spdlog::logger> logger);

} // namespace asyncwc
