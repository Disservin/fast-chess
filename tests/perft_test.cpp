#include "doctest/doctest.hpp"

#include "../src/chess/board.hpp"
#include "../src/chess/perft.hpp"

using namespace fast_chess;

TEST_CASE("PERFT Startposition")
{
    Perft perf;
    Board b;
    b.loadFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");

    perf.perftFunction(b, 1, 1);
    CHECK(perf.getAndResetNodes() == 20);

    perf.perftFunction(b, 2, 2);
    CHECK(perf.getAndResetNodes() == 400);

    perf.perftFunction(b, 3, 3);
    CHECK(perf.getAndResetNodes() == 8902);

    perf.perftFunction(b, 4, 4);
    CHECK(perf.getAndResetNodes() == 197281);

    perf.perftFunction(b, 5, 5);
    CHECK(perf.getAndResetNodes() == 4865609);

    perf.perftFunction(b, 6, 6);
    CHECK(perf.getAndResetNodes() == 119060324);
}

TEST_CASE("PERFT 1.1")
{
    Perft perf;
    Board b;
    b.loadFen("2kr3r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/1K1R3R b - - 3 2");
    perf.perftFunction(b, 2, 2);
    CHECK(perf.getAndResetNodes() == 1930);
}

TEST_CASE("PERFT 1.2")
{
    Perft perf;
    Board b;
    b.loadFen("2kr3r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/2KR3R w - - 2 2");
    perf.perftFunction(b, 3, 3);
    CHECK(perf.getAndResetNodes() == 80360);
}

TEST_CASE("PERFT 1.2")
{
    Perft perf;
    Board b;
    b.loadFen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q2/PPPBBPpP/2KR3R w kq - 0 2");
    perf.perftFunction(b, 1, 1);
    CHECK(perf.getAndResetNodes() == 44);
}

TEST_CASE("PERFT 1.3")
{
    Perft perf;
    Board b;
    b.loadFen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q2/PPPBBPpP/2KR3R w kq - 0 2");
    perf.perftFunction(b, 3, 3);
    CHECK(perf.getAndResetNodes() == 100518);
}

TEST_CASE("PERFT 1.4")
{
    Perft perf;
    Board b;
    b.loadFen("r3k2r/p1ppqpb1/Bn2pnp1/3PN3/1p2P3/2N2Q2/PPPB1PpP/2KR3R b kq - 0 2");
    perf.perftFunction(b, 2, 2);
    CHECK(perf.getAndResetNodes() == 2146);
}

TEST_CASE("PERFT 1.5")
{
    Perft perf;
    Board b;
    b.loadFen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/2KR3R b kq - 1 1");
    perf.perftFunction(b, 4, 4);
    CHECK(perf.getAndResetNodes() == 3551583);
}

TEST_CASE("PERFT 2")
{
    static constexpr int depth = 5;

    Perft perf;
    Board b;
    b.loadFen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");

    perf.perftFunction(b, 1, 1);
    CHECK(perf.getAndResetNodes() == 48);

    perf.perftFunction(b, 2, 2);
    CHECK(perf.getAndResetNodes() == 2039);

    perf.perftFunction(b, 3, 3);
    CHECK(perf.getAndResetNodes() == 97862);

    perf.perftFunction(b, 4, 4);
    CHECK(perf.getAndResetNodes() == 4085603);

    perf.perftFunction(b, depth, depth);
    CHECK(perf.getAndResetNodes() == 193690690ull);
}

TEST_CASE("PERFT 3")
{
    static constexpr int depth = 7;

    Perft perf;
    Board b;
    b.loadFen("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1");

    perf.perftFunction(b, depth, depth);
    CHECK(perf.getAndResetNodes() == 178633661ull);
}

TEST_CASE("PERFT 4")
{
    static constexpr int depth = 6;

    Perft perf;
    Board b;
    b.loadFen("r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1");

    perf.perftFunction(b, depth, depth);
    CHECK(perf.getAndResetNodes() == 706045033ull);
}

TEST_CASE("PERFT 5")
{
    static constexpr int depth = 5;

    Perft perf;
    Board b;
    b.loadFen("rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8");

    perf.perftFunction(b, depth, depth);
    CHECK(perf.getAndResetNodes() == 89941194ull);
}

TEST_CASE("PERFT 6")
{
    static constexpr int depth = 5;

    Perft perf;
    Board b;
    b.loadFen("r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10");

    perf.perftFunction(b, depth, depth);
    CHECK(perf.getAndResetNodes() == 164075551ull);
}

TEST_CASE("PERFT 7")
{
    static constexpr int depth = 7;

    Perft perf;
    Board b;
    b.loadFen("4r3/bpk5/5n2/2P1P3/8/4K3/8/8 w - - 0 1");

    perf.perftFunction(b, depth, depth);
    CHECK(perf.getAndResetNodes() == 71441619ull);
}

// TEST_CASE("PERFT 8")
// {
//     static constexpr int depth = 6;

//     Perft perf;
//     Board b;
//     b.loadFen("b2r4/2q3k1/p5p1/P1r1pp2/R1pnP2p/4NP1P/1PP2RPK/Q4B2 w - - 2 29");

//     perf.perftFunction(b, depth, depth);
//     CHECK(perf.getAndResetNodes() == 2261050076ull);
// }

TEST_CASE("PERFT 9")
{
    static constexpr int depth = 8;

    Perft perf;
    Board b;
    b.loadFen("3n1k2/8/4P3/8/8/8/8/2K1R3 w - - 0 1");

    perf.perftFunction(b, depth, depth);
    CHECK(perf.getAndResetNodes() == 437319625ull);
}