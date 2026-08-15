#include <matchmaking/scoreboard.hpp>

#include <array>

#include <doctest/doctest.hpp>

namespace fastchess {

TEST_SUITE("ScoreBoard") {
    TEST_CASE("Update and Get") {
        EngineConfiguration engine1 = {};
        EngineConfiguration engine2 = {};

        engine1.name = "engine1";
        engine2.name = "engine2";

        const auto stats = Stats(1, 2, 3);

        ScoreBoard scoreboard;
        scoreboard.updateNonPair({engine1, engine2}, stats);

        CHECK(scoreboard.getStats(engine1.name, engine2.name) == stats);
    }

    TEST_CASE("Update and Update and Get") {
        EngineConfiguration engine1 = {};
        EngineConfiguration engine2 = {};

        engine1.name = "engine1";
        engine2.name = "engine2";

        auto stats = Stats(1, 2, 3);

        ScoreBoard scoreboard;
        scoreboard.updateNonPair({engine1, engine2}, stats);
        scoreboard.updateNonPair({engine1, engine2}, stats);

        CHECK(scoreboard.getStats(engine1.name, engine2.name) == Stats{2, 4, 6});
    }

    TEST_CASE("Update and Update Reverse and Get") {
        EngineConfiguration engine1 = {};
        EngineConfiguration engine2 = {};

        engine1.name = "engine1";
        engine2.name = "engine2";

        ScoreBoard scoreboard;

        const auto stats = Stats{1, 2, 3};

        scoreboard.updateNonPair({engine1, engine2}, stats);
        scoreboard.updateNonPair({engine2, engine1}, stats);

        CHECK(scoreboard.getStats(engine1.name, engine2.name) == Stats(3, 3, 6));
        CHECK(scoreboard.getStats(engine2.name, engine1.name) == Stats(3, 3, 6));
    }

    TEST_CASE("Get stats for a specific opponent") {
        EngineConfiguration engine1 = {};
        EngineConfiguration engine2 = {};
        EngineConfiguration engine3 = {};

        engine1.name = "engine1";
        engine2.name = "engine2";
        engine3.name = "engine3";

        ScoreBoard scoreboard;
        scoreboard.updateNonPair({engine1, engine2}, Stats(1, 0, 0));
        scoreboard.updateNonPair({engine1, engine3}, Stats(0, 1, 0));

        CHECK(scoreboard.getStatsVsOpponent(engine1.name, engine2.name) == Stats(1, 0, 0));
        CHECK(scoreboard.getStatsVsOpponent(engine1.name, engine3.name) == Stats(0, 1, 0));
    }

    TEST_CASE("Update with black-to-move opening records assignment perspective") {
        EngineConfiguration engine1 = {};
        EngineConfiguration engine2 = {};

        engine1.name = "engine1";
        engine2.name = "engine2";

        MatchData match_data;
        match_data.players.white = {engine2, chess::GameResult::LOSE};
        match_data.players.black = {engine1, chess::GameResult::WIN};

        ScoreBoard scoreboard;
        scoreboard.updateNonPair({engine2, engine1}, Stats(match_data));

        CHECK(scoreboard.getStats(engine1.name, engine2.name) == Stats(1, 0, 0));
    }

    TEST_CASE("Update pair records every pentanomial outcome") {
        EngineConfiguration engine1 = {};
        EngineConfiguration engine2 = {};

        engine1.name = "engine1";
        engine2.name = "engine2";

        struct PairResult {
            const char* name;
            Stats first_game;
            Stats second_game;
            Stats expected;
        };

        const std::array<PairResult, 6> results = {{
            {"WW", Stats(1, 0, 0), Stats(0, 1, 0), Stats(0, 0, 0, 0, 0, 1)},
            {"WD", Stats(1, 0, 0), Stats(0, 0, 1), Stats(0, 0, 0, 0, 1, 0)},
            {"WL", Stats(1, 0, 0), Stats(1, 0, 0), Stats(0, 0, 1, 0, 0, 0)},
            {"DD", Stats(0, 0, 1), Stats(0, 0, 1), Stats(0, 0, 0, 1, 0, 0)},
            {"LD", Stats(0, 1, 0), Stats(0, 0, 1), Stats(0, 1, 0, 0, 0, 0)},
            {"LL", Stats(0, 1, 0), Stats(1, 0, 0), Stats(1, 0, 0, 0, 0, 0)},
        }};

        for (const auto& result : results) {
            CAPTURE(result.name);
            ScoreBoard scoreboard;

            CHECK_FALSE(scoreboard.updatePair({engine1, engine2}, result.first_game, 42));
            CHECK_FALSE(scoreboard.isPairCompleted(42));
            CHECK(scoreboard.getStats(engine1.name, engine2.name) == Stats());

            CHECK(scoreboard.updatePair({engine2, engine1}, result.second_game, 42));
            CHECK(scoreboard.isPairCompleted(42));

            auto expected = result.expected;
            expected += result.first_game + ~result.second_game;
            CHECK(scoreboard.getStats(engine1.name, engine2.name) == expected);
            CHECK(scoreboard.getStats(engine2.name, engine1.name) == ~expected);
        }
    }

    // TEST_CASE("SetResults") {
    //     EngineConfiguration engine1 = {};
    //     EngineConfiguration engine2 = {};

    //     engine1.name = "engine1";
    //     engine2.name = "engine2";

    //     stats_map results = {{engine1.name, {{engine2.name, Stats(1, 2, 3)}}},
    //                          {engine2.name, {{engine1.name, Stats(0, 0, 0)}}}};

    //     ScoreBoard result;
    //     result.setResults(results);

    //     CHECK(result.getStats(engine1.name, engine2.name) == Stats(1, 2, 3));
    //     CHECK(result.getResults() == results);
    // }
}
}  // namespace fastchess
