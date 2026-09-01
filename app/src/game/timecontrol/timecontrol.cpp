#include "timecontrol.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>

#include <core/helper.hpp>
#include <types/enums.hpp>

#include <json.hpp>

#define FMT_HEADER_ONLY
#include <fmt/include/fmt/core.h>

namespace fastchess {

TimeControl::TimeControl(const Limits& limits) : limits_(limits) {
    if (limits_.fixed_time != 0) {
        time_left_ = limits_.fixed_time;
    } else {
        time_left_ = limits_.time + limits_.increment;
    }

    moves_left_ = limits_.moves;
}

std::chrono::milliseconds TimeControl::getTimeoutThreshold() const noexcept {
    return std::chrono::milliseconds(time_left_ + limits_.timemargin + MARGIN);
}

bool TimeControl::updateTime(const int64_t elapsed_millis) noexcept {
    bool completed_period = false;

    if (limits_.moves > 0) {
        if (moves_left_ == 1) {
            moves_left_ = limits_.moves;
            completed_period = true;
        } else {
            moves_left_--;
        }
    }

    if (limits_.fixed_time == 0 && limits_.time + limits_.increment == 0) {
        return true;
    }

    time_left_ -= elapsed_millis;

    if (time_left_ < -limits_.timemargin) {
        return false;
    }

    if (time_left_ < 0) {
        time_left_ = 0;
    }

    time_left_ += limits_.increment;

    if (completed_period) time_left_ += limits_.time;

    if (limits_.fixed_time != 0) time_left_ = limits_.fixed_time;

    return true;
}

std::ostream& operator<<(std::ostream& os, const TimeControl& tc) {
    if (tc.limits_.fixed_time > 0) {
        os << fmt::format("{:.8g}/move", tc.limits_.fixed_time / 1000.0);
        return os;
    }

    if (tc.limits_.moves == 0 && tc.limits_.time == 0 && tc.limits_.increment == 0) {
        os << "-";
    }

    if (tc.limits_.moves > 0) os << fmt::format("{}/", tc.limits_.moves);

    if (tc.limits_.time + tc.limits_.increment > 0) os << fmt::format("{:.6g}", tc.limits_.time / 1000.0);

    if (tc.limits_.increment > 0) os << fmt::format("+{:.6g}", tc.limits_.increment / 1000.0);

    return os;
}

}  // namespace fastchess
