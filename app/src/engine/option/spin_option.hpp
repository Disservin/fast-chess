#pragma once

#include <stdexcept>
#include <type_traits>

#include "ucioption.hpp"

#include <types/exception.hpp>

#define FMT_HEADER_ONLY
#include <fmt/include/fmt/core.h>

namespace fastchess {

template <typename T>
class SpinOption : public UCIOption {
    static_assert(std::is_arithmetic<T>::value, "SpinOption only supports numeric types.");

   public:
    SpinOption(const std::string& name, const std::string& minValue, const std::string& maxValue) : name(name) {
        this->minValue = parseValue(minValue);
        this->maxValue = parseValue(maxValue);

        if (this->minValue > this->maxValue) {
            throw fastchess_exception::format("Option \"{}\": min value cannot be greater than max value.", name);
        }
    }

    std::string getName() const override { return name; }

    void setValue(const std::string& value) override {
        T parsedValue = parseValue(value);

        if (isValid(value)) {
            this->value = parsedValue;
        } else {
            throw fastchess_exception::format("Option \"{}\": value \"{}\" is out of the allowed range.", name,
                                               value);
        }
    }

    std::string getValue() const override {
        if constexpr (std::is_floating_point_v<T>) {
            return fmt::format("{:.6f}", value);
        } else {
            return fmt::format("{}", value);
        }
    }

    bool isValid(const std::string& value) const override {
        T parsedValue = parseValue(value);
        return parsedValue >= minValue && parsedValue <= maxValue;
    }

    Type getType() const override { return Type::Spin; }

   private:
    std::string name;
    T value;
    T minValue;
    T maxValue;

    // Helper function to parse the value as type T
    T parseValue(const std::string& valueStr) const {
        if constexpr (std::is_integral<T>::value) {
            return static_cast<T>(std::stoi(valueStr));
        } else if constexpr (std::is_floating_point<T>::value) {
            return static_cast<T>(std::stod(valueStr));
        } else {
            throw fastchess_exception("Unsupported type for SpinOption.");
        }
    }
};

}  // namespace fastchess
