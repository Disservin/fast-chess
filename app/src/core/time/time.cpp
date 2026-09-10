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

namespace {
std::mutex mutex_time;
}

// Get the current date and time in a given format.
std::optional<std::string> datetime(const std::string& format) {
    std::lock_guard<std::mutex> lock(mutex_time);

    // Get the current time in UTC
    const auto now        = sc::system_clock::now();
    const auto time_t_now = sc::system_clock::to_time_t(now);
    struct tm buf{};

#ifdef _WIN64
    auto res = localtime_s(&buf, &time_t_now);
    if (res != 0) {
        return std::nullopt;
    }

    // Format the time as an ISO 8601 string
    std::stringstream ss;
    ss << std::put_time(&buf, format.c_str());
    return ss.str();
#else
    const auto res = localtime_r(&time_t_now, &buf);

    // Format the time as an ISO 8601 string
    std::stringstream ss;
    ss << std::put_time(res, format.c_str());
    return ss.str();
#endif
}

std::string datetime_iso() {
    std::lock_guard<std::mutex> lock(mutex_time);

    auto now   = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);

    std::tm local_tm = *std::localtime(&now_c);

    std::string datetime = fmt::format("{:%Y-%m-%dT%H:%M:%S}", local_tm);

    // gmt == utc
    std::time_t gmt, local;
    std::tm tm_local = local_tm;
    std::tm tm_gmt   = *std::gmtime(&now_c);

    tm_local.tm_isdst = 0;
    tm_gmt.tm_isdst   = 0;

    local = std::mktime(&tm_local);
    gmt   = std::mktime(&tm_gmt);

    int diff      = (local - gmt) / 60;
    int hour_diff = diff / 60;
    int min_diff  = std::abs(diff % 60);

    std::string timezone = fmt::format("{}{:02d}{:02d}", (hour_diff >= 0 ? "+" : "-"), std::abs(hour_diff), min_diff);

    return fmt::format("{} {}", datetime, timezone);
}

// Formats a duration in seconds to a string in the format HH:MM:SS.
std::string duration(sc::seconds duration) {
    const auto hours = sc::duration_cast<sc::hours>(duration);
    duration -= hours;
    const auto minutes = sc::duration_cast<sc::minutes>(duration);
    duration -= minutes;
    const auto seconds = duration;

    return fmt::format("{:02}:{:02}:{:02}", hours.count(), minutes.count(), seconds.count());
}

std::string datetime_precise() {
    const auto now        = sc::system_clock::now();
    const auto ms         = sc::duration_cast<sc::microseconds>(now.time_since_epoch());
    const auto elapsed_ms = ms % sc::seconds(1);
    const auto str        = datetime("%H:%M:%S");

    return fmt::format("{}.{:06}", str.value_or(""), elapsed_ms.count());
}

}  // namespace fastchess::time
