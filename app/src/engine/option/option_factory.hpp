#pragma once

#include <iostream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <types/exception.hpp>

#include "ucioption.hpp"

#include "button_option.hpp"
#include "check_option.hpp"
#include "combo_option.hpp"
#include "spin_option.hpp"
#include "string_option.hpp"

namespace fastchess {

class UCIOptionFactory {
   public:
    static std::unique_ptr<UCIOption> parseUCIOptionLine(const std::string& line) {
        std::istringstream ss(line);
        std::string token, name, type;
        std::unordered_map<std::string, std::string> params;

        while (ss >> token) {
            if (token == "name") {
                ss >> name;
                while (ss >> token && token != "type") {
                    fmt::format_to(std::back_inserter(name), " {}", token);
                }
            }
            if (token == "type") {
                ss >> type;
            } else if (token == "default") {
                if (type == "string") {
                    std::getline(ss >> std::ws, params["default"]);
                } else {
                    ss >> params["default"];
                }
            } else if (token == "min") {
                ss >> params["min"];
            } else if (token == "max") {
                ss >> params["max"];
            } else if (token == "var") {
                std::string var;

                while (ss >> var) {
                    if (var == "var") {
                        continue;
                    }

                    fmt::format_to(std::back_inserter(params["var"]), "{} ", var);
                }
            }
        }

        if (type == "check") {
            auto option = std::make_unique<CheckOption>(name);
            option->setValue(params["default"]);
            return option;
        } else if (type == "spin") {
            if (isInteger(params["default"]) && isInteger(params["min"]) && isInteger(params["max"])) {
                return createSpinOption<int>(name, params["min"], params["max"], params["default"]);
            } else if (isFloat(params["default"]) && isFloat(params["min"]) && isFloat(params["max"])) {
                return createSpinOption<double>(name, params["min"], params["max"], params["default"]);
            }

            throw fastchess_exception::format("Option \"{}\": spin values must be numeric.", name);
        } else if (type == "combo") {
            std::istringstream varStream(params["var"]);
            std::vector<std::string> options;
            std::string option;
            while (varStream >> option) {
                options.push_back(option);
            }
            return std::make_unique<ComboOption>(name, options, params["default"]);
        } else if (type == "button") {
            return std::make_unique<ButtonOption>(name);
        } else if (type == "string") {
            return std::make_unique<StringOption>(name, params["default"]);
        }

        return nullptr;
    }

   private:
    template <typename T>
    static std::unique_ptr<SpinOption<T>> createSpinOption(const std::string& name, const std::string& min,
                                                           const std::string& max, const std::string& defaultValue) {
        auto option = std::make_unique<SpinOption<T>>(name, min, max);
        option->setValue(defaultValue);
        return option;
    }

    static bool isInteger(const std::string& s) {
        std::stringstream ss(s);
        int num;
        ss >> num;
        return ss.eof() && !ss.fail();
    }

    static bool isFloat(const std::string& s) {
        std::stringstream ss(s);
        double num;
        ss >> num;
        return ss.eof() && !ss.fail();
    }
};

}  // namespace fastchess
