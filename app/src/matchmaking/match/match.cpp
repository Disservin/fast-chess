#include <matchmaking/match/match.hpp>

#include <algorithm>

#include <types/tournament.hpp>
#include <util/date.hpp>
#include <util/helper.hpp>
#include <util/logger/logger.hpp>

namespace fast_chess {

namespace atomic {
extern std::atomic_bool stop;
}  // namespace atomic

namespace {
bool isFen(const std::string& line) { return line.find(';') == std::string::npos; }
}  // namespace

namespace chrono = std::chrono;

using clock = chrono::high_resolution_clock;
using namespace std::literals;
using namespace chess;

void Match::addMoveData(const Player& player, int64_t measured_time_ms, bool legal) {
    const auto move    = player.engine.bestmove() ? *player.engine.bestmove() : "<none>";
    MoveData move_data = MoveData(move, "0.00", measured_time_ms, 0, 0, 0, 0, legal);

    if (player.engine.output().size() <= 1) {
        data_.moves.push_back(move_data);
        uci_moves_.push_back(move);
        return;
    }

    // extract last info line
    const auto score_type = player.engine.lastScoreType();
    const auto info       = player.engine.lastInfo();

    move_data.nps      = str_utils::findElement<int>(info, "nps").value_or(0);
    move_data.hashfull = str_utils::findElement<int>(info, "hashfull").value_or(0);
    move_data.tbhits   = str_utils::findElement<uint64_t>(info, "tbhits").value_or(0);
    move_data.depth    = str_utils::findElement<int>(info, "depth").value_or(0);
    move_data.seldepth = str_utils::findElement<int>(info, "seldepth").value_or(0);
    move_data.nodes    = str_utils::findElement<uint64_t>(info, "nodes").value_or(0);
    move_data.score    = player.engine.lastScore();

    // Missing elements default to 0
    std::stringstream ss;

    if (score_type == engine::ScoreType::CP) {
        ss << (move_data.score >= 0 ? '+' : '-');
        ss << std::fixed << std::setprecision(2) << (float(std::abs(move_data.score)) / 100);
    } else if (score_type == engine::ScoreType::MATE) {
        ss << (move_data.score > 0 ? "+M" : "-M") << std::to_string(std::abs(move_data.score));
    } else {
        ss << "ERR";
    }

    move_data.score_string = ss.str();

    verifyPvLines(player);

    data_.moves.push_back(move_data);
    uci_moves_.push_back(move);
}

void Match::prepare() {
    board_.set960(config::TournamentConfig.get().variant == VariantType::FRC);

    if (isFen(opening_.fen)) {
        board_.setFen(opening_.fen);
    } else {
        board_.setEpd(opening_.fen);
    }

    start_position_ = board_.getFen() == chess::constants::STARTPOS ? "startpos" : board_.getFen();

    const auto insert_move = [&](const auto& opening_move) {
        const auto move = uci::moveToUci(opening_move, board_.chess960());
        board_.makeMove(opening_move);

        return MoveData(move, "0.00", 0, 0, 0, 0, 0, true, true);
    };

    data_ = MatchData(board_.getFen());

    std::transform(opening_.moves.begin(), opening_.moves.end(), std::back_inserter(data_.moves), insert_move);

    draw_tracker_     = DrawTracker();
    resign_tracker_   = ResignTracker();
    maxmoves_tracker_ = MaxMovesTracker();
}

void Match::start(engine::UciEngine& white, engine::UciEngine& black, const std::vector<int>& cpus) {
    prepare();

    std::transform(data_.moves.begin(), data_.moves.end(), std::back_inserter(uci_moves_),
                   [](const MoveData& data) { return data.move; });

    Player white_player = Player(white);
    Player black_player = Player(black);

    white_player.color = Color::WHITE;
    black_player.color = Color::BLACK;

    white_player.engine.refreshUci();
    black_player.engine.refreshUci();

    white_player.engine.setCpus(cpus);
    black_player.engine.setCpus(cpus);

    auto& first  = board_.sideToMove() == Color::WHITE ? white_player : black_player;
    auto& second = board_.sideToMove() == Color::WHITE ? black_player : white_player;

    const auto start = clock::now();

    try {
        while (true) {
            if (atomic::stop.load()) {
                data_.termination = MatchTermination::INTERRUPT;
                break;
            }

            if (!playMove(first, second)) break;

            if (atomic::stop.load()) {
                data_.termination = MatchTermination::INTERRUPT;
                break;
            }

            if (!playMove(second, first)) break;
        }
    } catch (const std::exception& e) {
    }

    const auto end = clock::now();

    data_.variant = config::TournamentConfig.get().variant;

    data_.end_time = util::time::datetime("%Y-%m-%dT%H:%M:%S %z");
    data_.duration = util::time::duration(chrono::duration_cast<chrono::seconds>(end - start));

    data_.players =
        GamePair(MatchData::PlayerInfo{white_player.engine.getConfig(), white_player.getResult(), white_player.color},
                 MatchData::PlayerInfo{black_player.engine.getConfig(), black_player.getResult(), black_player.color});
}

bool Match::playMove(Player& us, Player& them) {
    const auto gameover = board_.isGameOver();
    const auto name     = us.engine.getConfig().name;
    std::string go_string;
    std::string position_string;

    if (gameover.second == GameResult::DRAW) {
        us.setDraw();
        them.setDraw();
    }

    if (gameover.second == GameResult::LOSE) {
        us.setLost();
        them.setWon();
    }

    if (gameover.first != GameResultReason::NONE) {
        data_.reason = convertChessReason(getColorString(), gameover.first);
        return false;
    }

    // make sure adjudicate is placed after normal termination as it has lower priority
    if (adjudicate(them, us)) {
        return false;
    }

    // disconnect
    if (!us.engine.isready()) {
        setEngineCrashStatus(us, them, go_string, position_string);
        return false;
    }

    // write new uci position
    auto success = us.engine.position(uci_moves_, start_position_);
    position_string = us.engine.positionString();
    if (!success) {
        setEngineCrashStatus(us, them, go_string, position_string);
        return false;
    }

    // wait for readyok
    if (!us.engine.isready()) {
        setEngineCrashStatus(us, them, go_string, position_string);
        return false;
    }

    // write go command
    success = us.engine.go(us.getTimeControl(), them.getTimeControl(), board_.sideToMove());
    go_string = us.engine.goString();
    if (!success) {
        setEngineCrashStatus(us, them, go_string, position_string);
        return false;
    }

    // wait for bestmove
    auto t0     = clock::now();
    auto status = us.engine.readEngine("bestmove", us.getTimeoutThreshold());
    auto t1     = clock::now();

    if (!config::TournamentConfig.get().realtime_logging) {
        us.engine.writeLog();
    }

    if (status == engine::process::Status::ERR || !us.engine.isready()) {
        setEngineCrashStatus(us, them, go_string, position_string);
        return false;
    }

    if (atomic::stop) {
        data_.termination = MatchTermination::INTERRUPT;

        return false;
    }

    const auto elapsed_millis = chrono::duration_cast<chrono::milliseconds>(t1 - t0).count();

    const auto best_move = us.engine.bestmove();
    const auto move      = best_move ? uci::uciToMove(board_, *best_move) : Move::NO_MOVE;
    const auto legal     = isLegal(move);

    const auto timeout = !us.updateTime(elapsed_millis);

    addMoveData(us, elapsed_millis, legal);

    // there are two reasons why best_move could be empty
    // 1. the engine crashed
    // 2. the engine did not respond in time
    // we report a loss on time when the engine didnt respond in time
    // and otherwise an illegal move
    if (best_move == std::nullopt) {
        // Time forfeit
        if (timeout) {
            setEngineTimeoutStatus(us, them, go_string, position_string);
        } else {
            setEngineIllegalMoveStatus(us, them, best_move, go_string, position_string);
        }

        return false;
    }

    // illegal move
    if (!legal) {
        setEngineIllegalMoveStatus(us, them, best_move, go_string, position_string);
        return false;
    }

    if (timeout) {
        setEngineTimeoutStatus(us, them, go_string, position_string);
        return false;
    }

    board_.makeMove(move);

    // CuteChess uses plycount/2 for its movenumber, which is wrong for epd books as it doesnt take
    // into account the fullmove counter of the starting FEN, leading to different behavior between
    // pgn and epd adjudication. fast-chess fixes this by using the fullmove counter from the board
    // object directly
    auto score = us.engine.lastScore();
    auto type  = us.engine.lastScoreType();

    draw_tracker_.update(score, type, board_.halfMoveClock());
    resign_tracker_.update(score, type, ~board_.sideToMove());
    maxmoves_tracker_.update();

    return true;
}

std::string Match::formatWarningMessage(std::string &warning, const std::string &position_string, const std::string &go_string) {
    auto fmt  = fmt::format("From; {}", position_string);
    auto fmt2 = fmt::format("Command; {}", go_string);
    if (!go_string.empty() && !position_string.empty()){
        return fmt::format("{}\n{}\n{}", warning, fmt, fmt2);
    } else if (go_string.empty() && !position_string.empty()) {
        return fmt::format("{}\n{}", warning, fmt);
    }
    return warning;
}

bool Match::isLegal(Move move) const noexcept {
    Movelist moves;
    movegen::legalmoves(moves, board_);

    return std::find(moves.begin(), moves.end(), move) != moves.end();
}

void Match::setEngineCrashStatus(Player& loser, Player& winner, const std::string &go_string, const std::string &position_string) {
    loser.setLost();
    winner.setWon();

    crash_or_disconnect_ = true;

    const auto name  = loser.engine.getConfig().name;
    const auto color = getColorString();

    data_.termination = MatchTermination::DISCONNECT;
    data_.reason      = color + Match::DISCONNECT_MSG;

    std::string warning = fmt::format("Warning; Engine {} disconnects", name);
    warning = formatWarningMessage(warning, position_string, go_string);

    Logger::warn<true>("{}", warning);
}

void Match::setEngineTimeoutStatus(Player& loser, Player& winner, const std::string &go_string, const std::string &position_string) {
    loser.setLost();
    winner.setWon();

    const auto name  = loser.engine.getConfig().name;
    const auto color = getColorString();

    data_.termination = MatchTermination::TIMEOUT;
    data_.reason      = color + Match::TIMEOUT_MSG;

    std::string warning = fmt::format("Warning; Engine {} loses on time", name);
    warning = formatWarningMessage(warning, position_string, go_string);

    Logger::warn<true>("{}", warning);

    // we send a stop command to the engine to prevent it from thinking
    // and wait for a bestmove to appear

    loser.engine.writeEngine("stop");

    if (!loser.engine.outputIncludesBestmove()) {
        // wait 10 seconds for the bestmove to appear
        loser.engine.readEngine("bestmove", 1000ms * 10);
    }
}

void Match::setEngineIllegalMoveStatus(Player& loser, Player& winner, const std::optional<std::string>& best_move, 
                                       const std::string &go_string, const std::string &position_string) {
    loser.setLost();
    winner.setWon();

    const auto name  = loser.engine.getConfig().name;
    const auto color = getColorString();

    data_.termination = MatchTermination::ILLEGAL_MOVE;
    data_.reason      = color + Match::ILLEGAL_MSG;

    std::string warning = fmt::format("Warning; Illegal move {} played by {}", best_move ? *best_move : "<none>", name);
    warning = formatWarningMessage(warning, position_string, go_string);

    Logger::warn<true>("{}", warning);
}

bool Match::isUciMove(const std::string& move) noexcept {
    bool is_uci = false;

    constexpr auto is_digit     = [](char c) { return c >= '0' && c <= '9'; };
    constexpr auto is_file      = [](char c) { return c >= 'a' && c <= 'h'; };
    constexpr auto is_promotion = [](char c) { return c == 'n' || c == 'b' || c == 'r' || c == 'q'; };

    // assert that the move is in uci format, [abcdefgh][0-9][abcdefgh][0-9][nbrq]
    if (move.size() >= 4) {
        is_uci = is_file(move[0]) && is_digit(move[1]) && is_file(move[2]) && is_digit(move[3]);
    }

    if (move.size() == 5) {
        is_uci = is_uci && is_promotion(move[4]);
    }

    return is_uci;
}

void Match::verifyPvLines(const Player& us) {
    const static auto verifyPv = [](Board board, const std::string& startpos, const std::vector<std::string>& uci_moves,
                                    const std::string& info) {
        // skip lines without pv
        const auto tokens = str_utils::splitString(info, ' ');
        if (!str_utils::contains(tokens, "pv")) return;

        const auto fen = board.getFen();
        auto it_start  = std::find(tokens.begin(), tokens.end(), "pv") + 1;
        auto it_end    = std::find_if(it_start, tokens.end(), [](const auto& token) { return !isUciMove(token); });

        Movelist moves;

        while (it_start != it_end) {
            movegen::legalmoves(moves, board);

            if (std::find(moves.begin(), moves.end(), uci::uciToMove(board, *it_start)) == moves.end()) {
                auto fmt      = fmt::format("Warning; Illegal pv move {} pv: {}", *it_start, info);
                auto position = fmt::format("position {}", startpos == "startpos" ? "startpos" : ("fen " + startpos));
                auto fmt2     = fmt::format("From; {} moves {}", position, str_utils::join(uci_moves, " "));

                Logger::warn<true>(fmt + "\n" + fmt2);

                break;
            }

            board.makeMove(uci::uciToMove(board, *it_start));

            it_start++;
        }
    };

    for (const auto& info : us.engine.output()) {
        verifyPv(board_, start_position_, uci_moves_, info.line);
    }
}

bool Match::adjudicate(Player& us, Player& them) noexcept {
    if (config::TournamentConfig.get().resign.enabled && resign_tracker_.resignable() && us.engine.lastScore() < 0) {
        us.setLost();
        them.setWon();

        const auto color = getColorString(board_.sideToMove());

        data_.termination = MatchTermination::ADJUDICATION;
        data_.reason      = color + Match::ADJUDICATION_WIN_MSG;

        return true;
    }

    if (config::TournamentConfig.get().draw.enabled && draw_tracker_.adjudicatable(board_.fullMoveNumber() - 1)) {
        us.setDraw();
        them.setDraw();

        data_.termination = MatchTermination::ADJUDICATION;
        data_.reason      = Match::ADJUDICATION_MSG;

        return true;
    }

    if (config::TournamentConfig.get().maxmoves.enabled && maxmoves_tracker_.maxmovesreached()) {
        us.setDraw();
        them.setDraw();

        data_.termination = MatchTermination::ADJUDICATION;
        data_.reason      = Match::ADJUDICATION_MSG;

        return true;
    }

    return false;
}

std::string Match::convertChessReason(const std::string& engine_color, GameResultReason reason) noexcept {
    if (reason == GameResultReason::CHECKMATE) {
        std::string color = engine_color == "White" ? "Black" : "White";
        return color + Match::CHECKMATE_MSG;
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

    return "";
}

}  // namespace fast_chess
