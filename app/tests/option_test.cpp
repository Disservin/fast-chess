#include <engine/option/option_factory.hpp>

#include <doctest/doctest.hpp>
#include "types/exception.hpp"

namespace fastchess {

TEST_SUITE("Uci Options") {
    TEST_CASE("Parse Spin Option Integer") {
        std::string line = "name Hash type spin default 16 min 1 max 16384";
        auto option      = UCIOptionFactory::parseUCIOptionLine(line);

        CHECK(option->getName() == "Hash");

        CHECK(option->getType() == UCIOption::Type::Spin);

        CHECK(option->getValue() == "16");

        CHECK(option->isValid("1"));
        CHECK(option->isValid("16384"));

        CHECK(!option->isValid("0"));
        CHECK(!option->isValid("16385"));

        option->setValue("1");

        CHECK(option->getValue() == "1");
    }

    TEST_CASE("Parse Spin Option Double") {
        std::string line = "name x1 type spin default 321.12321 min 0 max 321321.3213";
        auto option      = UCIOptionFactory::parseUCIOptionLine(line);

        CHECK(option->getName() == "x1");

        CHECK(option->getType() == UCIOption::Type::Spin);

        CHECK(option->getValue() == "321.123210");

        CHECK(option->isValid("1"));
        CHECK(option->isValid("16384"));

        CHECK(!option->isValid("-1"));
        CHECK(!option->isValid("321322"));

        option->setValue("1");

        CHECK(option->getValue() == "1.000000");
    }

    TEST_CASE("Parse Spin Option Default String") {
        std::string line = "name x1 type spin default dsad min 0 max 321321.3213";

         CHECK_THROWS_WITH_AS(UCIOptionFactory::parseUCIOptionLine(line),
                              "Option \"x1\": spin values must be numeric.",
                              fastchess_exception);
    }

    TEST_CASE("Parse Spin Option Min String") {
        std::string line = "name x1 type spin default 3213.21 min foobar max 321321.3213";

         CHECK_THROWS_WITH_AS(UCIOptionFactory::parseUCIOptionLine(line),
                              "Option \"x1\": spin values must be numeric.",
                              fastchess_exception);
    }

    TEST_CASE("Parse Spin Option Max String") {
        std::string line = "name x1 type spin default 3213.21 min 321.321 max foobar";

         CHECK_THROWS_WITH_AS(UCIOptionFactory::parseUCIOptionLine(line),
                              "Option \"x1\": spin values must be numeric.",
                              fastchess_exception);
    }

    TEST_CASE("Parse Spin Option Min Larger Than Max") {
        std::string line = "name x1 type spin default 3213.21 min 10 max 0";

         CHECK_THROWS_WITH_AS(UCIOptionFactory::parseUCIOptionLine(line),
                              "Option \"x1\": min value cannot be greater than max value.",
                              fastchess_exception);
    }

    TEST_CASE("Parse Spin Option Default Not In Range") {
        std::string line = "name x1 type spin default 3213 min 0 max 10";

         CHECK_THROWS_WITH_AS(UCIOptionFactory::parseUCIOptionLine(line),
                              "Option \"x1\": value \"3213\" is out of the allowed range.",
                              fastchess_exception);
    }

    TEST_CASE("Parse Combo Option") {
        std::string line = "name Style type combo default Normal var Solid var Normal var Risky";
        auto option      = UCIOptionFactory::parseUCIOptionLine(line);

        CHECK(option->getName() == "Style");

        CHECK(option->getType() == UCIOption::Type::Combo);

        CHECK(option->getValue() == "Normal");

        CHECK(option->isValid("Solid"));
        CHECK(option->isValid("Normal"));
        CHECK(option->isValid("Risky"));

        CHECK(!option->isValid("0"));
        CHECK(!option->isValid("Random"));

        option->setValue("Solid");

        CHECK(option->getValue() == "Solid");
    }

    TEST_CASE("Parse Button Option") {
        std::string line = "name Clear Hash type button";
        auto option      = UCIOptionFactory::parseUCIOptionLine(line);

        CHECK(option->getName() == "Clear Hash");

        CHECK(option->getType() == UCIOption::Type::Button);

        CHECK(option->getValue() == "false");

        CHECK(option->isValid("true"));

        option->setValue("true");

        CHECK(option->getValue() == "true");
    }

    TEST_CASE("Parse Check Option") {
        std::string line = "name OwnBook type check default false";
        auto option      = UCIOptionFactory::parseUCIOptionLine(line);

        CHECK(option->getName() == "OwnBook");

        CHECK(option->getType() == UCIOption::Type::Check);

        CHECK(option->getValue() == "false");

        CHECK(option->isValid("true"));

        option->setValue("true");

        CHECK(option->getValue() == "true");
    }

    TEST_CASE("Parse String Option") {
        std::string line = "name NalimovPath type string default <empty>";
        auto option      = UCIOptionFactory::parseUCIOptionLine(line);

        CHECK(option->getName() == "NalimovPath");

        CHECK(option->getType() == UCIOption::Type::String);

        CHECK(option->getValue() == "<empty>");

        CHECK(option->isValid(""));

        option->setValue("nalimov");

        CHECK(option->getValue() == "nalimov");
    }

    TEST_CASE("Parse String Option Default Foo") {
        std::string line = "name NalimovPath type string default Foo";
        auto option      = UCIOptionFactory::parseUCIOptionLine(line);

        CHECK(option->getName() == "NalimovPath");

        CHECK(option->getType() == UCIOption::Type::String);

        CHECK(option->getValue() == "Foo");

        CHECK(option->isValid(""));

        option->setValue("nalimov");

        CHECK(option->getValue() == "nalimov");
    }

    TEST_CASE("Parse String Option Default With Spaces") {
        std::string line = "name Book Path type string default /tmp/My Books/book.bin";
        auto option      = UCIOptionFactory::parseUCIOptionLine(line);

        CHECK(option->getName() == "Book Path");
        CHECK(option->getType() == UCIOption::Type::String);
        CHECK(option->getValue() == "/tmp/My Books/book.bin");
    }
}

}  // namespace fastchess
