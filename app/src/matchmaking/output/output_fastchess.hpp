#pragma once

#include <iostream>
#include <memory>
#include <string>
#include <utility>

#include <core/config/config.hpp>
#include <core/logger/logger.hpp>
#include <engine/uci_engine.hpp>
#include <matchmaking/elo/elo_pentanomial.hpp>
#include <matchmaking/elo/elo_wdl.hpp>
#include <matchmaking/output/output.hpp>

namespace fastchess {

class Fastchess : public IOutput {
   public:
    Fastchess(bool report_penta = true) : report_penta_(report_penta) {}

    void printInterval(const SPRT& sprt, const Stats& stats, const std::string& first, const std::string& second,
                       const engines& engines, const std::string& book, ScoreBoard& scoreboard) override {
        std::cout                                                         //
            << "--------------------------------------------------\n"     //
            << printElo(stats, first, second, engines, book, scoreboard)  //
            << printSprt(sprt, stats)                                     //
            << "--------------------------------------------------\n"     //
            << std::flush;
    };

    void printResult(const Stats&, const std::string&, const std::string&) override {
        // do nothing
    }

    std::string printElo(const Stats& stats, const std::string& first, const std::string& second,
                         const engines& engines, const std::string& book, ScoreBoard& scoreboard) override {
        const auto& ecs = *config::EngineConfigs;

        if (ecs.size() == 2) {
            return printEloH2H(stats, first, second, engines, book);
        }

        const bool is_gauntlet = config::TournamentConfig && config::TournamentConfig->type == TournamentType::GAUNTLET &&
                                 !ecs.empty();
        const std::string base_engine = is_gauntlet ? ecs.front().name : "";

        std::vector<std::tuple<const EngineConfiguration*, std::unique_ptr<elo::EloBase>, Stats>> elos;

        for (auto& e : ecs) {
            const auto engine_stats = is_gauntlet && e.name != base_engine ? scoreboard.getStatsVsOpponent(e.name, base_engine)
                                                                          : scoreboard.getAllStats(e.name);
            elos.emplace_back(&e, createElo(engine_stats, report_penta_), engine_stats);
        }

        // sort by elo diff

        std::sort(elos.begin(), elos.end(), [](const auto& a, const auto& b) {
            double aElo = std::get<1>(a)->diff();
            double bElo = std::get<1>(b)->diff();
            return (!std::isnan(aElo) && std::isnan(bElo)) || aElo > bElo;
        });

        int rank = 0;

        size_t max_name_length = 25;
        for (const auto& [ec, elo, stats] : elos) {
            max_name_length = std::max(max_name_length, ec->name.length());
        }

        std::string out =
            fmt::format("{:<4} {:<{}} {:>10} {:>10} {:>10} {:>10} {:>10} {:>10} {:>10} {:>20}\n", "Rank", "Name",
                        max_name_length, "Elo", "+/-", "nElo", "+/-", "Games", "Score", "Draw", "Ptnml(0-2)");

        for (const auto& [ec, elo, stats] : elos) {
            out += fmt::format(
                "{:>4} {:<{}} {:>10.2f} {:>10.2f} {:>10.2f} {:>10.2f} {:>10} {:>9.1f}% {:>9.1f}% {:>20}\n", ++rank,
                ec->name, max_name_length, elo->diff(), elo->error(), elo->nEloDiff(), elo->nEloError(), stats.sum(),
                stats.pointsRatio(), report_penta_ ? stats.drawRatioPenta() : stats.drawRatio(),
                report_penta_ ? formatPentaStats(stats) : "");
        }

        return out;
    }

    std::string printSprt(const SPRT& sprt, const Stats& stats) override {
        if (sprt.isEnabled()) {
            double llr = sprt.getLLR(stats, report_penta_);

            return fmt::format("LLR: {:.2f} ({:.1f}%) {} {}\n", llr, sprt.getFraction(llr) * 100., sprt.getBounds(),
                               sprt.getElo());
        }

        return "";
    }

    void startGame(const GamePair<EngineConfiguration, EngineConfiguration>& configs, std::size_t current_game_count,
                   std::size_t max_game_count) override {
        std::cout << fmt::format("Started game {} of {} ({} vs {})\n", current_game_count, max_game_count,
                                 configs.white.name, configs.black.name)
                  << std::flush;
    }

    void endGame(const GamePair<EngineConfiguration, EngineConfiguration>& configs, const Stats& stats,
                 const std::string& annotation, std::size_t id) override {
        std::cout << fmt::format("Finished game {} ({} vs {}): {} {{{}}}\n", id, configs.white.name, configs.black.name,
                                 formatStats(stats), annotation)
                  << std::flush;
    }

    void endTournament(std::string_view terminationMessage = "") override {
        if (!terminationMessage.empty()) {
            std::cout << terminationMessage << std::endl;
        } else {
            std::cout << "Tournament finished" << std::endl;
        }
    }

    OutputType getType() const override { return OutputType::FASTCHESS; }

