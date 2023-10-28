#pragma once

#include <elo.hpp>
#include <matchmaking/output/output.hpp>
#include <util/logger.hpp>

namespace fast_chess {

class Fastchess : public IOutput {
   public:
    void printInterval(const SPRT& sprt, const Stats& stats, const std::string& first,
                       const std::string& second, int current_game_count) override {
        Logger::cout("--------------------------------------------------");
        printElo(stats, first, second, current_game_count);
        printSprt(sprt, stats);
        printPenta(stats);
        Logger::cout("--------------------------------------------------");
    };

    void printElo(const Stats& stats, const std::string& first, const std::string& second,
                  std::size_t current_game_count) override {
        const Elo elo(stats.wins, stats.losses, stats.draws);

        // clang-format off
        std::stringstream ss;
        ss  << "Score of " 
            << first 
            << " vs " 
            << second 
            << ": "
            << stats.wins 
            << " - " 
            << stats.losses
            << " - " 
            << stats.draws 
            << " [] " 
            << current_game_count
            << "\n";
        
        ss  << "Elo difference: " 
            << elo.getElo()
            << ", "
            << "LOS: "
            << Elo::getLos(stats.wins, stats.losses)
            << ", "
            << "DrawRatio: "
            << Elo::getDrawRatio(stats.wins, stats.losses, stats.draws);
        // clang-format on
        Logger::cout(ss.str());
    }

    void printSprt(const SPRT& sprt, const Stats& stats) override {
        if (sprt.isValid()) {
            std::stringstream ss;

            ss << "LLR: " << std::fixed << std::setprecision(2)
               << sprt.getLLR(stats.wins, stats.draws, stats.losses) << " " << sprt.getBounds()
               << " " << sprt.getElo();
            Logger::cout(ss.str());
        }
    };

    static void printPenta(const Stats& stats) {
        std::stringstream ss;

        ss << "Ptnml:   " << std::right << std::setw(7) << "WW" << std::right << std::setw(7)
           << "WD" << std::right << std::setw(7) << "DD/WL" << std::right << std::setw(7) << "LD"
           << std::right << std::setw(7) << "LL"
           << "\n"
           << "Distr:   " << std::right << std::setw(7) << stats.penta_WW << std::right
           << std::setw(7) << stats.penta_WD << std::right << std::setw(7)
           << stats.penta_WL + stats.penta_DD << std::right << std::setw(7) << stats.penta_LD
           << std::right << std::setw(7) << stats.penta_LL;
        Logger::cout(ss.str());
    }

    void startGame(const pair_config& configs, std::size_t current_game_count,
                   std::size_t max_game_count) override {
        std::stringstream ss;

        // clang-format off
        ss << "Started game "
           << current_game_count
           << " of "
           << max_game_count
           << " ("
           << configs.first.name
           << " vs "
           << configs.second.name
           << ")";
        // clang-format on

        Logger::cout(ss.str());
    }

    void endGame(const pair_config& configs, const Stats& stats, const std::string& annotation,
                 std::size_t id) override {
        std::stringstream ss;

        // clang-format off
        ss << "Finished game "
           << id
           << " ("
           << configs.first.name
           << " vs "
           << configs.second.name
           << "): "
           << formatStats(stats)
           << " {"
           << annotation 
           << "}",
            // clang-format on

            Logger::cout(ss.str());
    }

    void endTournament() override { Logger::cout("Tournament finished"); }
};

}  // namespace fast_chess
