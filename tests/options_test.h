#include "../src/options.h"
#include "doctest/doctest.h"

#include <cassert>

TEST_CASE("Testing Engine options parsing")
{
    const char *argv[] = {"fast-chess.exe",
                          "-engine",
                          "dir=Engines/",
                          "cmd=./Alexandria-EA649FED.exe",
                          "tc=10/9.64",
                          "option.Threads=1",
                          "option.Hash=16",
                          "name=Alexandria-EA649FED",
                          "-engine",
                          "dir=Engines/",
                          "cmd=./Alexandria-27E42728.exe",
                          "tc=9.64+0.10",
                          "option.Threads=1",
                          "option.Hash=32",
                          "name=Alexandria-27E42728",
                          "-openings",
                          "file=Books/Pohl.epd",
                          "format=epd",
                          "order=random",
                          "plies=16",
                          "-pgnout",
                          "PGNs/Alexandria-EA649FED_vs_Alexandria-27E42728"};
    CMD::Options options = CMD::Options(22, argv);

    auto configs = options.getEngineConfig();

    CHECK(configs.size() == 2);
    EngineConfiguration config0 = configs[0];
    EngineConfiguration config1 = configs[1];
    CHECK(config0.name == "Alexandria-EA649FED");
    CHECK(config0.tc.moves == 10);
    CHECK(config0.tc.time == 9640);
    CHECK(config0.tc.increment == 0);
    CHECK(config0.nodes == 0);
    CHECK(config0.plies == 0);
    CHECK(config0.options.at(0).first == "option.Threads");
    CHECK(config0.options.at(0).second == "1");
    CHECK(config0.options.at(1).first == "option.Hash");
    CHECK(config0.options.at(1).second == "16");
    CHECK(config1.name == "Alexandria-27E42728");
    CHECK(config1.tc.moves == 0);
    CHECK(config1.tc.time == 9640);
    CHECK(config1.tc.increment == 100);
    CHECK(config1.nodes == 0);
    CHECK(config1.plies == 0);
    CHECK(config1.options.at(0).first == "option.Threads");
    CHECK(config1.options.at(0).second == "1");
    CHECK(config1.options.at(1).first == "option.Hash");
    CHECK(config1.options.at(1).second == "32");
}

TEST_CASE("Testing Cli options parsing")
{
    const char *argv[] = {"fast-chess.exe",
                          "-repeat",
                          "-recover",
                          "-concurrency",
                          "8",
                          "-games",
                          "256",
                          "-openings",
                          "file=Books/Pohl.epd",
                          "format=epd",
                          "order=random",
                          "plies=16",
                          "-pgnout",
                          "PGNs/Alexandria-EA649FED_vs_Alexandria-27E42728"};
    CMD::Options options = CMD::Options(14, argv);
    auto gameOptions = options.getGameOptions();

    // Test proper cli settings
    CHECK(gameOptions.repeat == true);
    CHECK(gameOptions.recover == true);
    CHECK(gameOptions.concurrency == 8);
    CHECK(gameOptions.games == 256);
    //  Test opening settings parsing
    // CHECK(options.gameOptions.OpeningOptions.file == "Books/Pohl.epd");
    // CHECK(options.gameOptions.OpeningOptions.format == "epd");
    // CHECK(options.gameOptions.OpeningOptions.order == "random");
    // CHECK(options.gameOptions.OpeningOptions.plies == 16);
    //  Test pgn settings parsing
    // CHECK(options.gameOptions.PgnOptions.file == "PGNs/Alexandria-EA649FED_vs_Alexandria-27E42728");
}