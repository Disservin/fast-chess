#pragma once

#include <string>
#include <tuple>
#include <vector>

#include "chess/board.hpp"
#include "matchmaking/match_data.hpp"
#include "matchmaking/participant.hpp"
#include "options.hpp"

namespace fast_chess
{

class Match
{
  public:
    Match() = default;
    Match(CMD::GameManagerOptions game_config, const EngineConfiguration &engine1_config,
          const EngineConfiguration &engine2_config);

    void playMatch(const std::string &openingFen);

    MatchData getMatchData();

  private:
    /// @brief
    /// @param player
    /// @param input
    /// @return true if tell was succesful
    bool tellEngine(Participant &player, const std::string &input);

    bool hasErrors(Participant &player);
    bool isResponsive(Participant &player);

    MoveData parseEngineOutput(const std::vector<std::string> &output, const std::string &move,
                               int64_t measured_time);

    /// @brief
    /// @param player
    /// @param position_input
    /// @param time_left_us
    /// @param time_left_them
    /// @return false if move was illegal
    bool playNextMove(Participant &player, Participant &enemy, std::string &position_input,
                      TimeControl &time_left_us, const TimeControl &time_left_them);

    const std::string startpos_ = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    const Score mate_score_ = 100'000;

    CMD::GameManagerOptions game_config_;

    Participant player_1_;
    Participant player_2_;

    Board board_;

    MatchData match_data_;
};

} // namespace fast_chess