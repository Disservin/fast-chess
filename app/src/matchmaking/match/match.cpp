#include <matchmaking/match/match.hpp>

#include <algorithm>
#include <cassert>
#include <iterator>
#include <regex>
#include <string_view>

#include <chess.hpp>
#include <core/globals/globals.hpp>
#include <core/helper.hpp>
#include <core/logger/logger.hpp>
#include <core/time/time.hpp>
#include <types/exception.hpp>
#include <types/tournament.hpp>

namespace fastchess {

namespace chrono = std::chrono;

using namespace std::literals;
using namespace chess;
using clock = chrono::steady_clock;

std::string formatTimeoutReason(std::string_view color, int64_t overrun_ms);

namespace {

std::string to_escaped_string(const std::string& binary_str) {
    std::string result;

    for (unsigned char c : binary_str) {
        if (c >= 32 && c <= 126) {
            result.push_back(static_cast<char>(c));
        } else {
            fmt::format_to(std::back_inserter(result), "\\x{:02x}", c);
        }
    }

    return result;
}

bool isFen(const std::string& line) { return line.find(';') == std::string::npos; }

[[nodiscard]] std::pair<GameResultReason, GameResult> isGameOverSimple(const Board& board) {
    const auto [reason, result] = board.isGameOver();

    if (reason == GameResultReason::INSUFFICIENT_MATERIAL) {
        return {GameResultReason::NONE, GameResult::NONE};
    }

    return {reason, result};
}

std::string pvWarningFormat(PvWarning warning) {
    switch (warning) {
        case PvWarning::IllegalMove:
            return "Warning; Illegal PV move - move {} from {}";
        case PvWarning::ContinuesAfterThreefoldRepetition:
            return "Warning; PV continues after threefold repetition - move {} from {}";
        case PvWarning::ContinuesAfterFiftyMoveRule:
            return "Warning; PV continues after fifty-move rule - move {} from {}";
        case PvWarning::ContinuesAfterCheckmate:
            return "Warning; PV continues after checkmate - move {} from {}";
        case PvWarning::ContinuesAfterStalemate:
            return "Warning; PV continues after stalemate - move {} from {}";
        case PvWarning::IncompleteMatingPv:
            return "Warning; Incomplete mating PV - from {}";
        case PvWarning::TooLongMatingPv:
            return "Warning; Too long mating PV - from {}";
        case PvWarning::MatingPvDoesNotEndWithCheckmate:
            return "Warning; Mating PV does not end with checkmate - from {}";
        case PvWarning::BestmoveMismatch:
            return "Warning; Bestmove does not match beginning of last PV - move {} from {}";
    }

    assert(false);
    return {};
}

bool pvWarningHasMove(PvWarning warning) {
    switch (warning) {
        case PvWarning::IllegalMove:
        case PvWarning::ContinuesAfterThreefoldRepetition:
        case PvWarning::ContinuesAfterFiftyMoveRule:
        case PvWarning::ContinuesAfterCheckmate:
        case PvWarning::ContinuesAfterStalemate:
        case PvWarning::BestmoveMismatch:
            return true;
        case PvWarning::IncompleteMatingPv:
        case PvWarning::TooLongMatingPv:
        case PvWarning::MatingPvDoesNotEndWithCheckmate:
            return false;
    }

    assert(false);
    return false;
}

// emits a warning if both engines claim to have a proven win (same for loss)
void checkMateScoreSignMismatch(const Player& them, const Player& us, const Board& board,
                                const std::string& start_position, const MatchData& data) {
    if (data.moves.size() <= 1) return;

    const auto& themMove = data.moves[data.moves.size() - 2];

    if (themMove.book) return;

    const auto& usMove = data.moves[data.moves.size() - 1];

    if (!themMove.score.has_value() || !usMove.score.has_value()) {
        return;
    }

    const auto themScore = themMove.score.value();
    const auto usScore   = usMove.score.value();

    if (!themScore.isMate() || !usScore.isMate()) {
        return;
    }

    const auto themMate = themScore.value;
    const auto usMate   = usScore.value;

    if (themMate * usMate <= 0) return;

    const bool whiteToMove = board.sideToMove() == Color::WHITE;

    const auto themColor = whiteToMove ? "White" : "Black";
    const auto usColor   = whiteToMove ? "Black" : "White";

    const auto startPos = start_position == "startpos" ? start_position : fmt::format("fen {}", start_position);

    const auto warning =
        fmt::format("Warning; Sign mismatch in mate scores {} and {} from {} ({}) and {} ({})", themMate, usMate,
                    them.engine.getConfig().name, themColor, us.engine.getConfig().name, usColor);

    const auto uciInfo = fmt::format("Infos; {} ; {}", them.engine.lastInfoLine(), us.engine.lastInfoLine());

    const auto position = fmt::format("Position; {}", startPos);
    const auto moves    = fmt::format("Moves; {}", str_utils::join(data.getMoves(), " "));

    const auto separator = config::TournamentConfig->test_env ? " :: " : "\n";

    Logger::print<Logger::Level::WARN>("{1}{0}{2}{0}{3}{0}{4}", separator, warning, uciInfo, position, moves);
}

}  // namespace

std::optional<PvCheckResult> checkParsedPvLine(Board board, const engine::UciInfo& info, bool check_mate_pvs) {
    if (info.pv.empty()) {
        return std::nullopt;
    }

    Movelist moves;

    for (const auto& move : info.pv) {
        moves.clear();

        const auto gameoverResult = isGameOverSimple(board);
        const auto gameover       = gameoverResult.second != GameResult::NONE;

        if (gameover) {
            if (gameoverResult.first == GameResultReason::THREEFOLD_REPETITION) {
                return PvCheckResult{PvWarning::ContinuesAfterThreefoldRepetition, move};
            }
            if (gameoverResult.first == GameResultReason::FIFTY_MOVE_RULE) {
                return PvCheckResult{PvWarning::ContinuesAfterFiftyMoveRule, move};
            }
            if (gameoverResult.first == GameResultReason::CHECKMATE) {
                return PvCheckResult{PvWarning::ContinuesAfterCheckmate, move};
            }
            if (gameoverResult.first == GameResultReason::STALEMATE) {
                return PvCheckResult{PvWarning::ContinuesAfterStalemate, move};
            }

            return PvCheckResult{PvWarning::IllegalMove, move};
        }

        movegen::legalmoves(moves, board);
        const auto uci_move     = uci::uciToMove(board, move);
        const auto illegal_move = std::find(moves.begin(), moves.end(), uci_move) == moves.end();

        if (illegal_move) {
            return PvCheckResult{PvWarning::IllegalMove, move};
        }

        board.makeMove<true>(uci_move);
    }

    if (!check_mate_pvs) {
        return std::nullopt;
    }

    if (!info.score.has_value() || !info.score->isMate() || info.isBound()) {
        return std::nullopt;
    }

    const auto score_value = info.score->value;
    const uint64_t plies   = score_value > 0 ? score_value * 2 - 1 : score_value * -2;

    if (info.pv.size() < plies) {
        return PvCheckResult{PvWarning::IncompleteMatingPv, {}};
    }
    if (info.pv.size() > plies) {
        return PvCheckResult{PvWarning::TooLongMatingPv, {}};
    }

    movegen::legalmoves(moves, board);
    if (!moves.empty() || !board.inCheck()) {
        return PvCheckResult{PvWarning::MatingPvDoesNotEndWithCheckmate, {}};
    }

    return std::nullopt;
}

std::optional<PvCheckResult> checkParsedBestmovePv(const engine::UciInfo& info, std::string_view best_move) {
    if (best_move.empty() || info.isBound() || info.pv.empty() || best_move == info.pv[0]) {
        return std::nullopt;
    }

    return PvCheckResult{PvWarning::BestmoveMismatch, std::string(best_move)};
}

std::optional<PvCheckResult> checkPvLine(Board board, std::string_view info, bool check_mate_pvs) {
    return checkParsedPvLine(std::move(board), engine::UciEngine::parseInfo(info), check_mate_pvs);
}

std::optional<PvCheckResult> checkBestmovePv(std::string_view info, std::string_view best_move) {
    return checkParsedBestmovePv(engine::UciEngine::parseInfo(info), best_move);
}

Match::Match(const book::Opening& opening)
    : opening_(opening),
      draw_tracker_(config::TournamentConfig->draw),
      resign_tracker_(config::TournamentConfig->resign),
      maxmoves_tracker_(config::TournamentConfig->maxmoves),
      tb_adjudication_tracker_(config::TournamentConfig->tb_adjudication) {
    board_.set960(config::TournamentConfig->variant == VariantType::FRC);

    const auto success = isFen(opening_.fen_epd) ? board_.setFen(opening_.fen_epd) : board_.setEpd(opening_.fen_epd);

    if (!success) {
        LOG_FATAL_THREAD("Failed to set board position from opening book");
        atomic::stop                 = true;
        atomic::abnormal_termination = true;
        auto fen                     = to_escaped_string(opening_.fen_epd);

        Logger::print<Logger::Level::FATAL>("Failed to set position from opening book, invalid FEN or EPD: {}", fen);
        throw fastchess_exception::format("Failed to set position from opening book, invalid FEN or EPD: {}", fen);
    }

    const auto fen = board_.getFen();

    data_           = MatchData(fen);
    start_position_ = fen == chess::constants::STARTPOS ? "startpos" : fen;

    const auto insert_move = [&](const auto& opening_move) {
        const auto move = uci::moveToUci(opening_move, board_.chess960());
        board_.makeMove<true>(opening_move);

        MoveData move_data{move};

        move_data.book  = true;
        move_data.legal = true;

        return move_data;
    };

    std::transform(opening_.moves.begin(), opening_.moves.end(), std::back_inserter(data_.moves), insert_move);
}

void Match::addMoveData(const Player& player, const std::string& move, int64_t measured_time_ms, int64_t latency,
                        int64_t timeleft, bool legal) {
    MoveData move_data;

    move_data.move           = move;
    move_data.elapsed_millis = measured_time_ms;
    move_data.legal          = legal;

    if (player.engine.getStdoutLines().size() <= 1) {
        data_.moves.push_back(move_data);
        return;
    }

    // extract last info line
    const auto info_line = player.engine.lastInfoLine();

    if (info_line.empty()) {
        Logger::print<Logger::Level::WARN>("Warning; No info line available to extract score from engine {}",
                                           player.engine.getConfig().name);
    }

    const auto info = engine::UciEngine::parseInfo(info_line);

    if (!info_line.empty() && !info.score.has_value()) {
        Logger::print<Logger::Level::WARN>("Warning; Could not extract score from engine {}: {}",
                                           player.engine.getConfig().name, info.score.error());
    }

    move_data.nps      = info.nps.value_or(0);
    move_data.hashfull = info.hashfull.value_or(0);
    move_data.tbhits   = info.tbhits.value_or(0);
    move_data.depth    = info.depth.value_or(0);
    move_data.seldepth = info.seldepth.value_or(0);
    move_data.nodes    = info.nodes.value_or(0);
    move_data.pv       = str_utils::join(info.pv, " ");
    move_data.score =
        info.score.has_value() ? std::make_optional(Score{info.score->type, info.score->value}) : std::nullopt;
    move_data.timeleft = timeleft;
    move_data.latency  = latency;

    if (!config::TournamentConfig->pgn.additional_lines_rgx.empty()) {
        for (const auto& rgx : config::TournamentConfig->pgn.additional_lines_rgx) {
            const auto lines = player.engine.getStdoutLines();
            const auto regex = std::regex(rgx);
            // find the last line that matches the regex, iterate in reverse
            for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
                if (std::regex_search((*it)->line, regex)) {
                    move_data.additional_lines.push_back((*it)->line);
                    break;
                }
            }
        }
    }

