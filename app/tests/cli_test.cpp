#include <cli/cli.hpp>
#include <cli/cli_args.hpp>
#include <types/exception.hpp>

#include <algorithm>
#include <iterator>
#include <vector>
#include <regex>

#include <doctest/doctest.hpp>

using namespace fastchess;

namespace {

cli::Args buildArgs(std::vector<std::string> args) {
    std::vector<const char*> raw;
    raw.reserve(args.size());
    for (auto &value : args) {
        raw.push_back(value.c_str());
    }
    return cli::Args(static_cast<int>(raw.size()), raw.data());
}

cli::Args baseArgs(std::vector<std::string> extras = {}) {
    std::vector<std::string> args = {
        "fastchess.exe",
        "-engine",
        "cmd=app/tests/mock/engine/dummy_engine",
        "name=Alpha",
        "tc=10/1+0",
        "-engine",
        "cmd=app/tests/mock/engine/dummy_engine",
        "name=Beta",
        "tc=10/1+0",
    };

    args.insert(args.end(), std::make_move_iterator(extras.begin()), std::make_move_iterator(extras.end()));

    return buildArgs(std::move(args));
}

cli::Args engineArgsWithTc(std::string tc) {
    return buildArgs({
        "fastchess.exe",
        "-engine",
        "cmd=app/tests/mock/engine/dummy_engine",
        "name=Alpha",
        "tc=" + tc,
        "-engine",
        "cmd=app/tests/mock/engine/dummy_engine",
        "name=Beta",
        "tc=10/1+0",
    });
}

}  // namespace

