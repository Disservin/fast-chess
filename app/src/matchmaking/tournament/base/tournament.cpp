#include <matchmaking/tournament/base/tournament.hpp>

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <affinity/affinity_manager.hpp>
#include <core/filesystem/file_writer.hpp>
#include <core/globals/globals.hpp>
#include <core/logger/logger.hpp>
#include <core/memory/cache.hpp>
#include <core/threading/threadpool.hpp>
#include <engine/uci_engine.hpp>
#include <game/book/opening_book.hpp>
#include <game/epd/epd_builder.hpp>
#include <game/pgn/pgn_builder.hpp>
#include <matchmaking/match/match.hpp>
#include <matchmaking/output/output.hpp>
#include <matchmaking/output/output_factory.hpp>
#include <matchmaking/scoreboard.hpp>
#include <matchmaking/tournament/schedule/scheduler.hpp>
#include <types/tournament.hpp>

namespace fastchess {

BaseTournament::BaseTournament(const stats_map& results) {
    const auto& config = *config::TournamentConfig;
    const auto total   = setResults(results);

    initial_matchcount_ = total;
    match_count_        = total;

    output_ = OutputFactory::create(config.output, config.report_penta);
    cores_  = std::make_unique<affinity::AffinityManager>(config.affinity, config.affinity_cpus,
                                                          getMaxAffinity(*config::EngineConfigs));
    book_   = std::make_unique<book::OpeningBook>(config, initial_matchcount_);

    auto resolveAppendFlag = [total](bool appendFlag, const char* flagName) {
        if (appendFlag || !total) return appendFlag;

        Logger::print<Logger::Level::INFO>("Resuming from {} games, ignoring {} append=false.", total, flagName);
        return true;
    };

    if (!config.pgn.file.empty()) {
        const bool append = resolveAppendFlag(config.pgn.append_file, "-pgnout");
        file_writer_pgn_  = std::make_unique<util::FileWriter>(config.pgn.file, append, config.pgn.crc);
    }

    if (!config.epd.file.empty()) {
        const bool append = resolveAppendFlag(config.epd.append_file, "-epdout");
        file_writer_epd_  = std::make_unique<util::FileWriter>(config.epd.file, append);
    }

    pool_.resize(config.concurrency);
}

BaseTournament::~BaseTournament() {
    LOG_TRACE("~BaseTournament()");

    LOG_TRACE("Instructing engines to stop...");

    writeToOpenPipes();

    pool_.kill();

    if (config::TournamentConfig->output == OutputType::FASTCHESS) {
        if (tracker_.begin() != tracker_.end()) {
            Logger::print<Logger::Level::INFO>("");
        }

        for (const auto& [name, tracked] : tracker_) {
            Logger::print<Logger::Level::INFO>("Player: {}", name);
            Logger::print<Logger::Level::INFO>("  Timeouts: {}", tracked.timeouts);
            Logger::print<Logger::Level::INFO>("  Crashed: {}", tracked.disconnects);
        }

        if (tracker_.begin() != tracker_.end()) {
            Logger::print<Logger::Level::INFO>("");
        }
    }

    // loop over the individual engine caches and enqueue a terminate to the pool
    // to ensure all engines are properly shutdown, without having to wait sequentially
    std::lock_guard<std::mutex> lock(cache_management_mutex_);
    std::vector<engine::UciEngine*> engines_to_terminate;

    static constexpr auto error = "Unexpected; Engine still in use during tournament shutdown, please raise an issue.";

    const auto appendToList = [&engines_to_terminate](auto& it) {
        if (it->isAvailable() == false) {
            Logger::print<Logger::Level::WARN>(error);
            return;
        }

        engines_to_terminate.push_back(it->get().get());
    };

    if (config::TournamentConfig->affinity)
        for (const auto& [_, cache] : thread_caches_)  //
            cache->loopEntries(appendToList);
    else if (global_cache_)
        global_cache_->loopEntries(appendToList);

    // parallel terminate engines. might be more than concurrency but doesn't matter
    // since they arent doing much
    std::vector<std::thread> threads;

    for (auto* engine : engines_to_terminate) {
        threads.emplace_back([engine]() {
            engine->quit();
            LOG_TRACE_THREAD("Terminating engine {}", engine->getConfig().name);
            engine->terminate();  // uses a timeout
        });
    }

    for (auto& t : threads)
        if (t.joinable()) t.join();

    saveJson();
}

void BaseTournament::start() {
    LOG_TRACE("Starting tournament...");

    create();
}

BaseTournament::EngineCache& BaseTournament::getEngineCache() {
    if (config::TournamentConfig->affinity) {
        // Use per-thread cache
        std::thread::id tid = std::this_thread::get_id();
        std::lock_guard<std::mutex> lock(cache_management_mutex_);

        auto it = thread_caches_.find(tid);

        if (it == thread_caches_.end()) {
            thread_caches_[tid] = std::make_unique<EngineCache>();
            return *thread_caches_[tid];
        }

        return *it->second;

    } else {
        // Use single shared cache
        std::lock_guard<std::mutex> lock(cache_management_mutex_);
        if (!global_cache_) global_cache_ = std::make_unique<EngineCache>();
        return *global_cache_;
    }
}

void BaseTournament::create() {
    LOG_TRACE("Creating matches...");

    final_matchcount_ = generator_->total();

    for (int i = 0; i < pool_.getNumThreads(); i++) {
        startNext();
    }
}

void BaseTournament::saveJson() {
    LOG_TRACE("Saving results...");

    const auto& config = *config::TournamentConfig;

    const auto merged_results = [this]() {
        stats_map map;

        std::vector<PlayerPairKey> engine_names;

        for (const auto& engine : *config::EngineConfigs) {
            const auto engine_name = engine.name;

            for (const auto& opponent : *config::EngineConfigs) {
                const auto opponent_name = opponent.name;

                if (engine_name == opponent_name) continue;

                const auto check = [&engine_name, &opponent_name](const auto& pair) {
                    return PlayerPairKey(engine_name, opponent_name) == pair ||
                           PlayerPairKey(opponent_name, engine_name) == pair;
                };

                if (std::find_if(engine_names.begin(), engine_names.end(), check) != engine_names.end()) {
                    continue;
                }

                const auto stats                  = scoreboard_.getStats(engine_name, opponent_name);
                map[{engine_name, opponent_name}] = stats;
                engine_names.emplace_back(engine_name, opponent_name);
            }
        }

        return map;
    };

    nlohmann::ordered_json jsonfile = config;
    jsonfile["engines"]             = *config::EngineConfigs;
    jsonfile["stats"]               = merged_results();

    std::ofstream file(config.config_name);
    file << std::setw(4) << jsonfile << std::endl;

    LOG_TRACE("Saved results to.");
}

void BaseTournament::playGame(const GamePair<EngineConfiguration, EngineConfiguration>& engine_configs,
                              const start_fn& start, const finish_fn& finish, const book::Opening& opening,
                              std::size_t round_id, std::size_t game_id) {
    const auto& config = *config::TournamentConfig;
    const auto rl      = config.log.realtime;

    // ideally this should be also tied to the lifetime of the tournament
    thread_local static std::optional<std::vector<int>> cpus;

    if (cpus == std::nullopt && config.affinity) {
        cpus = cores_->consume().cpus;
        /*
        CPU Affinity Implementation Strategy:

        Previously, CPU affinity was set on engine processes after they were already running.
        This approach failed on Linux because:
        1. Chess engines typically spawn worker threads during initialization
        2. These worker threads were created before affinity was applied
        3. Child threads don't inherit affinity from parent processes retroactively
        4. This resulted in engines running on incorrect CPU cores

        Current approach:
        - Set CPU affinity on the managing thread BEFORE starting any engines
        - On linux, this ensures all subsequently spawned engine processes inherit the correct affinity
        - Engine caching strategy depends on affinity usage:
        * With affinity: thread-local engine cache (engines stick to assigned cores)
        * Without affinity: global engine cache (engines shared across all threads)
        - Additionally, explicitly set engine process affinity as part of starting the match. This ensures
          that on Windows the affinity of the engine process (and all threads it contains) is set correctly.
          Note that due to the caching behavior, this is only done once (for each engine) per tournament thread.
        */
        const bool success = affinity::setThreadAffinity(*cpus, affinity::getThreadHandle());
        if (!success) {
            Logger::print<Logger::Level::WARN>("Warning; Failed to set CPU affinity for the tournament thread.");
        }
    }

    const auto white_name = engine_configs.white.name;
    const auto black_name = engine_configs.black.name;

    auto& engine_cache_ = getEngineCache();

    auto white_engine = engine_cache_.getEntry(white_name, engine_configs.white, rl);
    auto black_engine = engine_cache_.getEntry(black_name, engine_configs.black, rl);

    util::ScopeGuard lock1((*white_engine)->getConfig().restart ? nullptr : &(*white_engine));
    util::ScopeGuard lock2((*black_engine)->getConfig().restart ? nullptr : &(*black_engine));

    if (atomic::stop.load()) return;

    LOG_TRACE_THREAD("Game {} between {} and {} starting", game_id, white_name, black_name);

    start();

    auto match = Match(opening);
    match.start(*white_engine->get(), *black_engine->get(), cpus);

    LOG_TRACE_THREAD("Game {} between {} and {} finished", game_id, white_name, black_name);

    if (match.isStallOrDisconnect()) {
        LOG_WARN_THREAD("Game {} between {} and {} stalled / disconnected", game_id, white_name, black_name);

        if (!config.recover) {
            if (!atomic::stop.exchange(true)) {
                Logger::print<Logger::Level::WARN>(
                    "Game {} stalled / disconnected and no recover option set for engine, stopping tournament.",
                    game_id);

                const auto match_data = match.get();

                finish({match_data}, match_data.reason, {*white_engine->get(), *black_engine->get()});

                atomic::abnormal_termination = true;
            }
            return;
        }

        // restart the engine when recover is enabled

        if ((*white_engine)->isready().code != engine::process::Status::OK) {
            restartEngine(white_engine->get());
        }
        if ((*black_engine)->isready().code != engine::process::Status::OK) {
            restartEngine(black_engine->get());
        }
    }

    const auto match_data = match.get();

    // If the game was interrupted(didn't completely finish)
    if (match_data.termination != MatchTermination::INTERRUPT && !atomic::stop) {
        if (!config.pgn.file.empty()) {
            const auto pgn_str = pgn::PgnBuilder(config.pgn, match_data, round_id + 1).get();
            file_writer_pgn_->write(pgn_str);
        }

        if (!config.epd.file.empty()) {
            file_writer_epd_->write(epd::EpdBuilder(config.variant, match_data).get());
        }

        const auto result = pgn::PgnBuilder::getResultFromWhiteMatch(match_data.players.white);
        LOG_TRACE_THREAD("Game {} finished with result {}", game_id, result);

        finish({match_data}, match_data.reason, {*white_engine->get(), *black_engine->get()});

        startNext();
    }

    // remove engines if restart is enabled, frees up memory
    if (white_engine->get()->getConfig().restart) engine_cache_.deleteFromCache(white_engine);
    if (black_engine->get()->getConfig().restart) engine_cache_.deleteFromCache(black_engine);

    const auto losing_player = match_data.getLosingPlayer();
    if (!losing_player.has_value()) return;

    switch (match_data.termination) {
        case MatchTermination::TIMEOUT:
            tracker_.report_timeout(losing_player->config.name);
            break;
        case MatchTermination::DISCONNECT:
        case MatchTermination::STALL:
            tracker_.report_disconnect(losing_player->config.name);
            break;
        default:
            break;
    }
}

int BaseTournament::getMaxAffinity(const std::vector<EngineConfiguration>& configs) const noexcept {
    constexpr auto transform = [](const auto& val) { return std::stoi(val); };
    const auto first_threads = configs[0].getOption<int>("Threads", transform).value_or(1);

    for (const auto& config : configs) {
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

void BaseTournament::restartEngine(std::unique_ptr<engine::UciEngine>& engine) {
    LOG_TRACE_THREAD("Restarting engine {}", engine->getConfig().name);
    auto config = engine->getConfig();
    auto rl     = engine->isRealtimeLogging();
    engine      = std::make_unique<engine::UciEngine>(config, rl);
}

std::size_t BaseTournament::setResults(const stats_map& results) {
    LOG_TRACE("Setting results...");

    scoreboard_.setResults(results);

    std::size_t total = 0;

    for (const auto& pair1 : scoreboard_.getResults()) {
        const auto& stats = pair1.second;

        total += stats.wins + stats.losses + stats.draws;
    }

    return total;
}

}  // namespace fastchess
