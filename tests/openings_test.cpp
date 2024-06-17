#include <types/tournament_options.hpp>
#include <book/opening_book.hpp>

#include "doctest/doctest.hpp"

using namespace fast_chess;

TEST_SUITE("Openings") {
    TEST_CASE("check epd openings") {
        options::Tournament tournament;
        tournament.rounds = 10;
        tournament.opening.file = "tests/data/openings.epd";
        tournament.opening.format = FormatType::EPD;
        tournament.opening.order = OrderType::RANDOM;
        tournament.opening.start = 3256;
        tournament.seed = 123456789;
        int initial_matchcount = 47;

        auto book = book::OpeningBook(tournament, initial_matchcount);
        std::vector<std::string> epd = book.getEpdBook();

        CHECK(epd.size() == 10);
        CHECK(epd.capacity() == 10);
        CHECK(epd[0] == "1n1qkb1r/rp3ppp/p1p1pn2/2PpN2b/3PP3/1QN5/PP3PPP/R1B1KB1R w KQk - 0 9");
        CHECK(epd[9] == "rn1qkb1r/4pp1p/3p1np1/2pP4/4P3/2N5/PP3PPP/R1BQ1KNR w kq - 0 9");
    }

    TEST_CASE("check pgn openings") {
        options::Tournament tournament;
        tournament.rounds = 10;
        tournament.opening.file = "tests/data/test2.pgn";
        tournament.opening.format = FormatType::PGN;
        tournament.opening.order = OrderType::RANDOM;
        tournament.opening.start = 3256;
        tournament.seed = 123456789;
        int initial_matchcount = 0;

        auto book = book::OpeningBook(tournament, initial_matchcount);
        std::vector<pgn::Opening> pgn = book.getPgnBook();

        CHECK(pgn.size() == 10);
        CHECK(pgn.capacity() == 10);
        CHECK(pgn[0].fen == "rnbq1rk1/pp2ppbp/6p1/2p2n2/3p1P2/2PP1NP1/PP2P1BP/RNBQ1R1K b - -");
        CHECK(pgn[9].fen == "r1bq1rk1/ppp1n1pp/2n5/2p1pP2/8/2NP4/PPPQ1PPP/2KR1BNR b - -");
    }
}

