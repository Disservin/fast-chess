#include <game/timecontrol/timecontrol.hpp>

#include <doctest/doctest.hpp>

using namespace fastchess;

TEST_SUITE("TimeControl") {
    TEST_CASE("moves/time+increment") {
        TimeControl::Limits limits;
        limits.moves      = 3;
        limits.time       = 10000;
        limits.increment  = 100;
        limits.timemargin = 100;

        TimeControl tc(limits);

        CHECK(tc.updateTime(5555) == true);
        CHECK(tc.getTimeLeft() == limits.time + limits.increment - 5555 + limits.increment);
        CHECK(tc.getMovesLeft() == 2);

        CHECK(tc.updateTime(4745) == true);
        CHECK(tc.getTimeLeft() == limits.increment);
        CHECK(tc.getMovesLeft() == 1);

        CHECK(tc.updateTime(50) == true);
        CHECK(tc.getTimeLeft() == 100 + limits.time - 50 + limits.increment);
        CHECK(tc.getMovesLeft() == 3);

        CHECK(tc.updateTime(10251) == false);
        CHECK(tc.getTimeLeft() == 10150 - 10251);
        CHECK(tc.getMovesLeft() == 2);
    }

    TEST_CASE("Period time is not credited before timeout check") {
        TimeControl::Limits limits;
        limits.moves      = 1;
        limits.time       = 2000;
        limits.timemargin = 100;

        TimeControl tc(limits);

        CHECK(tc.updateTime(2101) == false);
        CHECK(tc.getTimeLeft() == -101);
    }

    TEST_CASE("Fixed time") {
        TimeControl::Limits limits;
        limits.fixed_time = 5000;
        limits.timemargin = 200;

        TimeControl tc(limits);

        CHECK(tc.updateTime(limits.fixed_time + limits.timemargin - 1) == true);
        CHECK(tc.getTimeLeft() == limits.fixed_time);

        CHECK(tc.updateTime(limits.fixed_time + limits.timemargin) == true);
        CHECK(tc.getTimeLeft() == limits.fixed_time);

        CHECK(tc.updateTime(limits.fixed_time + limits.timemargin + 1) == false);
        CHECK(tc.getTimeLeft() == limits.fixed_time - (limits.fixed_time + limits.timemargin + 1));
    }

    TEST_CASE("Fixed depth/nodes") {
        TimeControl::Limits limits;

        TimeControl tc(limits);

        CHECK(tc.updateTime(523199) == true);
        CHECK(tc.getTimeLeft() == 0);
    }
}
