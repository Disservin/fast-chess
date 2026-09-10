#pragma once

#include <chrono>
#include <ctime>
#include <iomanip>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>

#include <fmt/include/fmt/core.h>
#include "fmt/include/fmt/std.h"

namespace fastchess::time {

namespace sc = std::chrono;

// Get the current date and time in a given format.
[[nodiscard]] std::optional<std::string> datetime(const std::string& format);

[[nodiscard]] std::string datetime_iso();

// Formats a duration in seconds to a string in the format HH:MM:SS.
[[nodiscard]] std::string duration(sc::seconds duration);

[[nodiscard]] std::string datetime_precise();

}  // namespace fastchess::time
