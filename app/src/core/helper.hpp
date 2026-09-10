#pragma once

#include <algorithm>
#include <cctype>
#include <exception>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <fmt/include/fmt/core.h>
#include <expected.hpp>
#include <json.hpp>

namespace fastchess::str_utils {

template <typename T>
[[nodiscard]] std::optional<T> parseInteger(std::string_view value) noexcept {
    static_assert(std::is_integral_v<T> && !std::is_same_v<T, bool>, "parseInteger requires an integer type");

    const std::string str(value);
    if (str.empty() || std::any_of(str.begin(), str.end(), [](unsigned char c) { return std::isspace(c); })) {
        return std::nullopt;
    }

    std::size_t parsed_length = 0;

    try {
        if constexpr (std::is_unsigned_v<T>) {
            if (str.front() == '-') return std::nullopt;

            const auto parsed = std::stoull(str, &parsed_length);
            if (parsed_length != str.size() || parsed > std::numeric_limits<T>::max()) return std::nullopt;
            return static_cast<T>(parsed);
        } else {
            const auto parsed = std::stoll(str, &parsed_length);
            if (parsed_length != str.size() || parsed < std::numeric_limits<T>::min() ||
                parsed > std::numeric_limits<T>::max()) {
                return std::nullopt;
            }
            return static_cast<T>(parsed);
        }
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

// @todo unnecessary with c++20
[[nodiscard]] inline bool startsWith(std::string_view haystack, std::string_view needle) noexcept {
    if (needle.empty()) return false;
    return (haystack.rfind(needle, 0) != std::string::npos);
}

// @todo unnecessary with c++20
[[nodiscard]] inline bool endsWith(std::string_view value, std::string_view ending) {
    if (ending.size() > value.size()) return false;
    return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
}

// Check if a string contains a substring. @todo unnecessary with c++20
[[nodiscard]] inline bool contains(std::string_view haystack, std::string_view needle) noexcept {
    return haystack.find(needle) != std::string::npos;
}

// Check if a vector of strings contains a string.
[[nodiscard]] inline bool contains(const std::vector<std::string>& haystack, std::string_view needle) noexcept {
    return std::find(haystack.begin(), haystack.end(), needle) != haystack.end();
}

// Split a string into a vector of strings based on a delimiter.
[[nodiscard]] inline std::vector<std::string> splitString(std::string_view string, const char& delimiter) {
    std::stringstream string_stream{std::string(string)};
    std::string segment;
    std::vector<std::string> seglist;

    while (std::getline(string_stream, segment, delimiter))
        if (!segment.empty()) seglist.emplace_back(segment);

    return seglist;
}

// Find an element in a vector of strings and return the next element as a specified type.
template <typename T>
[[nodiscard]] tl::expected<T, std::string> findElement(const std::vector<std::string>& haystack,
                                                       std::string_view needle) {
    auto it = std::find(haystack.begin(), haystack.end(), needle);

    if (it == haystack.end() || std::next(it) == haystack.end()) {
        return tl::make_unexpected(fmt::format("Element '{}' not found", needle));
    }

    const std::string& target = *std::next(it);

    try {
        if constexpr (std::is_same_v<T, int>) {
            return std::stoi(target);
        } else if constexpr (std::is_same_v<T, float>) {
            return std::stof(target);
        } else if constexpr (std::is_same_v<T, uint64_t>) {
            return std::stoull(target);
        } else if constexpr (std::is_same_v<T, int64_t>) {
            return std::stoll(target);
        } else if constexpr (std::is_same_v<T, std::string>) {
            return target;
        } else {
            return tl::make_unexpected(fmt::format("Unsupported target type for element '{}'", target));
        }
    } catch (const std::exception& e) {
        return tl::make_unexpected(fmt::format("Error converting element '{}' to target type: {}", target, e.what()));
    }
}

template <class StringLike>
[[nodiscard]] std::string join(const std::vector<StringLike>& strings, std::string_view delimiter) {
    std::string result;

    for (const auto& string : strings) {
        if (!result.empty()) {
            result.append(delimiter.data(), delimiter.size());
        }

        std::string_view view = string;
        result.append(view.data(), view.size());
    }

    return result;
}
}  // namespace fastchess::str_utils
