#pragma once

#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <chess.hpp>

#include <config/config.hpp>
#include <config/types.hpp>
#include <matchmaking/match/match.hpp>

namespace fast_chess::epd {

class EpdBuilder {
   public:
    EpdBuilder(const MatchData &match) {
        chess::Board board = chess::Board();
        board.set960(config::TournamentOptions.variant == VariantType::FRC);
        board.setFen(match.fen);

        for (const auto &move : match.moves) {
            const auto illegal = !move.legal;

            if (illegal) {
                break;
            }

            board.makeMove(chess::uci::uciToMove(board, move.move));
        }

        epd << board.getEpd() << "\n";
    }

    // Get the newly created epd
    [[nodiscard]] std::string get() const noexcept { return epd.str(); }

   private:
    std::stringstream epd;
};

}  // namespace fast_chess::epd