    verifyPvLines(player, legal ? move : "");

    data_.moves.push_back(move_data);
}

void Match::start(engine::UciEngine& white, engine::UciEngine& black, std::optional<std::vector<int>>& cpus) {
    Player white_player = Player(white);
    Player black_player = Player(black);

    if (auto ret = white_player.engine.start(cpus); !ret) {
        if (atomic::stop.load()) return;

        atomic::stop                 = true;
        atomic::abnormal_termination = true;

        Logger::print<Logger::Level::FATAL>("Fatal; {} engine startup failure: \"{}\"",
                                            white_player.engine.getConfig().name, ret.error());

        return;
    }

    if (auto ret = black_player.engine.start(cpus); !ret) {
        if (atomic::stop.load()) return;
        atomic::stop                 = true;
        atomic::abnormal_termination = true;

        Logger::print<Logger::Level::FATAL>("Fatal; {} engine startup failure: \"{}\"",
                                            black_player.engine.getConfig().name, ret.error());

        return;
    }

    if (atomic::stop.load()) {
        return;
    }

    if (!white_player.engine.refreshUci()) {
        setEngineCrashStatus(white_player, black_player);
    }

    if (!black_player.engine.refreshUci()) {
        setEngineCrashStatus(black_player, white_player);
    }

    // check connection
    validConnection(white_player, black_player);

    auto& first  = board_.sideToMove() == Color::WHITE ? white_player : black_player;
    auto& second = board_.sideToMove() == Color::WHITE ? black_player : white_player;

    const auto start = clock::now();

    if (data_.termination == MatchTermination::None) {
        gameLoop(first, second);
    }

    const auto end = clock::now();

    data_.variant = config::TournamentConfig->variant;

    data_.end_time = time::datetime_iso();
    data_.duration = time::duration(chrono::duration_cast<chrono::seconds>(end - start));

    data_.players = GamePair(MatchData::PlayerInfo{white_player}, MatchData::PlayerInfo{black_player});
}

