#include <core/config/config.hpp>
#include <core/globals/globals.hpp>
#include <engine/uci_engine.hpp>
#include <types/engine_config.hpp>

#include <chrono>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifndef _WIN64
#    include <signal.h>
#endif

#include <doctest/doctest.hpp>

using namespace fastchess;

namespace {

#ifndef _WIN64
TEST_CASE("Ignoring SIGPIPE allows broken engine pipes to be handled") {
    setCtrlCHandler();

    struct sigaction action {};
    REQUIRE(sigaction(SIGPIPE, nullptr, &action) == 0);
    CHECK(action.sa_handler == SIG_IGN);
}
#endif

#ifdef _WIN64
const std::string_view path = "./app/tests/mock/engine/dummy_engine.exe";
#else
const std::string_view path = "./app/tests/mock/engine/dummy_engine";
#endif

class MockUciEngine : public engine::UciEngine {
   public:
    explicit MockUciEngine(const EngineConfiguration& config, bool realtime_logging)
        : engine::UciEngine(config, realtime_logging) {}
};

std::vector<std::string> dumpCommands(engine::UciEngine& uci_engine) {
    CHECK(uci_engine.writeEngine("dump_commands"));
    const auto result = uci_engine.readEngine("commands done");
    CHECK(result.code == engine::process::Status::OK);

    std::vector<std::string> commands;
    for (const auto* line : uci_engine.getStdoutLines()) {
        constexpr std::string_view prefix = "command: ";
        if (line->line.rfind(prefix, 0) == 0) {
            commands.push_back(line->line.substr(prefix.size()));
        }
    }

    if (!commands.empty() && commands.front() == "uci") commands.erase(commands.begin());
    if (!commands.empty() && commands.back() == "dump_commands") commands.pop_back();

    return commands;
}

auto startTestEngine(engine::UciEngine& uci_engine) {
    if (!config::TournamentConfig) {
        config::TournamentConfig = std::make_unique<config::Tournament>();
    }
    return uci_engine.start(/*cpus*/ std::nullopt);
}

}  // namespace

