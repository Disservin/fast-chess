#pragma once

#include <chrono>

#include <engines/uci_engine.hpp>

namespace fast_chess {

class Player {
   public:
    explicit Player(UciEngine &uci_enigne)
        : engine(uci_enigne), time_control_(uci_enigne.getConfig().limit.tc) {
        if (time_control_.fixed_time != 0) {
            time_left_ = time_control_.fixed_time;
        } else {
            time_left_ = time_control_.time;
        }
    }

    /// @brief The timeout threshold for the read engine command.
    /// This has nothing to do with the time control itself.
    /// @return time in ms
    [[nodiscard]] std::chrono::milliseconds getTimeoutThreshold() const {
        if (engine.getConfig().limit.nodes != 0     //
            || engine.getConfig().limit.plies != 0  //
            || time_control_.fixed_time != 0) {
            // no timeout
            return std::chrono::milliseconds(0);
        }

        return std::chrono::milliseconds(time_left_ + 100) /* margin*/;
    }

    /// @brief remove the elapsed time from the participant's time
    /// @param elapsed_millis
    /// @return `false` when out of time
    [[nodiscard]] bool updateTime(const int64_t elapsed_millis) {
        const auto &tc = engine.getConfig().limit.tc;
        if (tc.time == 0) {
            return true;
        }

        time_left_ -= elapsed_millis;

        if (time_left_ < -tc.timemargin) {
            return false;
        }

        if (time_left_ < 0) {
            time_left_ = 0;
        }

        time_left_ += time_control_.increment;

        return true;
    }

    /// @brief Build the uci position input from the given moves and fen.
    /// @param moves
    /// @param fen
    /// @return
    [[nodiscard]] static std::string buildPositionInput(const std::vector<std::string> &moves,
                                                        const std::string &fen) {
        std::string position = fen == "startpos" ? "position startpos" : ("position fen " + fen);

        if (!moves.empty()) {
            position += " moves";
            for (const auto &move : moves) {
                position += " " + move;
            }
        }

        return position;
    }

    /// @brief Build the uci go input from the given time controls.
    /// @param stm
    /// @param enemy_tc
    /// @return
    [[nodiscard]] std::string buildGoInput(chess::Color stm, const TimeControl &enemy_tc) const {
        std::stringstream input;
        input << "go";

        if (engine.getConfig().limit.nodes != 0)
            input << " nodes " << engine.getConfig().limit.nodes;

        if (engine.getConfig().limit.plies != 0)
            input << " depth " << engine.getConfig().limit.plies;

        // We cannot use st and tc together
        if (time_control_.fixed_time != 0) {
            input << " movetime " << time_control_.fixed_time;
        } else if (time_left_ != 0 && enemy_tc.time != 0) {
            auto white = stm == chess::Color::WHITE ? time_control_ : enemy_tc;
            auto black = stm == chess::Color::WHITE ? enemy_tc : time_control_;

            input << " wtime " << white.time << " btime " << black.time;

            if (time_control_.increment != 0) {
                input << " winc " << white.increment << " binc " << black.increment;
            }

            if (time_control_.moves != 0) {
                input << " movestogo " << time_control_.moves;
            }
        }

        return input.str();
    }

    [[nodiscard]] const TimeControl &getTimeControl() const { return time_control_; }

    UciEngine &engine;

    chess::Color color       = chess::Color::NONE;
    chess::GameResult result = chess::GameResult::NONE;

   private:
    /// @brief updated time control after each move
    const TimeControl time_control_;

    int64_t time_left_;
};

}  // namespace fast_chess