void Match::gameLoop(Player& first, Player& second) {
    auto interrupted = [&] {
        if (!atomic::stop.load()) return false;

        data_.termination = MatchTermination::INTERRUPT;
        data_.reason      = Match::INTERRUPTED_MSG;
        return true;
    };

    auto takeTurn = [&](Player& current, Player& opponent) {
        if (interrupted()) return false;

        try {
            return playMove(current, opponent);
        } catch (const std::exception& e) {
            Logger::print<Logger::Level::FATAL>("Match failed with exception: {}", e.what());

            current.setLost();
            opponent.setWon();

            data_.termination = MatchTermination::INTERRUPT;
            data_.reason      = Match::INTERRUPTED_MSG;

            return false;
        }
    };

    while (takeTurn(first, second) && takeTurn(second, first)) {
    }

    assert(data_.termination != MatchTermination::None);
    assert(first.getResult() != GameResult::NONE);
    assert(second.getResult() != GameResult::NONE);
}

bool Match::playMove(Player& us, Player& them) {
    const auto gameover = isGameOver();
    const auto name     = us.engine.getConfig().name;

    if (gameover.second == GameResult::DRAW) {
        us.setDraw();
        them.setDraw();
    }

    if (gameover.second == GameResult::LOSE) {
        us.setLost();
        them.setWon();
    }

    if (gameover.first != GameResultReason::NONE) {
        data_.termination = MatchTermination::NORMAL;
        data_.reason      = convertChessReason((~board_.sideToMove()).longStr(), gameover.first);
        return false;
    }

    if (us.hasTimeControl() && us.getTimeControl().getTimeLeft() <= 0) {
        setEngineTimeoutStatus(us, them, std::nullopt, 0, false);
        return false;
    }

    // make sure adjudicate is placed after normal termination as it has lower priority
    if (adjudicate(them, us)) {
        return false;
    }

    // make sure the engine is not in an invalid state
    if (!validConnection(us, them)) return false;

    // write new uci position
    if (!us.engine.position(data_.getMoves(), start_position_)) {
        setEngineCrashStatus(us, them);
        return false;
    }

    // make sure the engine is not in an invalid state
    // after writing the position
    if (!validConnection(us, them)) return false;

    // write go command
    LOG_TRACE_THREAD("Engine {} is thinking", name);
    if (!us.engine.go(us.getTimeControl(), them.getTimeControl(), board_.sideToMove())) {
        setEngineCrashStatus(us, them);
        return false;
    }

    // prepare the engine for reading
    us.engine.setupReadEngine();

    // wait for output line starting with "bestmove"
    const auto t0     = clock::now();
    const auto status = us.engine.readEngineLowLat("bestmove", us.getTimeoutThreshold());
    const auto t1     = clock::now();

    LOG_TRACE_THREAD("Engine {} is done thinking", name);

    if (!config::TournamentConfig->log.realtime) {
        us.engine.writeLog();
    }

    if (atomic::stop) {
        data_.termination = MatchTermination::INTERRUPT;

        return false;
    }

    LOG_TRACE_THREAD("Check if engine {} is in a ready state", name);

    if (status.code == engine::process::Status::ERR) {
        setEngineCrashStatus(us, them);
        return false;
    }

    // make sure the engine is not in an invalid state after
    // the search completed
    if (!validConnection(us, them)) return false;

    LOG_TRACE_THREAD("Engine {} is in a ready state", name);

    const auto elapsed_ms = chrono::duration_cast<chrono::milliseconds>(t1 - t0).count();

    // calculate latency
    const auto last_time = us.engine.lastTime().count();
    const auto latency   = elapsed_ms - last_time;
    if (config::TournamentConfig->show_latency) {
        LOG_INFO_THREAD("Engine {} latency: {}ms (elapsed: {}, reported: {})", name, latency, elapsed_ms, last_time);
    }

    const auto best_move = us.engine.bestmove(status.code == engine::process::Status::OK);
    const auto move      = best_move && uci::isUciMove(*best_move) ? uci::uciToMove(board_, *best_move) : Move::NO_MOVE;
    const auto legal     = isLegal(move);

    const auto timeout    = us.hasTimeControl() ? !us.getTimeControl().updateTime(elapsed_ms) : false;
    const auto timeleft   = us.hasTimeControl() ? us.getTimeControl().getTimeLeft() : 0;
    const auto overrun_ms = timeleft < 0 ? -timeleft : 0;

    if (best_move) {
        addMoveData(us, *best_move, elapsed_ms, latency, timeleft, legal);
    }

    // there are two reasons why best_move could be empty
    // 1. the engine crashed
    // 2. the engine did not respond in time
    // we report a loss on time when the engine didn't respond in time
    // and otherwise an illegal move
    if (!best_move) {
        // Time forfeit
        if (timeout) {
            setEngineTimeoutStatus(us, them, best_move, overrun_ms);
        } else {
            setEngineIllegalMoveStatus(us, them, best_move);
        }

        return false;
    }

    // illegal move
    if (!legal) {
        setEngineIllegalMoveStatus(us, them, best_move);
        return false;
    }

    if (timeout) {
        setEngineTimeoutStatus(us, them, best_move, overrun_ms);
        return false;
    }

    board_.makeMove<true>(move);

    // CuteChess uses plycount/2 for its movenumber, which is wrong for epd books as it doesnt take
    // into account the fullmove counter of the starting FEN, leading to different behavior between
    // pgn and epd adjudication. fastchess fixes this by using the fullmove counter from the board
    // object directly
    auto usScore = us.engine.lastScore();

    if (usScore.has_value()) {
        draw_tracker_.update(usScore.value(), board_.halfMoveClock());
        resign_tracker_.update(usScore.value(), ~board_.sideToMove());
    } else {
        draw_tracker_.invalidate();
        resign_tracker_.invalidate(~board_.sideToMove());
    }

    maxmoves_tracker_.update();

    checkMateScoreSignMismatch(them, us, board_, start_position_, data_);

    return true;
}

