#pragma once

#include <cmath>
#include <cstdint>
#include <string>

#include <fmt/include/fmt/core.h>

namespace fastchess {

struct ScoreType {
    enum Value { CP, MATE, ERR };

    Value value;

    constexpr ScoreType(Value v = ERR) : value(v) {}

    static constexpr ScoreType fromString(std::string_view type) {
        return type == "cp" ? ScoreType{CP} : type == "mate" ? ScoreType{MATE} : ScoreType{ERR};
    }

    explicit operator std::string_view() const {
        switch (value) {
            case CP:
                return "cp";
            case MATE:
                return "mate";
            case ERR:
                return "err";
        }

        return "err";
    }

    constexpr operator Value() const { return value; }
};

struct Score {
    ScoreType type;
    int64_t value;

    explicit inline operator std::string() const {
        if (type == ScoreType::CP) {
            float normalizedScore = static_cast<float>(std::abs(value)) / 100.0f;
            return fmt::format("{}{:.2f}", value > 0 ? "+" : value < 0 ? "-" : "", normalizedScore);
        }

        if (type == ScoreType::MATE) {
            uint64_t plies = value > 0 ? value * 2 - 1 : value * -2;
            return fmt::format("{}M{}", value > 0 ? "+" : "-", plies);
        }

        return "ERR";
    }

    [[nodiscard]] bool isMate() const { return type == ScoreType::MATE; }
    [[nodiscard]] bool isCp() const { return type == ScoreType::CP; }
    [[nodiscard]] bool isErr() const { return type == ScoreType::ERR; }
};

}  // namespace fastchess