TEST_SUITE("Option Parsing Tests") {
    TEST_CASE("Should throw tc and st not usable together") {
        const auto args = cli::Args{
            "fastchess.exe",
            "-engine",
            "dir=./",
            "cmd=app/tests/mock/engine/dummy_engine",
            "tc=10/9.64",
            "st=5",
            "name=Alexandria-EA649FED",
            "-engine",
            "dir=./",
            "cmd=app/tests/mock/engine/dummy_engine",
            "tc=40/1:9.65+0.1",
        };

        CHECK_THROWS_WITH_AS(cli::OptionsParser{args}, "Error; cannot use tc and st together!", fastchess_exception);
    }

    TEST_CASE("Should throw no timecontrol specified") {
        const auto args = cli::Args{
            "fastchess.exe",
            "-engine",
            "dir=./",
            "cmd=app/tests/mock/engine/dummy_engine",
            "name=Alexandria-EA649FED",
            "-engine",
            "dir=./",
            "cmd=app/tests/mock/engine/dummy_engine",
            "name=Alexandria-27E42728",
        };

        CHECK_THROWS_WITH_AS(cli::OptionsParser{args}, "Error; no TimeControl specified!", fastchess_exception);
    }

    TEST_CASE("Should throw error too much concurrency") {
        const auto args = cli::Args{
            "fastchess.exe",
            "-concurrency",
            "20000",
        };

        CHECK_THROWS_WITH_AS(cli::OptionsParser{args},
                             "Error: Concurrency exceeds number of CPUs. Use -force-concurrency to override.",
                             fastchess_exception);
    }

    TEST_CASE("Should throw too many games") {
        const auto args = cli::Args{
            "fastchess.exe", "-games", "3", "-rounds", "25000",
        };

        CHECK_THROWS_WITH_AS(cli::OptionsParser{args}, "Error: Exceeded -game limit! Must be less than 2",
                             fastchess_exception);
    }

    TEST_CASE("Should throw invalid sprt config") {
        const auto args = cli::Args{
            "fastchess.exe", "-sprt", "alpha=0.05", "beta=0.05", "elo0=5", "elo1=-1.5", "model=bayesian",
        };

        CHECK_THROWS_WITH_AS(cli::OptionsParser{args}, "Error; SPRT: elo0 must be less than elo1!", fastchess_exception);
    }

    TEST_CASE("Should throw invalid sprt config 2") {
        const auto args = cli::Args{
            "fastchess.exe", "-sprt", "alpha=0.55", "beta=0.55", "elo0=4", "elo1=5", "model=bayesian",
        };

        CHECK_THROWS_WITH_AS(cli::OptionsParser{args}, "Error; SPRT: sum of alpha and beta must be less than 1!",
                             fastchess_exception);
    }

    TEST_CASE("Should throw invalid sprt config 3") {
        const auto args = cli::Args{
            "fastchess.exe", "-sprt", "alpha=0.05", "beta=0.05", "elo0=4", "elo1=5", "model=dsadsa",
        };

        CHECK_THROWS_WITH_AS(cli::OptionsParser{args}, "Error; SPRT: invalid SPRT model!", fastchess_exception);
    }

    TEST_CASE("Should throw invalid sprt config 4") {
        const auto args = cli::Args{
            "fastchess.exe", "-sprt", "alpha=1.05", "beta=0.05", "elo0=4", "elo1=5", "model=logistic",
        };

        CHECK_THROWS_WITH_AS(cli::OptionsParser{args}, "Error; SPRT: alpha must be a decimal number between 0 and 1!",
                             fastchess_exception);
    }

    TEST_CASE("Should throw invalid sprt config 5") {
        const auto args = cli::Args{
            "fastchess.exe", "-sprt", "alpha=0.05", "beta=1.05", "elo0=4", "elo1=5", "model=logistic",
        };

        CHECK_THROWS_WITH_AS(cli::OptionsParser{args}, "Error; SPRT: beta must be a decimal number between 0 and 1!",
                             fastchess_exception);
    }

    TEST_CASE("Should throw no chess960 opening book") {
        const auto args = cli::Args{
            "fastchess.exe",
            "-variant",
            "fischerandom",
        };

        CHECK_THROWS_WITH_AS(cli::OptionsParser{args}, "Error: Please specify a Chess960 opening book",
                             fastchess_exception);
    }

    TEST_CASE("Should throw not enough engines") {
        const auto args = cli::Args{
            "fastchess.exe", "-engine", "dir=./", "cmd=app/tests/mock/engine/dummy_engine", "depth=5",
        };

        CHECK_THROWS_WITH_AS(cli::OptionsParser{args}, "Error: Need at least two engines to start!",
                             fastchess_exception);
    }

    TEST_CASE("Should throw invalid tc") {
        const auto args = cli::Args{
            "fastchess.exe",
            "-engine",
            "dir=./",
            "cmd=app/tests/mock/engine/dummy_engine",
            "tc=10/0+0",
            "name=Alexandria-EA649FED",
            "-engine",
            "dir=./",
            "cmd=app/tests/mock/engine/dummy_engine",
            "tc=10/0+0",
        };

        CHECK_THROWS_WITH_AS(cli::OptionsParser{args}, "Error; no TimeControl specified!", fastchess_exception);
    }

    TEST_CASE("Malformed time controls should throw") {
        for (const auto& tc : {"40/", "10+", "1:", "1:2:3", "10/1junk", "10/1ss", "10/-1", "0/1"}) {
            CAPTURE(tc);
            CHECK_THROWS_AS(cli::OptionsParser{engineArgsWithTc(tc)}, fastchess_exception);
        }
    }

    TEST_CASE("Time controls should accept a seconds suffix") {
        const auto short_tc = cli::OptionsParser{engineArgsWithTc("0.02s")}.getEngineConfigs().front().limit.tc;
        CHECK(short_tc.time == 20);

        const auto increment_tc =
            cli::OptionsParser{engineArgsWithTc("0.2+0.002s")}.getEngineConfigs().front().limit.tc;
        CHECK(increment_tc.time == 200);
        CHECK(increment_tc.increment == 2);
    }

    TEST_CASE("Time controls should round fractional seconds to milliseconds") {
        const auto tc = cli::OptionsParser{engineArgsWithTc("60+0.6")}.getEngineConfigs().front().limit.tc;

        CHECK(tc.time == 60000);
        CHECK(tc.increment == 600);
    }

    TEST_CASE("Numeric options should reject trailing characters and invalid ranges") {
        CHECK_THROWS_AS(cli::OptionsParser{baseArgs({"-rounds", "10junk"})}, fastchess_exception);
        CHECK_THROWS_AS(cli::OptionsParser{baseArgs({"-srand", "-1"})}, fastchess_exception);
        CHECK_THROWS_AS(cli::OptionsParser{baseArgs({"-rounds", "999999999999999999999"})}, fastchess_exception);
        CHECK_THROWS_AS(cli::OptionsParser{baseArgs({"-sprt", "alpha=nan", "beta=0.05", "elo0=0", "elo1=1"})},
                        fastchess_exception);
    }

    TEST_CASE("Should throw engine with invalid restart") {
        const auto args = cli::Args{
            "fastchess.exe",
            "-engine",
            "dir=./",
            "cmd=app/tests/mock/engine/dummy_engine",
            "tc=10/1+0",
            "restart=true",
            "name=Alexandria-EA649FED",
            "-engine",
            "dir=./",
            "cmd=app/tests/mock/engine/dummy_engine",
            "name=Alexandria-27E42728",
            "tc=10/1+0",
        };

        CHECK_THROWS_WITH_AS(cli::OptionsParser{args},
                             "Error while reading option \"-engine\" with value \"name=Alexandria-EA649FED\"\nReason: "
                             "Invalid parameter (must be either \"on\" or \"off\"): true",
                             fastchess_exception);
    }

    TEST_CASE("Should throw error empty TB paths") {
        const auto args = cli::Args{
            "fastchess.exe",
            "-tb",
        };

        CHECK_THROWS_WITH_AS(cli::OptionsParser{args},
                             "Error while reading option \"-tb\" with value \"-tb\"\nReason: Option \"-tb\" expects exactly one value.",
                             fastchess_exception);
    }

    TEST_CASE("General Config Parsing") {
        const auto args = cli::Args{"fastchess.exe",
                                    "-engine",
                                    "dir=./",
                                    "cmd=app/tests/mock/engine/dummy_engine",
                                    "depth=5",
                                    "st=5",
                                    "nodes=5000",
                                    "option.Threads=1",
                                    "option.Hash=16",
                                    "name=Alexandria-EA649FED",
                                    "-engine",
                                    "dir=./",
                                    "cmd=app/tests/mock/engine/dummy_engine",
                                    "tc=40/1:9.65+0.1",
                                    "timemargin=243",
                                    "plies=7",
                                    "option.Threads=1",
                                    "option.Hash=32",
                                    "name=Alexandria-27E42728",
                                    "-openings",
                                    "file=./app/tests/data/test.epd",
                                    "format=epd",
                                    "order=random",
                                    "plies=16",
                                    "-rounds",
                                    "50",
                                    "-games",
                                    "2",
                                    "-pgnout",
                                    "file=PGNs/Alexandria-EA649FED_vs_Alexandria-27E42728",
                                    "-use-affinity",
                                    "0-1"};

        cli::OptionsParser options = cli::OptionsParser(args);

        CHECK(options.getTournamentConfig().affinity_cpus.size() == 2);
        CHECK(options.getTournamentConfig().affinity_cpus[0] == 0);
        CHECK(options.getTournamentConfig().affinity_cpus[1] == 1);

        auto configs = options.getEngineConfigs();

        CHECK(configs.size() == 2);
        EngineConfiguration config0 = configs[0];
        EngineConfiguration config1 = configs[1];
        CHECK(config0.name == "Alexandria-EA649FED");
        CHECK(config0.limit.tc.moves == 0);
        CHECK(config0.limit.tc.time == 0);
        CHECK(config0.limit.tc.increment == 0);
        CHECK(config0.limit.tc.timemargin == 0);
        CHECK(config0.limit.tc.fixed_time == 5000);
        CHECK(config0.limit.nodes == 5000);
        CHECK(config0.limit.plies == 5);
        CHECK(config0.options.at(0).first == "Threads");
        CHECK(config0.options.at(0).second == "1");
        CHECK(config0.options.at(1).first == "Hash");
        CHECK(config0.options.at(1).second == "16");
        CHECK(config1.name == "Alexandria-27E42728");
        CHECK(config1.limit.tc.moves == 40);
        CHECK(config1.limit.tc.time == 69650);
        CHECK(config1.limit.tc.fixed_time == 0);
        CHECK(config1.limit.tc.increment == 100);
        CHECK(config1.limit.tc.timemargin == 243);
        CHECK(config1.limit.nodes == 0);
        CHECK(config1.limit.plies == 7);
        CHECK(config1.options.at(0).first == "Threads");
        CHECK(config1.options.at(0).second == "1");
        CHECK(config1.options.at(1).first == "Hash");
        CHECK(config1.options.at(1).second == "32");
    }

    TEST_CASE("General Config Parsing 2") {
        const auto args = cli::Args{"fastchess.exe",
                                    "-engine",
                                    "dir=./",
                                    "cmd=app/tests/mock/engine/dummy_engine",
                                    "name=Alexandria-EA649FED",
                                    "tc=10/9.64",
                                    "-engine",
                                    "dir=./",
                                    "cmd=app/tests/mock/engine/dummy_engine",
                                    "name=Alexandria-27E42728",
                                    "tc=10/9.64",
                                    "-recover",
                                    "-concurrency",
                                    "1",
                                    "-ratinginterval",
                                    "2",
                                    "-scoreinterval",
                                    "3",
                                    "-autosaveinterval",
                                    "4",
                                    "-rounds",
                                    "256",
                                    "-draw",
                                    "movenumber=40",
                                    "movecount=3",
                                    "score=15",
                                    "-resign",
                                    "movecount=5",
                                    "score=600",
                                    "twosided=true",
                                    "-maxmoves",
                                    "150",
                                    "-games",
                                    "1",
                                    "-sprt",
                                    "alpha=0.05",
                                    "beta=0.05",
                                    "elo0=-1.5",
                                    "elo1=5",
                                    "model=bayesian",
                                    "-openings",
                                    "file=./app/tests/data/test.epd",
                                    "format=epd",
                                    "order=sequential",
                                    "plies=16",
                                    "start=4",
                                    "-variant",
                                    "fischerandom",
                                    "-output",
                                    "format=cutechess",
                                    "-srand",
                                    "1234",
                                    "-report",
                                    "penta=false",
                                    "-use-affinity",
                                    "-srand",
                                    "1234",
                                    "-epdout",
                                    "file=EPDs/Alexandria-EA649FED_vs_Alexandria-27E42728",
                                    "-pgnout",
                                    "file=PGNs/Alexandria-EA649FED_vs_Alexandria-27E42728",
                                    "nodes=true",
                                    "nps=true",
                                    "seldepth=true",
                                    "hashfull=true",
                                    "tbhits=true",
                                    "min=true"};

        cli::OptionsParser options = cli::OptionsParser(args);
        auto gameOptions           = options.getTournamentConfig();

        // Test proper cli settings
        CHECK(gameOptions.recover);
        CHECK(gameOptions.concurrency == 1);
        CHECK(gameOptions.ratinginterval == 2);
        CHECK(gameOptions.scoreinterval == 3);
        CHECK(gameOptions.autosaveinterval == 4);
        CHECK(gameOptions.games == 1);
        CHECK(gameOptions.rounds == 256);
        CHECK(gameOptions.variant == VariantType::FRC);
        CHECK(gameOptions.draw.move_number == 40);
        CHECK(gameOptions.draw.move_count == 3);
        CHECK(gameOptions.draw.score == 15);
        CHECK(gameOptions.draw.enabled);
        CHECK(gameOptions.resign.move_count == 5);
        CHECK(gameOptions.resign.score == 600);
        CHECK(gameOptions.resign.twosided);
        CHECK(gameOptions.resign.enabled);
        CHECK(gameOptions.maxmoves.move_count == 150);
        CHECK(gameOptions.maxmoves.enabled);
        CHECK(gameOptions.output == OutputType::CUTECHESS);
        CHECK(gameOptions.affinity);
        CHECK(gameOptions.seed == 1234);
        CHECK(gameOptions.report_penta == false);
        // Test Sprt options parsing
        CHECK(gameOptions.sprt.enabled);
        CHECK(gameOptions.sprt.alpha == 0.05);
        CHECK(gameOptions.sprt.beta == 0.05);
        CHECK(gameOptions.sprt.elo0 == -1.5);
        CHECK(gameOptions.sprt.elo1 == 5.0);
        CHECK(gameOptions.sprt.model == "bayesian");
        CHECK(gameOptions.pgn.file == "PGNs/Alexandria-EA649FED_vs_Alexandria-27E42728");
        CHECK(gameOptions.pgn.track_nodes);
        CHECK(gameOptions.pgn.track_seldepth);
        CHECK(gameOptions.pgn.track_nps);
        CHECK(gameOptions.pgn.track_hashfull);
        CHECK(gameOptions.pgn.track_tbhits);
        CHECK(gameOptions.pgn.min);
        CHECK(gameOptions.epd.file == "EPDs/Alexandria-EA649FED_vs_Alexandria-27E42728");
        // Test opening settings parsing
        CHECK(gameOptions.opening.file == "./app/tests/data/test.epd");
        CHECK(gameOptions.opening.format == FormatType::EPD);
        CHECK(gameOptions.opening.order == OrderType::SEQUENTIAL);
        CHECK(gameOptions.opening.plies == 16);
        CHECK(gameOptions.opening.start == 4);
    }

    TEST_CASE("Engine key/value propagation via -each") {
        const cli::OptionsParser parser{baseArgs({"-each", "option.Hash=128"})};
        const auto engines = parser.getEngineConfigs();

        REQUIRE(engines.size() == 2);
        auto hasHash = [](const EngineConfiguration &engine) {
            return std::any_of(engine.options.begin(), engine.options.end(), [&](const auto &opt) {
                return opt.first == "Hash" && opt.second == "128";
            });
        };
        CHECK(hasHash(engines[0]));
        CHECK(hasHash(engines[1]));
    }

    TEST_CASE("Default filename for pgnout and epdout") {
        const auto args = cli::Args{"fastchess.exe",
                                    "-engine",
                                    "dir=./",
                                    "cmd=app/tests/mock/engine/dummy_engine",
                                    "depth=5",
                                    "st=5",
                                    "name=Alexandria-EA649FED",
                                    "-engine",
                                    "dir=./",
                                    "cmd=app/tests/mock/engine/dummy_engine",
                                    "tc=40/1:9.65+0.1",
                                    "name=Alexandria-27E42728",
                                    "-openings",
                                    "file=./app/tests/data/test.epd",
                                    "-rounds",
                                    "50",
                                    "-games",
                                    "2",
                                    "-pgnout",
                                    "-epdout"};

        cli::OptionsParser options = cli::OptionsParser(args);

        const std::regex epd_pattern(R"(fastchess_\d{8}_\d{6}\.epd)");
        const std::regex pgn_pattern(R"(fastchess_\d{8}_\d{6}\.pgn)");

        CHECK(std::regex_match(options.getTournamentConfig().pgn.file, pgn_pattern));
        CHECK(std::regex_match(options.getTournamentConfig().epd.file, epd_pattern));
    }

    TEST_CASE("Scalar options update tournament config") {
        const cli::OptionsParser parser{baseArgs({"-concurrency",
                                                   "2",
                                                   "-force-concurrency",
                                                   "-ratinginterval",
                                                   "5",
                                                   "-scoreinterval",
                                                   "4",
                                                   "-autosaveinterval",
                                                   "7",
                                                   "-srand",
                                                   "123",
                                                   "-seeds",
                                                   "3",
                                                   "-wait",
                                                   "250",
                                                   "-noswap",
                                                   "-reverse"})};
        const auto config = parser.getTournamentConfig();

        CHECK(config.concurrency == 2);
        CHECK(config.force_concurrency);
        CHECK(config.ratinginterval == 5);
        CHECK(config.scoreinterval == 4);
        CHECK(config.autosaveinterval == 7);
        CHECK(config.seed == 123);
        CHECK(config.gauntlet_seeds == 3);
        CHECK(config.wait == 250);
        CHECK(config.noswap);
        CHECK(config.reverse);
    }

    TEST_CASE("Key/value options populate outputs and tablebases") {
        const cli::OptionsParser parser{baseArgs({"-pgnout",
                                                   "file=games.pgn",
                                                   "nodes=true",
                                                   "pv=true",
                                                   "tbhits=true",
                                                   "timeleft=true",
                                                   "latency=true",
                                                   "match_line=.*",
                                                   "notation=lan",
                                                   "-output",
                                                   "format=cutechess",
                                                   "-tb",
                                                   "app/tests/data/syzygy_wdl3",
                                                   "-tbignore50",
                                                   "-crc32",
                                                   "pgn=true",
                                                   "-site",
                                                   "Test",
                                                   "Arena"})};
        const auto config = parser.getTournamentConfig();

        CHECK(config.output == OutputType::CUTECHESS);
        CHECK(config.pgn.file == "games.pgn");
        CHECK(config.pgn.track_nodes);
        CHECK(config.pgn.track_pv);
        CHECK(config.pgn.track_tbhits);
        CHECK(config.pgn.track_timeleft);
        CHECK(config.pgn.track_latency);
        CHECK(config.pgn.notation == NotationType::LAN);
        CHECK(config.pgn.crc);
        CHECK(config.pgn.site == "TestArena");
        CHECK(config.tb_adjudication.enabled);
        CHECK(config.tb_adjudication.syzygy_dirs == "app/tests/data/syzygy_wdl3");
        CHECK(config.tb_adjudication.ignore_50_move_rule);
    }

    TEST_CASE("Tablebase adjudication parameters applied") {
        const cli::OptionsParser parser{baseArgs({"-tb",
                                                   "app/tests/data/syzygy_wdl3",
                                                   "-tbpieces",
                                                   "6",
                                                   "-tbadjudicate",
                                                   "DRAW"})};

        const auto config = parser.getTournamentConfig();
        CHECK(config.tb_adjudication.max_pieces == 6);
        CHECK(config.tb_adjudication.result_type == config::TbAdjudication::ResultType::DRAW);
    }

    TEST_CASE("Logging options configure logger") {
        const cli::OptionsParser parser{baseArgs({"-log",
                                                   "file=fast.log",
                                                   "level=trace",
                                                   "append=false",
                                                   "compress=true",
                                                   "realtime=true",
                                                   "engine=true"})};

        const auto &log = parser.getTournamentConfig().log;
        CHECK(log.file == "fast.log");
        CHECK(log.level == Logger::Level::TRACE);
        CHECK(!log.append_file);
        CHECK(log.compress);
        CHECK(log.realtime);
        CHECK(log.engine_coms);
    }

    TEST_CASE("Report option toggles pentanomial") {
        const cli::OptionsParser parser{baseArgs({"-report", "penta=false"})};
        CHECK_FALSE(parser.getTournamentConfig().report_penta);
    }

    TEST_CASE("Config option loads configuration file") {
        const cli::OptionsParser parser{
            buildArgs({"fastchess.exe", "-config", "file=app/tests/configs/config.json", "stats=false",
                       "outname=custom.json"})};

        const auto config = parser.getTournamentConfig();
        CHECK(config.rounds == 5);
        CHECK(config.check_mate_pvs);
        CHECK(config.show_latency);
        CHECK(config.log.realtime);
        CHECK(config.config_name == "custom.json");

        const auto engines = parser.getEngineConfigs();
        REQUIRE(engines.size() == 2);
        CHECK(engines[0].name == "engine1");
        CHECK(engines[1].name == "engine2");
    }

    TEST_CASE("Engine name should be auto-assigned and suffixed") {
        const auto args = cli::Args{
            "fastchess.exe",    "-engine", "dir=./", "cmd=app/tests/mock/engine/dummy_engine",
            "depth=5",          "-engine", "dir=./", "cmd=app/tests/mock/engine/dummy_engine",
            "tc=40/1:9.65+0.1", "-engine", "dir=./", "cmd=app/tests/mock/engine/dummy_engine",
            "tc=40/1:9.65+0.1"
        };

        cli::OptionsParser options = cli::OptionsParser(args);
        auto configs = options.getEngineConfigs();

        CHECK(configs[0].name == "dummy_engine");
        CHECK(configs[1].name == "dummy_engine_2");
        CHECK(configs[2].name == "dummy_engine_3");
    }

    TEST_CASE("Quick option creates two engines and presets tournament") {
        const cli::OptionsParser parser{
            buildArgs({"fastchess.exe",
                       "-quick",
                       "cmd=app/tests/mock/engine/dummy_engine",
                       "cmd=app/tests/mock/engine/dummy_engine",
                       "book=app/tests/data/test.epd"})};

        const auto config  = parser.getTournamentConfig();
        const auto engines = parser.getEngineConfigs();

        REQUIRE(engines.size() == 2);
        CHECK(config.games == 2);
        CHECK(config.rounds == 25000);
        CHECK(config.opening.file == "app/tests/data/test.epd");
        CHECK(config.opening.order == OrderType::RANDOM);
        CHECK(config.recover);
    }

    TEST_CASE("Repeat option resets games to default pair") {
        const cli::OptionsParser parser{baseArgs({"-games", "4", "-repeat"})};
        CHECK(parser.getTournamentConfig().games == 2);
    }

    TEST_CASE("Wait and event options stored") {
        const cli::OptionsParser parser{baseArgs({"-wait", "150", "-event", "My", "Event"})};
        const auto config = parser.getTournamentConfig();
        CHECK(config.wait == 150);
        CHECK(config.pgn.event_name == "MyEvent");
    }

    TEST_CASE("Tournament type selection with seeds") {
        const cli::OptionsParser parser{baseArgs({"-tournament", "gauntlet", "-seeds", "2"})};
        const auto config = parser.getTournamentConfig();
        CHECK(config.type == TournamentType::GAUNTLET);
        CHECK(config.gauntlet_seeds == 2);
    }

    TEST_CASE("Show latency and test environment toggles") {
        const cli::OptionsParser parser{baseArgs({"-show-latency", "-testEnv"})};
        const auto config = parser.getTournamentConfig();
        CHECK(config.show_latency);
        CHECK(config.test_env);
    }

    TEST_CASE("Debug option reports helpful error") {
        CHECK_THROWS_WITH_AS(cli::OptionsParser{baseArgs({"-debug"})},
                             "Error while reading option \"-debug\" with value \"-debug\"\nReason: The 'debug' option does not exist in fastchess. Use the 'log' option instead to write all engine input and output into a text file.",
                             fastchess_exception);
    }

    TEST_CASE("Unknown option should throw an error") {
        const auto args = cli::Args{"fastchess.exe", "-unknown"};
        CHECK_THROWS_AS(cli::OptionsParser{args}, fastchess_exception);
    }

    TEST_CASE("SPRT valid models logistic and normalized parse correctly") {
        const auto args1 = baseArgs({"-sprt", "alpha=0.05", "beta=0.05", "elo0=-1", "elo1=2", "model=logistic"});
        const auto args2 = baseArgs({"-sprt", "alpha=0.05", "beta=0.05", "elo0=-1", "elo1=2", "model=normalized"});
        CHECK(cli::OptionsParser(args1).getTournamentConfig().sprt.model == "logistic");
        CHECK(cli::OptionsParser(args2).getTournamentConfig().sprt.model == "normalized");
    }

    TEST_CASE("SPRT missing parameter should throw") {
        const auto args = baseArgs({"-sprt", "alpha=0.05", "elo0=0"});
        CHECK_THROWS_AS(cli::OptionsParser{args}, fastchess_exception);
    }

    TEST_CASE("Engine proto and args options parse correctly") {
        const cli::OptionsParser parser{baseArgs({"-each", "proto=uci", "args=--debug --fast"})};
        const auto engines = parser.getEngineConfigs();
        CHECK(engines[0].args == "--debug --fast");
    }

    TEST_CASE("Engine restart on/off accepted") {
        const auto args = baseArgs({"-engine", "restart=on", "name=foobar", "tc=10/1+0", "cmd=app/tests/mock/engine/dummy_engine"});
        const auto engines = cli::OptionsParser(args).getEngineConfigs();
        CHECK(engines[2].restart);
    }

    TEST_CASE("Adjudication WIN_LOSS and BOTH parse correctly") {
        const auto args1 = baseArgs({"-tb", "app/tests/data/syzygy_wdl3", "-tbadjudicate", "WIN_LOSS"});
        const auto args2 = baseArgs({"-tb", "app/tests/data/syzygy_wdl3", "-tbadjudicate", "BOTH"});
        const auto c1 = cli::OptionsParser(args1).getTournamentConfig();
        const auto c2 = cli::OptionsParser(args2).getTournamentConfig();
        CHECK(c1.tb_adjudication.result_type == config::TbAdjudication::ResultType::WIN_LOSS);
        CHECK(c2.tb_adjudication.result_type == config::TbAdjudication::ResultType::BOTH);
    }

    TEST_CASE("Output format fastchess parses correctly") {
        const auto args = baseArgs({"-output", "format=fastchess"});
        CHECK(cli::OptionsParser(args).getTournamentConfig().output == OutputType::FASTCHESS);
    }

    TEST_CASE("Report penta true explicitly sets flag") {
        const auto args = baseArgs({"-report", "penta=true"});
        CHECK(cli::OptionsParser(args).getTournamentConfig().report_penta);
    }

    TEST_CASE("Compliance mode throws until implemented") {
        const auto args = cli::Args{"fastchess.exe", "--compliance", "app/tests/mock/engine/dummy_engine"};
        CHECK_THROWS_AS(cli::OptionsParser{args}, fastchess_exception);
    }

    TEST_CASE("CRC32 false disables checksum") {
        const auto args = baseArgs({"-crc32", "pgn=false"});
        const auto config = cli::OptionsParser(args).getTournamentConfig();
        CHECK_FALSE(config.pgn.crc);
    }

    TEST_CASE("Invalid log level throws") {
        const auto args = baseArgs({"-log", "level=verbose"});
        CHECK_THROWS_AS(cli::OptionsParser{args}, fastchess_exception);
    }

    TEST_CASE("Complex affinity string parses correctly") {
        const auto args = baseArgs({"-use-affinity", "3,5,7-9"});
        const auto cpus = cli::OptionsParser(args).getTournamentConfig().affinity_cpus;
        CHECK(cpus == std::vector<int>({3,5,7,8,9}));
    }

    TEST_CASE("Use-affinity without args just enables affinity") {
        const auto args = baseArgs({"-use-affinity"});
        CHECK(cli::OptionsParser(args).getTournamentConfig().affinity);
    }

}