bool Match::validConnection(Player& us, Player& them) {
    const auto is_ready = us.engine.isready(engine::UciEngine::getPingTime());

    if (is_ready.code == engine::process::Status::TIMEOUT) {
        setEngineStallStatus(us, them);
        return false;
    }

    if (is_ready.code != engine::process::Status::OK) {
        setEngineCrashStatus(us, them);
        return false;
    }

    return true;
}

bool Match::isLegal(Move move) const noexcept {
    Movelist moves;
    movegen::legalmoves(moves, board_);

    return std::find(moves.begin(), moves.end(), move) != moves.end();
}

std::pair<chess::GameResultReason, chess::GameResult> Match::isGameOver() const { return board_.isGameOver(); }

void Match::setEngineCrashStatus(Player& loser, Player& winner) {
    loser.setLost();
    winner.setWon();

    stall_or_disconnect_ = true;

    const auto name  = loser.engine.getConfig().name;
    const auto color = board_.sideToMove().longStr();

    if (atomic::stop) {
        data_.termination = MatchTermination::INTERRUPT;
        data_.reason      = Match::INTERRUPTED_MSG;
    } else {
        data_.termination = MatchTermination::DISCONNECT;
        data_.reason      = fmt::format(Match::DISCONNECT_MSG, color);
    }

    LOG_WARN_THREAD("Engine {} disconnects", name);
}

