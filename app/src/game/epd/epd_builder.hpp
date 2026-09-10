#pragma once

#include <string>

#include <chess.hpp>

#include <core/config/config.hpp>
#include <matchmaking/match/match.hpp>
#include <types/tournament.hpp>

#include <fmt/include/fmt/core.h>

namespace fastchess::epd {

class EpdBuilder {
   public:
    EpdBuilder(const VariantType& variant, const MatchData& match) {
        chess::Board board = chess::Board();
        board.set960(variant == VariantType::FRC);
        board.setFen(match.fen);

        for (const auto& move : match.moves) {
            const auto illegal = !move.legal;

            if (illegal) break;

            board.makeMove<true>(chess::uci::uciToMove(board, move.move));
        }

        epd = fmt::format("{}\n", board.getEpd());
    }

    // Get the newly created epd
    [[nodiscard]] std::string get() const noexcept { return epd; }

   private:
    std::string epd;
};

}  // namespace fastchess::epd
