#pragma once

#include <atomic>
#include <fstream>
#include <utility>
#include <vector>
#include <string_view>

#include "chess/board.hpp"
#include "engines/engine_config.hpp"
#include "engines/uci_engine.hpp"
#include "options.hpp"
#include "sprt.hpp"
#include "threadpool.hpp"
#include "tournament_data.hpp"

namespace fast_chess
{

/*
 * This is the main class to start engines matches.
 * Generally we always swap the engine order, the first engine is always
 * the the positive engine, meaning all stats should be calculated from that view point.
 */
class Tournament
{
  public:
    // For testing purposes
    explicit Tournament(bool saveTime) : save_time_header_(saveTime)
    {
        file_.open("fast-chess.pgn", std::ios::app);
    };

    explicit Tournament(const CMD::GameManagerOptions &mc);

    template <typename T>
    static T findElement(const std::vector<std::string> &haystack, std::string_view needle)
    {
        auto index = std::find(haystack.begin(), haystack.end(), needle) - haystack.begin();
        if constexpr (std::is_same_v<T, int>)
            return std::stoi(haystack[index + 1]);
        else if constexpr (std::is_same_v<T, float>)
            return std::stof(haystack[index + 1]);
        else if constexpr (std::is_same_v<T, uint64_t>)
            return std::stoull(haystack[index + 1]);
        else
            return haystack[index + 1];
    }

    void loadConfig(const CMD::GameManagerOptions &mc);

    Stats getStats() const;
    void setStats(const Stats &stats);

    std::vector<std::string> getPGNS() const;
    void setStorePGN(bool v);

    void printElo() const;

    void startTournament(const std::vector<EngineConfiguration> &configs);

    void stopPool();

    const std::string startpos_ = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    static const Score mate_score_ = 100'000;

  private:
    std::string fetchNextFen();

    void writeToFile(const std::string &data);

    MoveData parseEngineOutput(const Board &board, const std::vector<std::string> &output,
                               const std::string &move, int64_t measuredTime) const;

    bool checkEngineStatus(UciEngine &engine, Match &match, int roundId) const;

    void updateTrackers(DrawAdjTracker &drawTracker, ResignAdjTracker &resignTracker,
                        const Score moveScore, const int move_number) const;

    GameResult checkAdj(Match &match, const DrawAdjTracker &drawTracker,
                        const ResignAdjTracker &resignTracker, const Score score,
                        const Color lastSideThatMoved) const;

    bool playNextMove(UciEngine &engine, std::string &positionInput, Board &board,
                      TimeControl &timeLeftUs, const TimeControl &timeLeftThem, GameResult &res,
                      Match &match, DrawAdjTracker &drawTracker, ResignAdjTracker &resignTracker,
                      int roundId);

    Match startMatch(UciEngine &engine1, UciEngine &engine2, int roundId, std::string openingFen);

    std::vector<Match> runH2H(CMD::GameManagerOptions localMatchConfig,
                              const std::vector<EngineConfiguration> &configs,
                              const std::string &fen);

    CMD::GameManagerOptions match_config_ = {};

    ThreadPool pool_ = ThreadPool(1);

    SPRT sprt_ = SPRT();

    std::vector<std::string> pgns_;
    std::vector<std::string> opening_book_;
    std::vector<std::string> engine_names_;

    std::ofstream file_;
    std::mutex file_mutex_;

    std::atomic<int> wins_ = 0;
    std::atomic<int> draws_ = 0;
    std::atomic<int> losses_ = 0;
    std::atomic<int> round_count_ = 0;
    std::atomic<int> total_count_ = 0;
    std::atomic<int> timeouts_ = 0;

    std::atomic<int> penta_WW_ = 0;
    std::atomic<int> penta_WD_ = 0;
    std::atomic<int> penta_WL_ = 0;
    std::atomic<int> penta_LD_ = 0;
    std::atomic<int> penta_LL_ = 0;

    // Stats from "White" pov for white advantage
    std::atomic<int> white_wins_ = 0;
    std::atomic<int> white_losses_ = 0;

    size_t fen_index_ = 0;

    bool store_pgns_ = false;

    bool save_time_header_ = true;
};

} // namespace fast_chess