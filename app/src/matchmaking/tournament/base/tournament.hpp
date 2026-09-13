#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <affinity/affinity_manager.hpp>
#include <core/filesystem/file_writer.hpp>
#include <core/globals/globals.hpp>
#include <core/logger/logger.hpp>
#include <core/memory/cache.hpp>
#include <core/threading/threadpool.hpp>
#include <engine/uci_engine.hpp>
#include <game/book/opening_book.hpp>
#include <matchmaking/game_pair.hpp>
#include <matchmaking/output/csv_report.hpp>
#include <matchmaking/output/output.hpp>
#include <matchmaking/scoreboard.hpp>
#include <matchmaking/timeout_tracker.hpp>
#include <matchmaking/tournament/schedule/scheduler.hpp>
#include <types/tournament.hpp>

namespace fastchess {

class BaseTournament {
    using EngineCache = util::CachePool<engine::UciEngine, std::string>;

   public:
    BaseTournament(const stats_map& results);
    virtual ~BaseTournament();

    // Starts the tournament
    virtual void start();

    [[nodiscard]] stats_map getResults() noexcept { return scoreboard_.getResults(); }

   protected:
    using start_fn  = std::function<void()>;
    using finish_fn = std::function<void(const Stats& stats, const std::string& reason, const engines&)>;

    EngineCache& getEngineCache();

    // Gets called after a game has finished
    virtual void startNext() = 0;

    // creates the matches
    void create();

    // Function to save the config file
    void saveJson();

    // play one game and write it to the pgn file
    void playGame(const GamePair<EngineConfiguration, EngineConfiguration>& configs, const start_fn& start,
                  const finish_fn& finish, const book::Opening& opening, std::size_t round_id, std::size_t game_id);

    // We keep engines alive after they were used to avoid the overhead of starting them again.
    ScoreBoard scoreboard_ = {};
    PlayerTracker tracker_ = {};
    util::ThreadPool pool_ = util::ThreadPool{1};

    std::unique_ptr<Scheduler> generator_;
    std::unique_ptr<IOutput> output_;
    std::unique_ptr<affinity::AffinityManager> cores_;
    std::unique_ptr<util::FileWriter> file_writer_pgn_;
    std::unique_ptr<util::FileWriter> file_writer_epd_;
    std::unique_ptr<CsvReport> csv_report_;
    std::unique_ptr<book::OpeningBook> book_;

    std::atomic<std::uint64_t> match_count_      = 0;
    std::uint64_t initial_matchcount_            = 0;
    std::atomic<std::uint64_t> final_matchcount_ = 0;

    // Caches for non-affinity mode (shared by all threads)
    std::unique_ptr<EngineCache> global_cache_;

    // Per-Thread cache for affinity mode
    std::unordered_map<std::thread::id, std::unique_ptr<EngineCache>> thread_caches_;

    // Only need mutex for managing the cache containers, not the EngineCache itself
    std::mutex cache_management_mutex_;

   private:
    [[nodiscard]] std::size_t setResults(const stats_map& results);

    [[nodiscard]] int getMaxAffinity(const std::vector<EngineConfiguration>& configs) const noexcept;

    void restartEngine(std::unique_ptr<engine::UciEngine>& engine);
};

}  // namespace fastchess
