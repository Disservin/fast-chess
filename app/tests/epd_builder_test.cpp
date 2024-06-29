#include <epd/epd_builder.hpp>

#include "doctest/doctest.hpp"

namespace fast_chess {
TEST_SUITE("EPD Builder Tests") {
    TEST_CASE("EPD Creation White Start") {
        MatchData match_data;

        match_data.moves = {MoveData("e2e4", "+1.00", 1321, 15, 4, 0, 0),
                            MoveData("e7e5", "+1.23", 430, 15, 3, 0, 0),
                            MoveData("g1f3", "+1.45", 310, 16, 24, 0, 0),
                            MoveData("g8f6", "+10.15", 1821, 18, 7, 0, 0)};

        match_data.fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

        options::Tournament options;

        std::string expected = "rnbqkb1r/pppp1ppp/5n2/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - hmvc 2; fmvn 3;\n";

        epd::EpdBuilder epd_builder = epd::EpdBuilder(match_data, options);
        CHECK(epd_builder.get() == expected);
    }

    TEST_CASE("EPD Creation Black Start") {
        MatchData match_data;

        match_data.moves = {MoveData("e8g8", "+1.00", 1321, 15, 4, 0, 0),
                            MoveData("e1g1", "+1.23", 430, 15, 3, 0, 0),
                            MoveData("a6c5", "+1.45", 310, 16, 24, 0, 0)};

        match_data.fen = "r2qk2r/1bpp2pp/n3pn2/p2P1p2/1bP5/2N1BNP1/1PQ1PPBP/R3K2R b KQkq - 0 1";

        options::Tournament options;

        std::string expected = "r2q1rk1/1bpp2pp/4pn2/p1nP1p2/1bP5/2N1BNP1/1PQ1PPBP/R4RK1 w - - hmvc 3; fmvn 3;\n";

        epd::EpdBuilder epd_builder = epd::EpdBuilder(match_data, options);
        CHECK(epd_builder.get() == expected);
    }
}
}  // namespace fast_chess
