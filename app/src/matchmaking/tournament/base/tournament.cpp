#include <matchmaking/tournament/base/tournament.hpp>

#include <affinity/affinity_manager.hpp>
#include <book/opening_book.hpp>
#include <engine/uci_engine.hpp>
#include <epd/epd_builder.hpp>
#include <matchmaking/match/match.hpp>
#include <matchmaking/output/output.hpp>
#include <matchmaking/output/output_factory.hpp>
#include <matchmaking/scoreboard.hpp>
#include <pgn/pgn_builder.hpp>
#include <types/tournament.hpp>
#include <util/cache.hpp>
#include <util/file_writer.hpp>
#include <util/logger/logger.hpp>
#include <util/threadpool.hpp>

namespace fastchess {

BaseTournament::BaseTournament(const stats_map &results) {
    const auto &config = config::TournamentConfig.get();

    output_ = OutputFactory::create(config.output, config.report_penta);
    cores_  = std::make_unique<affinity::AffinityManager>(config.affinity, getMaxAffinity(config::EngineConfigs.get()));

    if (!config.pgn.file.empty()) file_writer_pgn = std::make_unique<util::FileWriter>(config.pgn.file);
    if (!config.epd.file.empty()) file_writer_epd = std::make_unique<util::FileWriter>(config.epd.file);

    pool_.resize(config.concurrency);

    setResults(results);

    book_ = std::make_unique<book::OpeningBook>(config, initial_matchcount_);
}

void BaseTournament::start() {
    Logger::trace("Starting tournament...");

    create();
}

void BaseTournament::saveJson() {
    Logger::trace("Saving results...");

    const auto &config = config::TournamentConfig.get();

    nlohmann::ordered_json jsonfile = config;
    jsonfile["engines"]             = config::EngineConfigs.get();
    jsonfile["stats"]               = getResults();

    auto filename = config.config_name.empty() ? "config.json" : config.config_name;

    std::ofstream file(filename);
    file << std::setw(4) << jsonfile << std::endl;

    Logger::trace("Saved results.");
}

void BaseTournament::playGame(const GamePair<EngineConfiguration, EngineConfiguration> &engine_configs,
                              start_callback start, finished_callback finish, const pgn::Opening &opening,
                              std::size_t round_id, std::size_t game_id) {
    if (atomic::stop) return;

    const auto &config = config::TournamentConfig.get();
    const auto core    = util::ScopeGuard(cores_->consume());

    const auto white_name = engine_configs.white.name;
    const auto black_name = engine_configs.black.name;

    auto &white_engine = engine_cache_.getEntry(white_name, engine_configs.white, config.log.realtime);
    auto &black_engine = engine_cache_.getEntry(black_name, engine_configs.black, config.log.realtime);

    util::ScopeGuard lock1(white_engine);
    util::ScopeGuard lock2(black_engine);

    Logger::trace<true>("Game {} between {} and {} starting", game_id, white_name, black_name);

    start();

    auto match = Match(opening);
    match.start(white_engine.get(), black_engine.get(), core.get().cpus);

    Logger::trace<true>("Game {} between {} and {} finished", game_id, white_name, black_name);

    if (match.isStallOrDisconnect()) {
        Logger::trace<true>("Game {} between {} and {} stalled / disconnected", game_id, white_name, black_name);
        if (!config.recover) {
            atomic::stop = true;
            return;
        }

        // restart the engine when recover is enabled

        Logger::trace<true>("Restarting engine...");
        if (white_engine.get().isready() != engine::process::Status::OK) {
            Logger::trace<true>("Restarting engine {}", white_name);
            white_engine.get().refreshUci();
        }

        if (black_engine.get().isready() != engine::process::Status::OK) {
            Logger::trace<true>("Restarting engine {}", black_name);
            black_engine.get().refreshUci();
        }
    }

    const auto match_data = match.get();

    // If the game was interrupted(didn't completely finish)
    if (match_data.termination != MatchTermination::INTERRUPT && !atomic::stop) {
        if (!config.pgn.file.empty()) {
            file_writer_pgn->write(pgn::PgnBuilder(config.pgn, match_data, round_id + 1).get());
        }

        if (!config.epd.file.empty()) {
            file_writer_epd->write(epd::EpdBuilder(config.variant, match_data).get());
        }

        const auto result = pgn::PgnBuilder::getResultFromMatch(match_data.players.white, match_data.players.black);
        Logger::trace<true>("Game {} finished with result {}", game_id, result);

        finish({match_data}, match_data.reason, {white_engine.get(), black_engine.get()});
    }
}

int BaseTournament::getMaxAffinity(const std::vector<EngineConfiguration> &configs) const noexcept {
    constexpr auto transform = [](const auto &val) { return std::stoi(val); };
    const auto first_threads = configs[0].getOption<int>("Threads", transform).value_or(1);

    for (const auto &config : configs) {
        const auto threads = config.getOption<int>("Threads", transform).value_or(1);

        // thread count in all configs has to be the same for affinity to work,
        // otherwise we set it to 0 and affinity is disabled
        // if (threads != first_threads) {
        //     return 0;
        // }

        // only enable for 1 thread currently
        if (threads != 1) {
            return 0;
        }
    }

    return first_threads;
}

}  // namespace fastchess
