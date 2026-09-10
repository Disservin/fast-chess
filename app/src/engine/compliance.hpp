#pragma once

#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <core/config/config.hpp>
#include <engine/uci_engine.hpp>

#include <fmt/include/fmt/core.h>

namespace fastchess::engine {

inline bool isValidInfoLine(const std::string& infoLine) {
    std::istringstream iss(infoLine);
    std::string token;

    if (!(iss >> token) || token != "info") {
        std::cerr << fmt::format("\r\nInvalid info line format: {}\n", infoLine) << std::flush;
        return false;
    }

    while (iss >> token) {
        if (token == "time" || token == "nps" || token == "score") {
            if (!(iss >> token)) {
                std::cerr << fmt::format("\r\nNo value after token: {}\n", token) << std::flush;
                return false;
            }

            if (token.find('.') != std::string::npos) {
                std::cerr << fmt::format("\r\nTime/NPS/Score value is not an integer: {}\n", token) << std::flush;
                return false;
            }
        }
    }

    return true;
}

inline bool compliant(int argc, char const* argv[]) {
    config::TournamentConfig = std::make_unique<config::Tournament>(config::Tournament{});

    int step = 0;

    EngineConfiguration config;
    config.cmd  = argv[2];
    config.args = argc > 3 ? std::string(argv[3]) : "";

    UciEngine uci_engine(config, false);

    auto executeStep = [&step, &uci_engine](const std::string& description, const std::function<bool()>& action) {
        step++;

        std::cout << fmt::format("Step {}: {}...", step, description) << std::flush;

        if (!action()) {
            std::cerr << fmt::format("\r\033[1;31m Failed\033[0m Step {}: {}\n", step, description) << std::flush;

            std::cerr << "\033[1;33m Your engine's stdout output was:\033[0m" << std::endl;
            for (const auto& line : uci_engine.getStdoutLines())
                std::cerr << fmt::format(" {}\n", line->line) << std::flush;

            if (uci_engine.getStderrLines().size()) {
                std::cerr << "\033[1;33m Your engine's stderr output was:\033[0m" << std::endl;
                for (const auto& line : uci_engine.getStderrLines())
                    std::cerr << fmt::format(" {}\n", line->line) << std::flush;
            }

            return false;
        }

        std::cout << fmt::format("\r\033[1;32m Passed\033[0m Step {}: {}\n", step, description) << std::flush;

        return true;
    };

    std::vector<std::pair<std::string, std::function<bool()>>> steps = {
        {"Start the engine", [&uci_engine] { return uci_engine.start(/*cpus*/ std::nullopt).has_value(); }},
        {"Check if engine is ready", [&uci_engine] { return uci_engine.isready().code == process::Status::OK; }},
        {"Check id name", [&uci_engine] { return uci_engine.idName().has_value(); }},
        {"Check id author", [&uci_engine] { return uci_engine.idAuthor().has_value(); }},
        {"Send ucinewgame", [&uci_engine] { return uci_engine.ucinewgame(); }},
        {"Set position to startpos", [&uci_engine] { return uci_engine.writeEngine("position startpos"); }},
        {"Check if engine is ready after startpos",
         [&uci_engine] { return uci_engine.isready().code == process::Status::OK; }},
        {"Set position to fen",
         [&uci_engine] {
             return uci_engine.writeEngine(
                 "position fen 3r2k1/p5n1/1pq1p2p/2p3p1/2P1P1n1/1P1P2pP/PN1Q2K1/5R2 w - - 0 27");
         }},

        {"Check if engine is ready after fen",
         [&uci_engine] { return uci_engine.isready().code == process::Status::OK; }},
        {"Send go wtime 100", [&uci_engine] { return uci_engine.writeEngine("go wtime 100"); }},
        {"Read bestmove", [&uci_engine] { return uci_engine.readEngine("bestmove").code == process::Status::OK; }},
        {"Check if engine prints an info line", [&uci_engine] { return !uci_engine.lastInfoLine().empty(); }},
        {"Verify info line format is valid", [&uci_engine] { return isValidInfoLine(uci_engine.lastInfoLine()); }},
        {"Verify info line contains score",
         [&uci_engine] { return str_utils::contains(uci_engine.lastInfoLine(), "score"); }},
        {"Set position to black to move",
         [&uci_engine] {
             return uci_engine.writeEngine("position fen rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1");
         }},
        {"Send go btime 100", [&uci_engine] { return uci_engine.writeEngine("go btime 100"); }},
        {"Read bestmove after go btime 100",
         [&uci_engine] { return uci_engine.readEngine("bestmove").code == process::Status::OK; }},
        {"Check if engine prints an info line after go btime 100",
         [&uci_engine] { return !uci_engine.lastInfoLine().empty(); }},
        {"Verify info line format is valid after go btime 100",
         [&uci_engine] { return isValidInfoLine(uci_engine.lastInfoLine()); }},
        {"Check if engine prints an info line with the score after go btime 100",
         [&uci_engine] { return str_utils::contains(uci_engine.lastInfoLine(), "score"); }},
        {"Send go wtime 100 winc 100 btime 100 binc 100",
         [&uci_engine] { return uci_engine.writeEngine("go wtime 100 winc 100 btime 100 binc 100"); }},
        {"Read bestmove after go wtime 100 winc 100 btime 100 binc 100",
         [&uci_engine] { return uci_engine.readEngine("bestmove").code == process::Status::OK; }},
        {"Check if engine prints an info line after go wtime 100 winc 100",
         [&uci_engine] { return !uci_engine.lastInfoLine().empty(); }},
        {"Verify info line format is valid after go wtime 100 winc 100",
         [&uci_engine] { return isValidInfoLine(uci_engine.lastInfoLine()); }},
        {"Check if engine prints an info line with the score after go wtime 100 winc 100",
         [&uci_engine] { return str_utils::contains(uci_engine.lastInfoLine(), "score"); }},
        {"Send go btime 100 binc 100 wtime 100 winc 100",
         [&uci_engine] { return uci_engine.writeEngine("go btime 100 binc 100 wtime 100 winc 100"); }},
        {"Read bestmove after go btime 100 binc 100 wtime 100 winc 100",
         [&uci_engine] { return uci_engine.readEngine("bestmove").code == process::Status::OK; }},
        {"Check if engine prints an info line after go btime 100 binc 100",
         [&uci_engine] { return !uci_engine.lastInfoLine().empty(); }},
        {"Verify info line format is valid after go btime 100 binc 100",
         [&uci_engine] { return isValidInfoLine(uci_engine.lastInfoLine()); }},
        {"Check if engine prints an info line with the score after go btime 100 binc 100",
         [&uci_engine] { return str_utils::contains(uci_engine.lastInfoLine(), "score"); }},
        {"Check if engine prints an info line after go btime 100 binc 100",
         [&uci_engine] { return str_utils::contains(uci_engine.lastInfoLine(), "score"); }},
        // Simulate a game
        {"Send ucinewgame", [&uci_engine] { return uci_engine.ucinewgame(); }},
        {"Set position to startpos", [&uci_engine] { return uci_engine.writeEngine("position startpos"); }},
        {"Send go wtime 100", [&uci_engine] { return uci_engine.writeEngine("go wtime 100 btime 100"); }},
        {"Read bestmove after go wtime 100 btime 100",
         [&uci_engine] {
             return uci_engine.readEngine("bestmove").code == process::Status::OK &&
                    uci_engine.bestmove() != std::nullopt;
         }},
        {"Verify info line format is valid after go wtime 100 btime 100",
         [&uci_engine] { return isValidInfoLine(uci_engine.lastInfoLine()); }},
        {"Set position to startpos moves e2e4 e7e5",
         [&uci_engine] { return uci_engine.writeEngine("position startpos moves e2e4 e7e5"); }},
        {"Send go wtime 100 btime 100", [&uci_engine] { return uci_engine.writeEngine("go wtime 100 btime 100"); }},
        {"Read bestmove after position startpos moves e2e4 e7e5",
         [&uci_engine] {
             return uci_engine.readEngine("bestmove").code == process::Status::OK &&
                    uci_engine.bestmove() != std::nullopt;
         }},
        {"Verify info line format is valid after position startpos moves e2e4 e7e5",
         [&uci_engine] { return isValidInfoLine(uci_engine.lastInfoLine()); }}};

    for (const auto& [description, action] : steps) {
        if (!executeStep(description, action)) {
            return false;
        }
    }

    std::cout << "Engine passed all compliance checks." << std::endl;

    return true;
}

}  // namespace fastchess::engine
