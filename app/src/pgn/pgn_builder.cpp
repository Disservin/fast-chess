#include <pgn/pgn_builder.hpp>

#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <matchmaking/output/output.hpp>

namespace fastchess::pgn {

namespace str {
template <typename T>
std::string to_string(const T &obj) {
    std::stringstream ss;
    ss << obj;
    return ss.str();
}
}  // namespace str

PgnBuilder::PgnBuilder(const config::Pgn &pgn_config, const MatchData &match, std::size_t round_id)
    : pgn_config_(pgn_config), match_(match) {
    const auto &white_player = match.players.white;
    const auto &black_player = match.players.black;

    const auto is_frc_variant = match.variant == VariantType::FRC;

    addHeader("Event", pgn_config_.event_name);
    addHeader("Site", pgn_config_.site);
    addHeader("Date", match_.date);
    addHeader("Round", std::to_string(round_id));
    addHeader("White", white_player.config.name);
    addHeader("Black", black_player.config.name);
    addHeader("Result", getResultFromMatch(white_player, black_player));

    if (match_.fen != chess::constants::STARTPOS || is_frc_variant) {
        addHeader("SetUp", "1");
        addHeader("FEN", match_.fen);
    }

    if (is_frc_variant) {
        addHeader("Variant", "Chess960");
    }

    if (!pgn_config_.min) {
        addHeader("GameDuration", match_.duration);
        addHeader("GameStartTime", match_.start_time);
        addHeader("GameEndTime", match_.end_time);
        addHeader("PlyCount", std::to_string(match_.moves.size()));
        addHeader("Termination", convertMatchTermination(match_.termination));

        if (white_player.config.limit.tc == black_player.config.limit.tc) {
            addHeader("TimeControl", str::to_string(white_player.config.limit.tc));
        } else {
            addHeader("WhiteTimeControl", str::to_string(white_player.config.limit.tc));
            addHeader("BlackTimeControl", str::to_string(black_player.config.limit.tc));
        }
    }

    pgn_ << "\n";
    // add body

    // create the pgn lines and assert that the line length is below 80 characters
    // otherwise move the move onto the next line

    chess::Board board = chess::Board();
    board.set960(is_frc_variant);
    board.setFen(match_.fen);

    std::size_t move_number = int(board.sideToMove() == chess::Color::BLACK) + 1;
    std::size_t line_length = 0;
    bool first_move         = true;

    for (auto it = match_.moves.begin(); it != match_.moves.end(); ++it) {
        const auto illegal = !it->legal;

        const auto n_dots   = first_move && board.sideToMove() == chess::Color::BLACK ? 3 : 1;
        const auto last     = std::next(it) == match_.moves.end();
        const auto move_str = addMove(board, *it, move_number, n_dots, illegal, last);

        board.makeMove(chess::uci::uciToMove(board, it->move));

        move_number++;

        if (line_length + move_str.size() > LINE_LENGTH) {
            pgn_ << "\n";
            line_length = 0;
        }

        // note: the move length might be larger than LINE_LENGTH
        pgn_ << (line_length == 0 ? "" : " ") << move_str;
        line_length += move_str.size();

        first_move = false;

        if (illegal) {
            break;
        }
    }

    // 8.2.6: Game Termination Markers
    pgn_ << " " << getResultFromMatch(white_player, black_player);
}

template <typename T>
void PgnBuilder::addHeader(std::string_view name, const T &value) noexcept {
    if constexpr (std::is_same_v<T, std::string>) {
        // don't add the header if it is an empty string
        if (value.empty()) {
            return;
        }
    }
    pgn_ << "[" << name << " \"" << value << "\"]\n";
}

std::string PgnBuilder::moveNotation(chess::Board &board, const std::string &move) const noexcept {
    if (pgn_config_.notation == NotationType::SAN) {
        return chess::uci::moveToSan(board, chess::uci::uciToMove(board, move));
    } else if (pgn_config_.notation == NotationType::LAN) {
        return chess::uci::moveToLan(board, chess::uci::uciToMove(board, move));
    } else {
        return move;
    }
}

std::string PgnBuilder::addMove(chess::Board &board, const MoveData &move, std::size_t move_number, int dots,
                                bool illegal, bool last) noexcept {
    std::stringstream ss;

    ss << (dots == 3 || move_number % 2 == 1 ? std::to_string((move_number + 1) / 2) + std::string(dots, '.') + ' '
                                             : "");
    ss << (illegal ? move.move : moveNotation(board, move.move));

    if (!pgn_config_.min) {
        if (move.book) {
            ss << addComment("book");
        } else {
            ss << addComment((move.score_string + "/" + std::to_string(move.depth)),                         //
                             formatTime(move.elapsed_millis),                                                //
                             pgn_config_.track_nodes ? "n=" + std::to_string(move.nodes) : "",               //
                             pgn_config_.track_seldepth ? "sd=" + std::to_string(move.seldepth) : "",        //
                             pgn_config_.track_nps ? "nps=" + std::to_string(move.nps) : "",                 //
                             pgn_config_.track_hashfull ? "hashfull=" + std::to_string(move.hashfull) : "",  //
                             pgn_config_.track_tbhits ? "tbhits=" + std::to_string(move.tbhits) : "",        //
                             last ? match_.reason : ""                                                       //
            );
        }
    }

    return ss.str();
}

std::string PgnBuilder::getResultFromMatch(const MatchData::PlayerInfo &white,
                                           const MatchData::PlayerInfo &black) noexcept {
    if (white.result == chess::GameResult::WIN) {
        return "1-0";
    } else if (black.result == chess::GameResult::WIN) {
        return "0-1";
    } else if (white.result == chess::GameResult::DRAW) {
        return "1/2-1/2";
    } else {
        return "*";
    }
}

std::string PgnBuilder::convertMatchTermination(const MatchTermination &res) noexcept {
    switch (res) {
        case MatchTermination::NORMAL:
            return "normal";
        case MatchTermination::ADJUDICATION:
            return "adjudication";
        case MatchTermination::DISCONNECT:
            return "abandoned";
        case MatchTermination::STALL:
            return "abandoned";
        case MatchTermination::TIMEOUT:
            return "time forfeit";
        case MatchTermination::ILLEGAL_MOVE:
            return "illegal move";
        case MatchTermination::INTERRUPT:
            return "unterminated";
        default:
            return "";
    }
}

}  // namespace fastchess::pgn