TEST_SUITE("Uci Engine Communication Tests") {
    TEST_CASE("Parse UCI info") {
        const auto info = engine::UciEngine::parseInfo(
            "info depth 18 seldepth 27 multipv 2 score cp -31 upperbound nodes 123456 nps 789000 time 156 "
            "hashfull 42 tbhits 7 pv e2e4 e7e5 g1f3 string ignored");

        REQUIRE(info.score.has_value());
        CHECK(info.score->isCp());
        CHECK(info.score->value == -31);
        CHECK(info.depth == 18);
        CHECK(info.seldepth == 27);
        CHECK(info.multipv == 2);
        CHECK(info.nodes == 123456);
        CHECK(info.nps == 789000);
        CHECK(info.time == 156);
        CHECK(info.hashfull == 42);
        CHECK(info.tbhits == 7);
        CHECK_FALSE(info.lowerbound);
        CHECK(info.upperbound);
        CHECK(info.pv == std::vector<std::string>{"e2e4", "e7e5", "g1f3"});
    }

    TEST_CASE("Parse lowerbound UCI info updates") {
        const auto lowerbound = engine::UciEngine::parseInfo(
            "info depth 20 score cp 42 lowerbound nodes 234567 nps 890000 time 200 pv d2d4 d7d5");

        REQUIRE(lowerbound.score.has_value());
        CHECK(lowerbound.score->value == 42);
        CHECK(lowerbound.lowerbound);
        CHECK_FALSE(lowerbound.upperbound);
        CHECK(lowerbound.isBound());
        CHECK(lowerbound.pv == std::vector<std::string>{"d2d4", "d7d5"});

        const auto partial = engine::UciEngine::parseInfo("info depth 21 score cp 51 lowerbound");

        REQUIRE(partial.score.has_value());
        CHECK(partial.score->value == 51);
        CHECK(partial.depth == 21);
        CHECK(partial.lowerbound);
        CHECK(partial.isBound());
        CHECK_FALSE(partial.nodes.has_value());
        CHECK_FALSE(partial.nps.has_value());
        CHECK_FALSE(partial.time.has_value());
        CHECK(partial.pv.empty());
    }

    TEST_CASE("Reject invalid UCI info integers") {
        const auto info = engine::UciEngine::parseInfo(
            "info depth 12x nodes -1 nps 18446744073709551616 score cp 9223372036854775808");

        CHECK_FALSE(info.depth.has_value());
        CHECK_FALSE(info.nodes.has_value());
        CHECK_FALSE(info.nps.has_value());
        CHECK_FALSE(info.score.has_value());
    }

    TEST_CASE("Generate exact position commands") {
        EngineConfiguration config;
        config.cmd = path;

        engine::UciEngine uci_engine(config, false);
        REQUIRE(startTestEngine(uci_engine));

        CHECK(uci_engine.position({}, "startpos"));

        const std::vector<std::string_view> startpos_moves = {"e2e4", "e7e5"};
        CHECK(uci_engine.position(startpos_moves, "startpos"));

        const std::string fen = "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3";
        const std::vector<std::string_view> fen_moves = {"f1b5", "a7a6"};
        CHECK(uci_engine.position(fen_moves, fen));

        CHECK(dumpCommands(uci_engine) == std::vector<std::string>{"position startpos",
                                                                   "position startpos moves e2e4 e7e5",
                                                                   "position fen " + fen + " moves f1b5 a7a6"});
    }

    TEST_CASE("Generate exact fixed-limit go command") {
        EngineConfiguration config;
        config.cmd         = path;
        config.limit.nodes = 123456;
        config.limit.plies = 17;

        engine::UciEngine uci_engine(config, false);
        REQUIRE(startTestEngine(uci_engine));

        TimeControl::Limits limits;
        limits.fixed_time = 2500;
        CHECK(uci_engine.go(TimeControl(limits), TimeControl(), chess::Color::WHITE));

        CHECK(dumpCommands(uci_engine) == std::vector<std::string>{"go nodes 123456 depth 17 movetime 2500"});
    }

    TEST_CASE("Generate exact clock go command") {
        EngineConfiguration config;
        config.cmd = path;

        engine::UciEngine uci_engine(config, false);
        REQUIRE(startTestEngine(uci_engine));

        TimeControl::Limits our_limits;
        our_limits.time      = 10000;
        our_limits.increment = 100;
        our_limits.moves     = 40;

        TimeControl::Limits enemy_limits;
        enemy_limits.time      = 20000;
        enemy_limits.increment = 250;

        CHECK(uci_engine.go(TimeControl(our_limits), TimeControl(enemy_limits), chess::Color::WHITE));
        CHECK(uci_engine.go(TimeControl(our_limits), TimeControl(enemy_limits), chess::Color::BLACK));

        CHECK(dumpCommands(uci_engine) ==
              std::vector<std::string>{"go wtime 10100 btime 20250 winc 100 binc 250 movestogo 40",
                                       "go wtime 20250 btime 10100 winc 250 binc 100 movestogo 40"});
    }

    TEST_CASE("Generate enemy increment when our clock has no increment") {
        EngineConfiguration config;
        config.cmd = path;

        engine::UciEngine uci_engine(config, false);
        REQUIRE(startTestEngine(uci_engine));

        TimeControl::Limits our_limits;
        our_limits.time = 30000;

        TimeControl::Limits enemy_limits;
        enemy_limits.time      = 45000;
        enemy_limits.increment = 500;

        CHECK(uci_engine.go(TimeControl(our_limits), TimeControl(enemy_limits), chess::Color::WHITE));

        CHECK(dumpCommands(uci_engine) == std::vector<std::string>{"go wtime 30000 btime 45500 binc 500"});
    }

    TEST_CASE("Test engine::UciEngine Args Simple") {
        EngineConfiguration config;
        config.cmd  = path;
        config.args = "arg1 arg2 arg3";

        engine::UciEngine uci_engine = engine::UciEngine(config, false);

        CHECK(uci_engine.start(/*cpus*/ std::nullopt));

        for (const auto& line : uci_engine.getStdoutLines()) {
            std::cout << line->line << std::endl;
        }

        CHECK(uci_engine.getStdoutLines().size() == 12);
        CHECK(uci_engine.getStdoutLines()[0]->line == "argv[1]: arg1");
        CHECK(uci_engine.getStdoutLines()[1]->line == "argv[2]: arg2");
        CHECK(uci_engine.getStdoutLines()[2]->line == "argv[3]: arg3");

        CHECK(uci_engine.idName().has_value());
        CHECK(uci_engine.idName().value() == "Dummy Engine");

        CHECK(uci_engine.idAuthor().has_value());
        CHECK(uci_engine.idAuthor().value() == "Fastchess");
        CHECK(uci_engine.getStdoutLines().size() == 12);
    }

    TEST_CASE("Test engine::UciEngine Args Complex") {
        EngineConfiguration config;
        config.cmd = path;
        config.args =
            "--backend=multiplexing "
            "--backend-opts=\"backend=cuda-fp16,(gpu=0),(gpu=1),(gpu=2),(gpu=3)\" "
            "--weights=lc0/BT4-1024x15x32h-swa-6147500.pb.gz --minibatch-size=132 "
            "--nncache=50000000 --threads=5";

        engine::UciEngine uci_engine = engine::UciEngine(config, false);

        CHECK(uci_engine.start(/*cpus*/ std::nullopt));

        for (const auto& line : uci_engine.getStdoutLines()) {
            std::cout << line->line << std::endl;
        }

        CHECK(uci_engine.getStdoutLines().size() == 15);
        CHECK(uci_engine.getStdoutLines()[0]->line == "argv[1]: --backend=multiplexing");
        CHECK(uci_engine.getStdoutLines()[1]->line ==
              "argv[2]: --backend-opts=backend=cuda-fp16,(gpu=0),(gpu=1),(gpu=2),(gpu=3)");
        CHECK(uci_engine.getStdoutLines()[2]->line == "argv[3]: --weights=lc0/BT4-1024x15x32h-swa-6147500.pb.gz");
        CHECK(uci_engine.getStdoutLines()[3]->line == "argv[4]: --minibatch-size=132");
        CHECK(uci_engine.getStdoutLines()[4]->line == "argv[5]: --nncache=50000000");
        CHECK(uci_engine.getStdoutLines()[5]->line == "argv[6]: --threads=5");
    }

    TEST_CASE("Testing the EngineProcess class") {
        EngineConfiguration config;
        config.cmd  = path;
        config.args = "arg1 arg2 arg3";

        engine::UciEngine uci_engine = engine::UciEngine(config, false);

        CHECK(uci_engine.start(/*cpus*/ std::nullopt));

        CHECK(uci_engine.getStdoutLines().size() == 12);
        CHECK(uci_engine.getStdoutLines()[0]->line == "argv[1]: arg1");
        CHECK(uci_engine.getStdoutLines()[1]->line == "argv[2]: arg2");
        CHECK(uci_engine.getStdoutLines()[2]->line == "argv[3]: arg3");

        auto uciSuccess = uci_engine.uci();
        CHECK(uciSuccess);

        auto uci       = uci_engine.uciok();
        auto uciOutput = uci_engine.getStdoutLines();

        CHECK(uci);
        CHECK(uciOutput.size() == 9);
        CHECK(uciOutput[0]->line == "id name Dummy Engine");
        CHECK(uciOutput[1]->line == "id author Fastchess");
        CHECK(uciOutput[2]->line == "option name Threads type spin default 1 min 1 max 1024");
        CHECK(uciOutput[3]->line == "option name Hash type spin default 1 min 1 max 500000");
        CHECK(uciOutput[4]->line == "option name MultiPV type spin default 1 min 1 max 256");
        CHECK(uciOutput[5]->line == "option name UCI_Chess960 type check default false");
        CHECK(uciOutput[6]->line == "line0");
        CHECK(uciOutput[7]->line == "line1");
        CHECK(uciOutput[8]->line == "uciok");
        CHECK(uci_engine.isready().code == engine::process::Status::OK);

        CHECK(uci_engine.writeEngine("sleep"));
        const auto res = uci_engine.readEngine("done", std::chrono::milliseconds(100));
        CHECK(res.code == engine::process::Status::TIMEOUT);

        CHECK(uci_engine.writeEngine("sleep"));
        const auto res2 = uci_engine.readEngine("done", std::chrono::milliseconds(5000));
        CHECK(res2.code == engine::process::Status::OK);
        CHECK(uci_engine.getStdoutLines().size() == 1);
        CHECK(uci_engine.getStdoutLines()[0]->line == "done");
    }

    TEST_CASE("Testing the EngineProcess class with lower level class functions") {
        EngineConfiguration config;
        config.cmd = path;

        engine::UciEngine uci_engine = engine::UciEngine(config, false);

        CHECK(uci_engine.start(/*cpus*/ std::nullopt));

        CHECK(uci_engine.writeEngine("uci"));
        const auto res = uci_engine.readEngine("uciok");

        CHECK(res.code == engine::process::Status::OK);
        CHECK(uci_engine.getStdoutLines().size() == 9);
        CHECK(uci_engine.getStdoutLines()[0]->line == "id name Dummy Engine");
        CHECK(uci_engine.getStdoutLines()[1]->line == "id author Fastchess");
        CHECK(uci_engine.getStdoutLines()[2]->line == "option name Threads type spin default 1 min 1 max 1024");
        CHECK(uci_engine.getStdoutLines()[3]->line == "option name Hash type spin default 1 min 1 max 500000");
        CHECK(uci_engine.getStdoutLines()[4]->line == "option name MultiPV type spin default 1 min 1 max 256");
        CHECK(uci_engine.getStdoutLines()[5]->line == "option name UCI_Chess960 type check default false");
        CHECK(uci_engine.getStdoutLines()[6]->line == "line0");
        CHECK(uci_engine.getStdoutLines()[7]->line == "line1");
        CHECK(uci_engine.getStdoutLines()[8]->line == "uciok");

        CHECK(uci_engine.writeEngine("isready"));
        const auto res2 = uci_engine.readEngine("readyok");
        CHECK(res2.code == engine::process::Status::OK);
        CHECK(uci_engine.getStdoutLines().size() == 1);
        CHECK(uci_engine.getStdoutLines()[0]->line == "readyok");

        CHECK(uci_engine.writeEngine("sleep"));
        const auto res3 = uci_engine.readEngine("done", std::chrono::milliseconds(100));
        CHECK(res3.code == engine::process::Status::TIMEOUT);

        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        CHECK(uci_engine.writeEngine("sleep"));
        const auto res4 = uci_engine.readEngine("done", std::chrono::milliseconds(5000));
        CHECK(res4.code == engine::process::Status::OK);
        CHECK(uci_engine.getStdoutLines().size() == 1);
        CHECK(uci_engine.getStdoutLines()[0]->line == "done");
    }

    TEST_CASE("Restarting the engine") {
        EngineConfiguration config;
        config.cmd = path;

        std::unique_ptr<engine::UciEngine> uci_engine = std::make_unique<MockUciEngine>(config, false);

        CHECK(uci_engine->start(/*cpus*/ std::nullopt));

        CHECK(uci_engine->writeEngine("uci"));
        const auto res = uci_engine->readEngine("uciok");

        CHECK(res.code == engine::process::Status::OK);
        CHECK(uci_engine->getStdoutLines().size() == 9);
        CHECK(uci_engine->getStdoutLines()[0]->line == "id name Dummy Engine");
        CHECK(uci_engine->getStdoutLines()[1]->line == "id author Fastchess");
        CHECK(uci_engine->getStdoutLines()[2]->line == "option name Threads type spin default 1 min 1 max 1024");
        CHECK(uci_engine->getStdoutLines()[3]->line == "option name Hash type spin default 1 min 1 max 500000");
        CHECK(uci_engine->getStdoutLines()[4]->line == "option name MultiPV type spin default 1 min 1 max 256");
        CHECK(uci_engine->getStdoutLines()[5]->line == "option name UCI_Chess960 type check default false");
        CHECK(uci_engine->getStdoutLines()[6]->line == "line0");
        CHECK(uci_engine->getStdoutLines()[7]->line == "line1");
        CHECK(uci_engine->getStdoutLines()[8]->line == "uciok");

        uci_engine = std::make_unique<MockUciEngine>(config, false);

        CHECK(uci_engine->start(/*cpus*/ std::nullopt));

        CHECK(uci_engine->writeEngine("uci"));
        const auto res2 = uci_engine->readEngine("uciok");

        CHECK(res2.code == engine::process::Status::OK);
        CHECK(uci_engine->getStdoutLines().size() == 9);
        CHECK(uci_engine->getStdoutLines()[0]->line == "id name Dummy Engine");
        CHECK(uci_engine->getStdoutLines()[1]->line == "id author Fastchess");
        CHECK(uci_engine->getStdoutLines()[2]->line == "option name Threads type spin default 1 min 1 max 1024");
        CHECK(uci_engine->getStdoutLines()[3]->line == "option name Hash type spin default 1 min 1 max 500000");
        CHECK(uci_engine->getStdoutLines()[4]->line == "option name MultiPV type spin default 1 min 1 max 256");
        CHECK(uci_engine->getStdoutLines()[5]->line == "option name UCI_Chess960 type check default false");
        CHECK(uci_engine->getStdoutLines()[6]->line == "line0");
        CHECK(uci_engine->getStdoutLines()[7]->line == "line1");
        CHECK(uci_engine->getStdoutLines()[8]->line == "uciok");
    }

    TEST_CASE("Restarting the engine") {
        EngineConfiguration config;
        config.cmd = path;

        std::unique_ptr<engine::UciEngine> uci_engine = std::make_unique<MockUciEngine>(config, false);

        CHECK(uci_engine->start(/*cpus*/ std::nullopt));

        CHECK(uci_engine->writeEngine("uci"));
        const auto res = uci_engine->readEngine("uciok");

        CHECK(res.code == engine::process::Status::OK);
        CHECK(uci_engine->getStdoutLines().size() == 9);
        CHECK(uci_engine->getStdoutLines()[0]->line == "id name Dummy Engine");
        CHECK(uci_engine->getStdoutLines()[1]->line == "id author Fastchess");
        CHECK(uci_engine->getStdoutLines()[2]->line == "option name Threads type spin default 1 min 1 max 1024");
        CHECK(uci_engine->getStdoutLines()[3]->line == "option name Hash type spin default 1 min 1 max 500000");
        CHECK(uci_engine->getStdoutLines()[4]->line == "option name MultiPV type spin default 1 min 1 max 256");
        CHECK(uci_engine->getStdoutLines()[5]->line == "option name UCI_Chess960 type check default false");
        CHECK(uci_engine->getStdoutLines()[6]->line == "line0");
        CHECK(uci_engine->getStdoutLines()[7]->line == "line1");
        CHECK(uci_engine->getStdoutLines()[8]->line == "uciok");

        uci_engine = std::make_unique<MockUciEngine>(config, false);

        CHECK(uci_engine->start(/*cpus*/ std::nullopt));

        CHECK(uci_engine->writeEngine("uci"));
        const auto res2 = uci_engine->readEngine("uciok");

        CHECK(res2.code == engine::process::Status::OK);
        CHECK(uci_engine->getStdoutLines().size() == 9);
        CHECK(uci_engine->getStdoutLines()[0]->line == "id name Dummy Engine");
        CHECK(uci_engine->getStdoutLines()[1]->line == "id author Fastchess");
        CHECK(uci_engine->getStdoutLines()[2]->line == "option name Threads type spin default 1 min 1 max 1024");
        CHECK(uci_engine->getStdoutLines()[3]->line == "option name Hash type spin default 1 min 1 max 500000");
        CHECK(uci_engine->getStdoutLines()[4]->line == "option name MultiPV type spin default 1 min 1 max 256");
        CHECK(uci_engine->getStdoutLines()[5]->line == "option name UCI_Chess960 type check default false");
        CHECK(uci_engine->getStdoutLines()[6]->line == "line0");
        CHECK(uci_engine->getStdoutLines()[7]->line == "line1");
        CHECK(uci_engine->getStdoutLines()[8]->line == "uciok");
    }

    TEST_CASE("Sending uci options before ucinewgame and expect Threads to be first") {
        EngineConfiguration config;
        config.cmd     = path;
        config.options = {
            {"Hash", "1600"},
            {"MultiPV", "3"},
            {"UCI_Chess960", "true"},
            {"Threads", "4"},
        };

        std::unique_ptr<engine::UciEngine> uci_engine = std::make_unique<MockUciEngine>(config, false);

        CHECK(uci_engine->start(/*cpus*/ std::nullopt));

        CHECK(uci_engine->refreshUci());

        CHECK(uci_engine->writeEngine("dump_commands"));
        const auto res = uci_engine->readEngine("commands done");

        CHECK(res.code == engine::process::Status::OK);
        REQUIRE(uci_engine->getStdoutLines().size() >= 8);
        CHECK(uci_engine->getStdoutLines()[0]->line == "command: uci");
        CHECK(uci_engine->getStdoutLines()[1]->line == "command: setoption name Threads value 4");
        CHECK(uci_engine->getStdoutLines()[2]->line == "command: setoption name Hash value 1600");
        CHECK(uci_engine->getStdoutLines()[3]->line == "command: setoption name MultiPV value 3");
        CHECK(uci_engine->getStdoutLines()[4]->line == "command: setoption name UCI_Chess960 value true");
        CHECK(uci_engine->getStdoutLines()[5]->line == "command: ucinewgame");
        CHECK(uci_engine->getStdoutLines()[6]->line == "command: isready");
        CHECK(uci_engine->getStdoutLines()[7]->line == "command: dump_commands");
    }

    TEST_CASE("Sending a button option once without a value") {
        EngineConfiguration config;
        config.cmd     = path;
        config.args    = "--button-option";
        config.options = {{"Clear Hash", "true"}};

        MockUciEngine uci_engine(config, false);

        CHECK(uci_engine.start(/*cpus*/ std::nullopt));
        CHECK(uci_engine.refreshUci());
        CHECK(uci_engine.writeEngine("dump_commands"));

        const auto res = uci_engine.readEngine("commands done");
        CHECK(res.code == engine::process::Status::OK);
        REQUIRE(uci_engine.getStdoutLines().size() == 6);
        CHECK(uci_engine.getStdoutLines()[0]->line == "command: uci");
        CHECK(uci_engine.getStdoutLines()[1]->line == "command: setoption name Clear Hash");
        CHECK(uci_engine.getStdoutLines()[2]->line == "command: ucinewgame");
        CHECK(uci_engine.getStdoutLines()[3]->line == "command: isready");
        CHECK(uci_engine.getStdoutLines()[4]->line == "command: dump_commands");
        CHECK(uci_engine.getStdoutLines()[5]->line == "commands done");
    }

    TEST_CASE("Detect crashed engine while waiting for bestmove with fixed nodes") {
        engine::process::Process process;
        std::vector<engine::process::Line> output;

        process.setRealtimeLogging(false);

        CHECK(process.init(".", std::string(path), "--crash-on-go-nodes", "dummy").code == engine::process::Status::OK);
        CHECK(process.writeInput("go nodes 1\n").code == engine::process::Status::OK);

        const auto res = process.readOutput(output, "bestmove", std::chrono::milliseconds(0));

        CHECK(res.code == engine::process::Status::ERR);
        CHECK(process.writeInput("stop\n").code == engine::process::Status::ERR);
        CHECK(process.writeInput("quit\n").code == engine::process::Status::ERR);
    }
}
