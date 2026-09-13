#include <matchmaking/tournament/roundrobin/roundrobin.hpp>

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

#include <core/logger/logger.hpp>
#include <core/memory/scope_guard.hpp>
#include <core/rand.hpp>
#include <game/pgn/pgn_builder.hpp>
#include <matchmaking/output/output_factory.hpp>
#include <matchmaking/tournament/roundrobin/scheduler.hpp>

namespace fastchess {

RoundRobin::RoundRobin(const stats_map& results) : BaseTournament(results) {
    sprt_ = SPRT(config::TournamentConfig->sprt.alpha, config::TournamentConfig->sprt.beta,
                 config::TournamentConfig->sprt.elo0, config::TournamentConfig->sprt.elo1,
                 config::TournamentConfig->sprt.model, config::TournamentConfig->sprt.enabled);

    const auto& config     = *config::TournamentConfig;
    const auto num_players = config::EngineConfigs->size();

    generator_ =
        std::make_unique<RoundRobinScheduler>(book_.get(), num_players, config.rounds, config.games, match_count_);
}

void RoundRobin::start() {
    LOG_TRACE("Starting round robin tournament...");

    BaseTournament::start();

    // If autosave is enabled, save the results every save_interval games
    const auto save_interval = config::TournamentConfig->autosaveinterval;

    // Account for the initial matchcount
    auto save_iter = initial_matchcount_ + save_interval;

    // Wait for games to finish
    while (match_count_ < final_matchcount_ && !atomic::stop) {
        if (save_interval > 0 && match_count_ >= save_iter) {
            saveJson();
            save_iter += save_interval;
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void RoundRobin::startNext() {
    std::lock_guard<std::mutex> lock(game_gen_mutex_);

    if (atomic::stop) return;

    auto match = generator_->next();

    if (!match) {
        LOG_TRACE("No more matches to generate");
        return;
    }

    pool_.enqueue([this, match]() {
        try {
            this->createMatch(match.value());
        } catch (const std::exception& e) {
            Logger::print<Logger::Level::ERR>("Error while creating match: {}", e.what());
            atomic::stop                 = true;
            atomic::abnormal_termination = true;
        }
        LOG_TRACE_THREAD("Thread {} finished match {}", std::this_thread::get_id(), match->game_id);
    });
}

void RoundRobin::createMatch(const Scheduler::Pairing& pairing) {
    const auto opening = (*book_)[pairing.opening_id];
    const auto first   = (*config::EngineConfigs)[pairing.player1];
    const auto second  = (*config::EngineConfigs)[pairing.player2];

    GamePair<EngineConfiguration, EngineConfiguration> configs = {first, second};

    if (pairing.game_id % 2 == 0 && !config::TournamentConfig->noswap) {
        std::swap(configs.white, configs.black);
    }

    if (config::TournamentConfig->reverse) {
        std::swap(configs.white, configs.black);
    }

    // callback functions, do not capture by reference
    const auto start = [this, configs, pairing]() {
        std::lock_guard<std::mutex> lock(output_mutex_);

        output_->startGame(configs, pairing.game_id, final_matchcount_);
    };

    // callback functions, do not capture by reference
    const auto finish = [this, configs, first, second, pairing](const Stats& stats, const std::string& reason,
                                                                const engines& engines) {
        const auto& cfg = *config::TournamentConfig;

        // lock to avoid chaotic output, i.e.
        // Finished game 187 (Engine1 vs Engine2): 0-1 {White loses on time}
        // Finished game 186 (Engine2 vs Engine1): 0-1 {White loses on time}
        // Score of Engine1 vs Engine2: 95 - 92 - 0  [0.508] 187
        // Score of Engine1 vs Engine2: 94 - 92 - 0  [0.505] 186
        std::lock_guard<std::mutex> lock(output_mutex_);

        output_->endGame(configs, stats, reason, pairing.game_id);

        if (cfg.report_penta) {
            scoreboard_.updatePair(configs, stats, pairing.pairing_id);
        } else {
            scoreboard_.updateNonPair(configs, stats);
        }

        const auto updated_stats = scoreboard_.getStats(first.name, second.name);

        if (shouldPrintScoreInterval() || allMatchesPlayed()) {
            output_->printResult(updated_stats, first.name, second.name);
        }

        if ((shouldPrintRatingInterval(pairing.pairing_id) && scoreboard_.isPairCompleted(pairing.pairing_id)) ||
            allMatchesPlayed()) {
            output_->printInterval(sprt_, updated_stats, first.name, second.name, engines, cfg.opening.file,
                                   scoreboard_);
        }

        if (csv_report_ && ((shouldWriteCsvInterval(pairing.pairing_id) &&
                             scoreboard_.isPairCompleted(pairing.pairing_id)) ||
                            allMatchesPlayed())) {
            csv_report_->update(sprt_, updated_stats);
        }

        updateSprtStatus({first, second}, engines);

        match_count_++;
    };

    playGame(configs, start, finish, opening, pairing.pairing_id, pairing.game_id);

    if (config::TournamentConfig->wait > 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(config::TournamentConfig->wait));
}

void RoundRobin::updateSprtStatus(const std::vector<EngineConfiguration>& engine_configs, const engines& engines) {
    if (!sprt_.isEnabled()) return;

    const auto stats = scoreboard_.getStats(engine_configs[0].name, engine_configs[1].name);
    const auto llr   = sprt_.getLLR(stats, config::TournamentConfig->report_penta);

    const auto sprtResult = sprt_.getResult(llr);

    if (sprtResult != SPRT_CONTINUE || match_count_ == final_matchcount_) {
        LOG_TRACE("SPRT test finished, stopping tournament.");
        atomic::stop = true;

        const std::string terminationMessage =
            fmt::format("SPRT ({}) completed - {} was accepted", sprt_.getElo(), sprtResult == SPRT_H0 ? "H0" : "H1");

        Logger::info("{}", terminationMessage);

        output_->printResult(stats, engine_configs[0].name, engine_configs[1].name);
        output_->printInterval(sprt_, stats, engine_configs[0].name, engine_configs[1].name, engines,
                               config::TournamentConfig->opening.file, scoreboard_);
        output_->endTournament(terminationMessage);

        // record the point which crossed the bound, whatever the csv interval is
        if (csv_report_ && !csv_final_written_.exchange(true)) {
            csv_report_->update(sprt_, stats);
        }
    }
}

bool RoundRobin::shouldPrintRatingInterval(std::size_t round_id) const noexcept {
    const auto& cfg = *config::TournamentConfig;

    // round_id and match_count_ starts 0 so we add 1
    const auto ratinginterval_index = cfg.report_penta ? round_id + 1 : match_count_ + 1;

    return ratinginterval_index % cfg.ratinginterval == 0;
}

bool RoundRobin::shouldWriteCsvInterval(std::size_t round_id) const noexcept {
    const auto& cfg = *config::TournamentConfig;

    // round_id and match_count_ starts 0 so we add 1
    const auto csvinterval_index = cfg.report_penta ? round_id + 1 : match_count_ + 1;
    const auto interval          = cfg.csv.interval > 0 ? cfg.csv.interval : 1;

    return csvinterval_index % interval == 0;
}

bool RoundRobin::shouldPrintScoreInterval() const noexcept {
    const auto& cfg = *config::TournamentConfig;

    const auto scoreinterval_index = match_count_ + 1;

    return scoreinterval_index % cfg.scoreinterval == 0;
}

}  // namespace fastchess
