#pragma once

#include <exception>
#include <string>
#include <utility>

#include <fmt/include/fmt/core.h>

namespace fastchess {

class fastchess_exception : public std::exception {
   public:
    explicit fastchess_exception(std::string message) : message_(std::move(message)) {}

    template <typename... T>
    static fastchess_exception format(fmt::format_string<T...> format, T&&... args) {
        return fastchess_exception(fmt::format(format, std::forward<T>(args)...));
    }

    const char* what() const noexcept override { return message_.c_str(); }

   private:
    std::string message_;
};

}  // namespace fastchess