void Match::setEngineStallStatus(Player& loser, Player& winner) {
    loser.setLost();
    winner.setWon();

    stall_or_disconnect_ = true;

    const auto name  = loser.engine.getConfig().name;
    const auto color = board_.sideToMove().longStr();

    data_.termination = MatchTermination::STALL;
    data_.reason      = fmt::format(Match::STALL_MSG, color);

    LOG_WARN_THREAD("Engine {} stalls", name);
}

void Match::setEngineTimeoutStatus(Player& loser, Player& winner, const std::optional<std::string>& best_move,
                                   int64_t overrun_ms, bool engine_started) {
    loser.setLost();
    winner.setWon();

    const auto name  = loser.engine.getConfig().name;
    const auto color = board_.sideToMove().longStr();

    data_.termination = MatchTermination::TIMEOUT;
    data_.reason      = formatTimeoutReason(color, overrun_ms);

    LOG_WARN_THREAD("Engine {} loses on time by {}ms", name, overrun_ms);

    // we send a stop command to the engine to prevent it from thinking
    // and wait for a bestmove to appear

    if (!engine_started) return;

    loser.engine.writeEngine("stop");

    if (!best_move) {
        // wait 10 seconds for the bestmove to appear
        loser.engine.readEngine("bestmove", 1000ms * 10);
    }
}

