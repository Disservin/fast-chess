#pragma once

#include <cstdint>
#include <string>

#include "chess/types.hpp"
#include "engines/engine_config.hpp"

namespace fast_chess
{

struct MoveData
{
    std::string move;
    std::string score_string;
    int64_t elapsed_millis = 0;
    int depth = 0;
    int score = 0;
    uint64_t nodes = 0;
    MoveData() = default;

    MoveData(std::string _move, std::string _score_string, int64_t _elapsed_millis, int _depth,
             int _score, int _nodes)
        : move(_move), score_string(std::move(_score_string)), elapsed_millis(_elapsed_millis),
          depth(_depth), score(_score), nodes(_nodes)
    {
    }
};

struct Match
{
    std::vector<MoveData> moves;
    EngineConfiguration white_engine;
    EngineConfiguration black_engine;
    GameResult result = GameResult::NONE;
    std::string termination;
    std::string start_time;
    std::string end_time;
    std::string duration;
    std::string date;
    std::string fen;
    int round = 0;
    bool legal = true;
    bool needs_restart = false;
};

struct DrawAdjTracker
{
    Score draw_score = 0;
    int move_count = 0;

    DrawAdjTracker() = default;
    DrawAdjTracker(Score draw_score, int move_count)
    {
        this->draw_score = draw_score;
        this->move_count = move_count;
    }
};

struct ResignAdjTracker
{
    int move_count = 0;
    Score resign_score = 0;

    ResignAdjTracker() = default;
    ResignAdjTracker(Score resign_score, int move_count)
    {
        this->resign_score = resign_score;
        this->move_count = move_count;
    }
};

struct Stats
{
    int wins = 0;
    int draws = 0;
    int losses = 0;

    int penta_WW = 0;
    int penta_WD = 0;
    int penta_WL = 0;
    int penta_LD = 0;
    int penta_LL = 0;

    int round_count = 0;
    int total_count = 0;
    int timeouts = 0;

    Stats() = default;

    Stats(int _wins, int _draws, int _losses, int _penta_WW, int _penta_WD, int _penta_WL,
          int _penta_LD, int _penta_LL, int _round_count, int _total_count, int _timeouts)
        : wins(_wins), draws(_draws), losses(_losses), penta_WW(_penta_WW), penta_WD(_penta_WD),
          penta_WL(_penta_WL), penta_LD(_penta_LD), penta_LL(_penta_LL), round_count(_round_count),
          total_count(_total_count), timeouts(_timeouts){};
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_ORDERED_JSON(Stats, wins, draws, losses, penta_WW, penta_WD, penta_WL,
                                                penta_LD, penta_LL, round_count, total_count,
                                                timeouts);

} // namespace fast_chess