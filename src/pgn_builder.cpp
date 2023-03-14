#include "pgn_builder.hpp"

#include <iomanip> // std::setprecision
#include <sstream>

namespace fast_chess
{

PgnBuilder::PgnBuilder(const Match &match, const CMD::GameManagerOptions &game_options_)
{
    const std::string result = resultToString(match.result);
    const std::string termination = match.termination;
    std::stringstream ss;

    // clang-format off
    ss << "[Event "         << "\"" << game_options_.event_name    << "\"" << "]" << "\n";
    ss << "[Site "          << "\"" << game_options_.site         << "\"" << "]" << "\n";
    ss << "[Date "          << "\"" << match.date               << "\"" << "]" << "\n";
    ss << "[Round "         << "\"" << match.round              << "\"" << "]" << "\n";
    ss << "[White "         << "\"" << match.white_engine.name   << "\"" << "]" << "\n";
    ss << "[Black "         << "\"" << match.black_engine.name   << "\"" << "]" << "\n";
    ss << "[Result "        << "\"" << result                   << "\"" << "]" << "\n";
    ss << "[FEN "           << "\"" << match.fen                << "\"" << "]" << "\n";
    ss << "[GameDuration "  << "\"" << match.duration           << "\"" << "]" << "\n";
    ss << "[GameEndTime "   << "\"" << match.end_time            << "\"" << "]" << "\n";
    ss << "[GameStartTime " << "\"" << match.start_time          << "\"" << "]" << "\n";
    ss << "[PlyCount "      << "\"" << match.moves.size()       << "\"" << "]" << "\n";
    
    if (!termination.empty())
        ss << "[Termination " << "\"" << termination << "\"" << "]" << "\n";

    if (match.white_engine.tc.fixed_time != 0)  
        ss << "[TimeControl " << "\"" << match.white_engine.tc.fixed_time << "/move" << "\"" << "]" << "\n";
    else 
        ss << "[TimeControl " << "\"" << match.white_engine.tc << "\"" << "]" << "\n";
    // clang-format on
    ss << "\n";
    std::stringstream illegalMove;

    if (!match.legal)
    {
        illegalMove << ", other side makes an illegal move: "
                    << match.moves[match.moves.size() - 1].move;
    }

    auto b = std::make_unique<Board>(match.fen);

    int move_count = 3;
    for (size_t i = 0; i < match.moves.size(); i++)
    {
        const MoveData data = match.moves[i];

        std::stringstream nodesString, timeString;
        timeString << std::fixed << std::setprecision(3) << data.elapsed_millis / 1000.0 << "s";

        if (game_options_.pgn.track_nodes)
        {
            nodesString << " n=" << data.nodes;
        }

        const std::string move =
            MoveToRep(*b, convertUciToMove(*b, data.move), game_options_.pgn.notation != "san");

        if (move_count % 2 != 0)
            ss << move_count / 2 << "."
               << " " << move << " {" << data.score_string << "/" << data.depth << nodesString.str()
               << " " << timeString.str() << illegalMove.str() << "}";
        else
        {
            ss << " " << move << " {" << data.score_string << "/" << data.depth << nodesString.str()
               << " " << timeString.str() << illegalMove.str() << "}";

            if (i == match.moves.size() - 1)
                break;

            if (i % 7 == 0)
                ss << "\n";
            else
                ss << " ";
        };

        if (!match.legal && i == match.moves.size() - 2)
        {
            break;
        }

        b->makeMove(convertUciToMove(*b, data.move));

        move_count++;
    }

    ss << " " << result << "\n";

    pgn_ = ss.str();
}

std::string PgnBuilder::getPGN() const
{
    return pgn_;
}

} // namespace fast_chess