void Match::setEngineIllegalMoveStatus(Player& loser, Player& winner, const std::optional<std::string>& best_move) {
    loser.setLost();
    winner.setWon();

    const auto name  = loser.engine.getConfig().name;
    const auto color = board_.sideToMove().longStr();

    data_.termination = MatchTermination::ILLEGAL_MOVE;
    data_.reason      = fmt::format(Match::ILLEGAL_MSG, color);

    if (best_move && !uci::isUciMove(*best_move)) {
        Logger::print<Logger::Level::WARN>(
            "Warning; Move does not match uci move format, lowercase and 4/5 chars. Move {} played by {}", *best_move,
            name);
    }

    Logger::print<Logger::Level::WARN>("Warning; Illegal move {} played by {}", best_move.value_or("<none>"), name);
}

void Match::verifyPvLines(const Player& us, const std::string& best_move) {
    const auto info_lines = us.engine.getInfoLines();
    std::vector<std::pair<const std::string*, engine::UciInfo>> parsed_lines;
    parsed_lines.reserve(info_lines.size());

    for (const auto info : info_lines) {
        parsed_lines.emplace_back(info, engine::UciEngine::parseInfo(*info));
        const auto result =
            checkParsedPvLine(board_, parsed_lines.back().second, config::TournamentConfig->check_mate_pvs);

        if (!result.has_value()) {
            continue;
        }

        const auto warning = pvWarningFormat(result->warning);
        const auto out     = pvWarningHasMove(result->warning)
                                 ? fmt::format(fmt::runtime(warning), result->move, us.engine.getConfig().name)
                                 : fmt::format(fmt::runtime(warning), us.engine.getConfig().name);
        auto uci_info      = fmt::format("Info; {}", *info);
        auto position = fmt::format("Position; {}{}", start_position_ == "startpos" ? "" : "fen ", start_position_);
        auto moves    = fmt::format("Moves; {}", str_utils::join(data_.getMoves(), " "));

        auto separator = config::TournamentConfig->test_env ? " :: " : "\n";

        Logger::print<Logger::Level::WARN>("{1}{0}{2}{0}{3}{0}{4}", separator, out, uci_info, position, moves);
    }

    // finally check if the final PV matches bestmove
    if (best_move.empty()) {
        return;
    }

    // find the correct info line: search backwards for multipv 1 or no multipv
    const auto it = std::find_if(parsed_lines.rbegin(), parsed_lines.rend(), [](const auto& line) {
        return !line.second.multipv.has_value() || line.second.multipv == 1;
    });

    if (it == parsed_lines.rend()) {
        return;
    }

    const auto& info  = *it->first;
    const auto result = checkParsedBestmovePv(it->second, best_move);
    if (result.has_value()) {
        const auto warning = pvWarningFormat(result->warning);
        auto start_pos     = start_position_ == "startpos" ? start_position_ : fmt::format("fen {}", start_position_);
        auto out           = fmt::format(fmt::runtime(warning), result->move, us.engine.getConfig().name);
        auto uci_info      = fmt::format("Info; {}", info);
        auto position      = fmt::format("Position; {}", start_pos);
        auto ucimoves      = fmt::format("Moves; {}", str_utils::join(data_.getMoves(), " "));

        auto separator = config::TournamentConfig->test_env ? " :: " : "\n";

        Logger::print<Logger::Level::WARN>("{1}{0}{2}{0}{3}{0}{4}", separator, out, uci_info, position, ucimoves);
    }
}

