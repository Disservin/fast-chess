#pragma once

#include <string>

#include <types/exception.hpp>

#include "ucioption.hpp"

namespace fastchess {

// idk
class ButtonOption : public UCIOption {
   public:
    ButtonOption(const std::string& name) : name(name) {}

    std::string getName() const override { return name; }

    void setValue(const std::string& value) override {
        if (isValid(value)) {
            this->value = true;
        } else {
            throw fastchess_exception::format("Option \"{}\": button value must be \"true\".", name);
        }
    }

    std::string getValue() const override { return value ? "true" : "false"; }

    bool isValid(const std::string& value) const override { return value == "true"; }

    Type getType() const override { return Type::Button; }

   private:
    std::string name;
    bool value = false;
};

}  // namespace fastchess
