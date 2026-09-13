#pragma once

#include <atomic>
#include <mutex>
#include <vector>

#include <affinity/affinity_manager.hpp>
#include <core/filesystem/file_writer.hpp>
#include <core/globals/globals.hpp>
#include <core/memory/cache.hpp>
#include <core/rand.hpp>
#include <core/threading/threadpool.hpp>
#include <game/book/opening_book.hpp>
#include <matchmaking/match/match.hpp>
#include <matchmaking/scoreboard.hpp>
#include <matchmaking/sprt/sprt.hpp>
#include <matchmaking/stats.hpp>
#include <types/tournament.hpp>

#include <matchmaking/tournament/base/tournament.hpp>

namespace fastchess {

class RoundRobin : public BaseTournament {
   public:
    explicit RoundRobin(const stats_map& results);

    // starts the round robin
    void start() override;

   protected:
    void startNext() override;

   private:
    bool shouldPrintRatingInterval(std::size_t round_id) const noexcept;

    // print score result based on scoreinterval if output format is cutechess
    bool shouldPrintScoreInterval() const noexcept;

    // append a row to the csv report based on its own interval
    bool shouldWriteCsvInterval(std::size_t round_id) const noexcept;

    bool allMatchesPlayed() const noexcept { return match_count_ + 1 == final_matchcount_; }

    void createMatch(const Scheduler::Pairing& pairing);

    // update the current running sprt. SPRT Config has to be valid.
    void updateSprtStatus(const std::vector<EngineConfiguration>& engine_configs, const engines& engines);

    SPRT sprt_ = SPRT();

    std::atomic<bool> csv_final_written_ = false;

    std::mutex output_mutex_;
    std::mutex game_gen_mutex_;
};
}  // namespace fastchess