bool Match::adjudicate(Player& us, Player& them) noexcept {
    // Start with TB adjudication, if applicable, since this provides a sort of 'exact' result, whereas the other
    // adjudication methods are more heuristic.
    if (config::TournamentConfig->tb_adjudication.enabled && tb_adjudication_tracker_.adjudicatable(board_)) {
        const GameResult result = tb_adjudication_tracker_.adjudicate(board_);
        const auto desired_adju = config::TournamentConfig->tb_adjudication.result_type;

        if ((result == GameResult::WIN || result == GameResult::LOSE) &&
            desired_adju & config::TbAdjudication::ResultType::WIN_LOSS) {
            Color c = Color::NONE;

            if (result == GameResult::WIN) {
                us.setLost();
                them.setWon();

                c = board_.sideToMove();
            } else {
                us.setWon();
                them.setLost();

                c = (~board_.sideToMove());
            }

            data_.reason      = fmt::format(Match::ADJUDICATION_TB_WIN_MSG, c.longStr());
            data_.termination = MatchTermination::ADJUDICATION;

            return true;
        }

        if (result == GameResult::DRAW && desired_adju & config::TbAdjudication::ResultType::DRAW) {
            us.setDraw();
            them.setDraw();

            data_.reason      = Match::ADJUDICATION_TB_DRAW_MSG;
            data_.termination = MatchTermination::ADJUDICATION;

            return true;
        }
    }

    const auto score = us.engine.lastScore();
    if (config::TournamentConfig->resign.enabled && resign_tracker_.resignable() && score.has_value() &&
        score.value().value < 0) {
        us.setLost();
        them.setWon();

        const auto color = board_.sideToMove().longStr();

        data_.termination = MatchTermination::ADJUDICATION;
        data_.reason      = fmt::format(Match::ADJUDICATION_WIN_MSG, color);

        return true;
    }

    if (config::TournamentConfig->draw.enabled && draw_tracker_.adjudicatable(board_.fullMoveNumber() - 1)) {
        us.setDraw();
        them.setDraw();

        data_.termination = MatchTermination::ADJUDICATION;
        data_.reason      = Match::ADJUDICATION_MSG;

        return true;
    }

    if (config::TournamentConfig->maxmoves.enabled && maxmoves_tracker_.maxmovesreached()) {
        us.setDraw();
        them.setDraw();

        data_.termination = MatchTermination::ADJUDICATION;
        data_.reason      = Match::ADJUDICATION_MSG;

        return true;
    }

    return false;
}

std::string Match::convertChessReason(const std::string& color, GameResultReason reason) noexcept {
    if (reason == GameResultReason::CHECKMATE) {
        return fmt::format(Match::CHECKMATE_MSG, color);
    }

    if (reason == GameResultReason::STALEMATE) {
        return Match::STALEMATE_MSG;
    }

    if (reason == GameResultReason::INSUFFICIENT_MATERIAL) {
        return Match::INSUFFICIENT_MSG;
    }

    if (reason == GameResultReason::THREEFOLD_REPETITION) {
        return Match::REPETITION_MSG;
    }

    if (reason == GameResultReason::FIFTY_MOVE_RULE) {
        return Match::FIFTY_MSG;
    }

    assert(false && "Unhandled GameResultReason in convertChessReason");
    return "";
}
std::string formatTimeoutReason(std::string_view color, int64_t overrun_ms) {
    const auto overrun = std::max<int64_t>(0, overrun_ms);
    return fmt::format(Match::TIMEOUT_MSG, color, overrun);
}

}  // namespace fastchess