   private:
    std::string printEloH2H(const Stats& stats, const std::string& first, const std::string& second,
                            const engines& engines, const std::string& book) {
        auto elo = createElo(stats, report_penta_);

        const auto& first_engine  = engines.first.getConfig().name == first ? engines.first : engines.second;
        const auto& second_engine = engines.first.getConfig().name == second ? engines.first : engines.second;

        const auto tc       = formatTimeControl(first_engine.getConfig(), second_engine.getConfig());
        const auto threads  = formatThreads(first_engine.uciOptions(), second_engine.uciOptions());
        const auto hash     = formatHash(first_engine.uciOptions(), second_engine.uciOptions());
        const auto bookname = getShortName(book);

        auto result = fmt::format("{}\n{}\n{}\n{}", formatMatchup(first, second, tc, threads, hash, bookname),
                                  formatElo(elo), formatGameStats(*elo, stats, stats.pairsRatio()),
                                  formatGameResults(stats, stats.points(), stats.pointsRatio()));

        if (report_penta_) {
            result += fmt::format("\nPtnml(0-2): {}, {}", formatPentaStats(stats), formatWLDDRatio(stats));
        }

        return fmt::format("{}\n", result);
    }

    std::string getTime(const EngineConfiguration& config) const {
        const auto& limit = config.limit;

        if (limit.tc.time + limit.tc.increment > 0) {
            auto moves     = limit.tc.moves > 0 ? fmt::format("{}/", limit.tc.moves) : "";
            auto time      = limit.tc.time / 1000.0;
            auto increment = limit.tc.increment > 0 ? fmt::format("+{:.2g}", limit.tc.increment / 1000.0) : "";

            return fmt::format("{}{}{}", moves, time, increment);
        } else if (limit.tc.fixed_time > 0) {
            auto time = limit.tc.fixed_time / 1000.0;

            return fmt::format("{}/move", time);
        } else if (limit.plies > 0) {
            return fmt::format("{} plies", limit.plies);
        } else if (limit.nodes > 0) {
            return fmt::format("{} nodes", limit.nodes);
        }

        return "";
    }

    std::string getThreads(const UCIOptions& uci_options) const {
        const auto option = uci_options.getOption("Threads");

        if (option.has_value()) {
            auto value = option.value()->getValue();
            return fmt::format("{}t", value);
        }

        return fmt::format("NULL");
    }

    std::string getHash(const UCIOptions& uci_options) const {
        const auto option = uci_options.getOption("Hash");

        if (option.has_value()) {
            auto value = option.value()->getValue();
            return fmt::format("{}MB", value);
        }

        return fmt::format("NULL");
    }

    static std::string getShortName(const std::string& name) {
        std::size_t pos = name.find_last_of("/\\");

        if (pos != std::string::npos) {
            return name.substr(pos + 1);
        }

        return name;
    }

    std::string formatMatchup(const std::string& first, const std::string& second, const std::string& tc,
                              const std::string& threads, const std::string& hash, const std::string& bookname) const {
        return fmt::format("Results of {} vs {} ({}, {}, {}{}):", first, second, tc, threads, hash,
                           bookname.empty() ? "" : fmt::format(", {}", bookname));
    }

    std::string formatElo(const std::unique_ptr<elo::EloBase>& elo) const {
        return fmt::format("Elo: {}, nElo: {}", elo->getElo(), elo->nElo());
    }

    std::string formatGameStats(const elo::EloBase& elo, const Stats& stats, double pairsRatio) const {
        return fmt::format("LOS: {}, DrawRatio: {:.2f} %{}", elo.los(),
                           report_penta_ ? stats.drawRatioPenta() : stats.drawRatio(),
                           report_penta_ ? fmt::format(", PairsRatio: {:.2f}", pairsRatio) : "");
    }

    std::string formatGameResults(const Stats& stats, double points, double pointsRatio) const {
        return fmt::format("Games: {}, Wins: {}, Losses: {}, Draws: {}, Points: {:.1f} ({:.2f} %)",
                           stats.wins + stats.losses + stats.draws, stats.wins, stats.losses, stats.draws, points,
                           pointsRatio);
    }

    std::string formatPentaStats(const Stats& stats) const {
        return fmt::format("[{}, {}, {}, {}, {}]", stats.penta_LL, stats.penta_LD, stats.penta_WL + stats.penta_DD,
                           stats.penta_WD, stats.penta_WW);
    }

    std::string formatWLDDRatio(const Stats& stats) const {
        return fmt::format("WL/DD Ratio: {:.2f}", stats.wl_dd_Ratio());
    }

    std::string formatTimeControl(const EngineConfiguration first_config,
                                  const EngineConfiguration seocnd_config) const {
        const auto timeFirst  = getTime(first_config);
        const auto timeSecond = getTime(seocnd_config);
        return timeFirst == timeSecond ? fmt::format("{}", timeFirst) : fmt::format("{} - {}", timeFirst, timeSecond);
    }

    std::string formatThreads(const UCIOptions& first_uci_options, const UCIOptions& second_uci_options) const {
        const auto threadsFirst  = getThreads(first_uci_options);
        const auto threadsSecond = getThreads(second_uci_options);
        return threadsFirst == threadsSecond ? fmt::format("{}", threadsFirst)
                                             : fmt::format("{} - {}", threadsFirst, threadsSecond);
    }

    std::string formatHash(const UCIOptions& first_uci_options, const UCIOptions& second_uci_options) const {
        const auto hashFirst  = getHash(first_uci_options);
        const auto hashSecond = getHash(second_uci_options);
        return hashFirst == hashSecond ? fmt::format("{}", hashFirst) : fmt::format("{} - {}", hashFirst, hashSecond);
    }

    std::unique_ptr<elo::EloBase> createElo(const Stats& stats, bool report_penta) const {
        if (report_penta) {
            return std::make_unique<elo::EloPentanomial>(stats);
        }

        return std::make_unique<elo::EloWDL>(stats);
    }

    bool report_penta_;
};

}  // namespace fastchess
