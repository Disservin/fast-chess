#include <config/config.hpp>
#include <engine/uci_engine.hpp>
#include <types/engine_config.hpp>

#include <chrono>
#include <string_view>
#include <thread>

#include "doctest/doctest.hpp"

using namespace fastchess;

namespace {

const std::string path = "./app/tests/mock/engine/";

class MockUciEngine : public engine::UciEngine {
   public:
    explicit MockUciEngine(const EngineConfiguration& config, bool realtime_logging)
        : engine::UciEngine(config, realtime_logging) {}

    void restart() { engine::UciEngine::restart(); }
};

}  // namespace

TEST_SUITE("Uci Engine Communication Tests") {
    TEST_CASE("Test engine::UciEngine Args Simple") {
        EngineConfiguration config;
#ifdef _WIN64
        config.cmd = path + "dummy_engine.exe";
#else
        config.cmd = path + "dummy_engine";
#endif
        config.args = "arg1 arg2 arg3";

        engine::UciEngine uci_engine = engine::UciEngine(config, false);

        CHECK(uci_engine.start());

        for (const auto& line : uci_engine.output()) {
            std::cout << line.line << std::endl;
        }

        CHECK(uci_engine.output().size() == 6);
        CHECK(uci_engine.output()[0].line == "argv[1]: arg1");
        CHECK(uci_engine.output()[1].line == "argv[2]: arg2");
        CHECK(uci_engine.output()[2].line == "argv[3]: arg3");
    }

    TEST_CASE("Test engine::UciEngine Args Complex") {
        EngineConfiguration config;
#ifdef _WIN64
        config.cmd = path + "dummy_engine.exe";
#else
        config.cmd = path + "dummy_engine";
#endif
        config.args =
            "--backend=multiplexing "
            "--backend-opts=\"backend=cuda-fp16,(gpu=0),(gpu=1),(gpu=2),(gpu=3)\" "
            "--weights=lc0/BT4-1024x15x32h-swa-6147500.pb.gz --minibatch-size=132 "
            "--nncache=50000000 --threads=5";

        engine::UciEngine uci_engine = engine::UciEngine(config, false);

        CHECK(uci_engine.start());

        for (const auto& line : uci_engine.output()) {
            std::cout << line.line << std::endl;
        }

        CHECK(uci_engine.output().size() == 9);
        CHECK(uci_engine.output()[0].line == "argv[1]: --backend=multiplexing");
        CHECK(uci_engine.output()[1].line ==
              "argv[2]: --backend-opts=backend=cuda-fp16,(gpu=0),(gpu=1),(gpu=2),(gpu=3)");
        CHECK(uci_engine.output()[2].line == "argv[3]: --weights=lc0/BT4-1024x15x32h-swa-6147500.pb.gz");
        CHECK(uci_engine.output()[3].line == "argv[4]: --minibatch-size=132");
        CHECK(uci_engine.output()[4].line == "argv[5]: --nncache=50000000");
        CHECK(uci_engine.output()[5].line == "argv[6]: --threads=5");
    }

    TEST_CASE("Testing the EngineProcess class") {
        EngineConfiguration config;
#ifdef _WIN64
        config.cmd = path + "dummy_engine.exe";
#else
        config.cmd = path + "dummy_engine";
#endif
        config.args = "arg1 arg2 arg3";

        engine::UciEngine uci_engine = engine::UciEngine(config, false);

        CHECK(uci_engine.start());

        CHECK(uci_engine.output().size() == 6);
        CHECK(uci_engine.output()[0].line == "argv[1]: arg1");
        CHECK(uci_engine.output()[1].line == "argv[2]: arg2");
        CHECK(uci_engine.output()[2].line == "argv[3]: arg3");

        auto uciSuccess = uci_engine.uci();
        CHECK(uciSuccess);

        auto uci       = uci_engine.uciok();
        auto uciOutput = uci_engine.output();

        CHECK(uci);
        CHECK(uciOutput.size() == 3);
        CHECK(uciOutput[0].line == "line0");
        CHECK(uciOutput[1].line == "line1");
        CHECK(uciOutput[2].line == "uciok");
        CHECK(uci_engine.isready() == engine::process::Status::OK);

        CHECK(uci_engine.writeEngine("sleep"));
        const auto res = uci_engine.readEngine("done", std::chrono::milliseconds(100));
        CHECK(res == engine::process::Status::TIMEOUT);

        CHECK(uci_engine.writeEngine("sleep"));
        const auto res2 = uci_engine.readEngine("done", std::chrono::milliseconds(5000));
        CHECK(res2 == engine::process::Status::OK);
        CHECK(uci_engine.output().size() == 1);
        CHECK(uci_engine.output()[0].line == "done");
    }

    TEST_CASE("Testing the EngineProcess class with lower level class functions") {
        EngineConfiguration config;
#ifdef _WIN64
        config.cmd = path + "dummy_engine.exe";
#else
        config.cmd = path + "dummy_engine";
#endif
        engine::UciEngine uci_engine = engine::UciEngine(config, false);

        CHECK(uci_engine.start());

        CHECK(uci_engine.writeEngine("uci"));
        const auto res = uci_engine.readEngine("uciok");

        CHECK(res == engine::process::Status::OK);
        CHECK(uci_engine.output().size() == 3);
        CHECK(uci_engine.output()[0].line == "line0");
        CHECK(uci_engine.output()[1].line == "line1");
        CHECK(uci_engine.output()[2].line == "uciok");

        CHECK(uci_engine.writeEngine("isready"));
        const auto res2 = uci_engine.readEngine("readyok");
        CHECK(res2 == engine::process::Status::OK);
        CHECK(uci_engine.output().size() == 1);
        CHECK(uci_engine.output()[0].line == "readyok");

        CHECK(uci_engine.writeEngine("sleep"));
        const auto res3 = uci_engine.readEngine("done", std::chrono::milliseconds(100));
        CHECK(res3 == engine::process::Status::TIMEOUT);

        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        CHECK(uci_engine.writeEngine("sleep"));
        const auto res4 = uci_engine.readEngine("done", std::chrono::milliseconds(5000));
        CHECK(res4 == engine::process::Status::OK);
        CHECK(uci_engine.output().size() == 1);
        CHECK(uci_engine.output()[0].line == "done");
    }

    TEST_CASE("Restarting the engine") {
        EngineConfiguration config;
#ifdef _WIN64
        config.cmd = path + "dummy_engine.exe";
#else
        config.cmd = path + "dummy_engine";
#endif
        MockUciEngine uci_engine = MockUciEngine(config, false);

        CHECK(uci_engine.start());

        CHECK(uci_engine.writeEngine("uci"));
        const auto res = uci_engine.readEngine("uciok");

        CHECK(res == engine::process::Status::OK);
        CHECK(uci_engine.output().size() == 3);
        CHECK(uci_engine.output()[0].line == "line0");
        CHECK(uci_engine.output()[1].line == "line1");
        CHECK(uci_engine.output()[2].line == "uciok");

        uci_engine.restart();

        CHECK(uci_engine.writeEngine("uci"));
        const auto res2 = uci_engine.readEngine("uciok");

        CHECK(res2 == engine::process::Status::OK);
        CHECK(uci_engine.output().size() == 3);
        CHECK(uci_engine.output()[0].line == "line0");
        CHECK(uci_engine.output()[1].line == "line1");
        CHECK(uci_engine.output()[2].line == "uciok");
    }
}
