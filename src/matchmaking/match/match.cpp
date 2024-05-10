#include <matchmaking/match/match.hpp>

#include <algorithm>

#include <types/tournament_options.hpp>
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
    MoveData move_data =
        MoveData(player.engine.bestmove(), "0.00", measured_time_ms, 0, 0, 0, 0, legal);

    if (player.engine.output().size() <= 1) {
        data_.moves.push_back(move_data);
        return;
    }

    // extract last info line
    const auto score_type = player.engine.lastScoreType();
    const auto info       = player.engine.lastInfo();

    move_data.nps      = str_utils::findElement<int>(info, "nps").value_or(0);
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
}

void Match::prepare() {
    board_.set960(tournament_options_.variant == VariantType::FRC);
    if (isFen(opening_.fen)) {
        board_.setFen(opening_.fen);
    } else {
        board_.setEpd(opening_.fen);
    }

    start_position_ = board_.getFen() == chess::constants::STARTPOS ? "startpos" : board_.getFen();

    const auto insert_move = [&](const auto& opening_move) {
        const auto move = uci::moveToUci(opening_move, board_.chess960());
        board_.makeMove<true>(opening_move);

        return MoveData(move, "0.00", 0, 0, 0, 0, 0, true, true);
    };

    data_ = MatchData(board_.getFen());

    std::transform(opening_.moves.begin(), opening_.moves.end(), std::back_inserter(data_.moves),
                   insert_move);

    draw_tracker_     = DrawTracker(tournament_options_);
    resign_tracker_   = ResignTracker(tournament_options_);
    maxmoves_tracker_ = MaxMovesTracker(tournament_options_);
}

void Match::start(engine::UciEngine& engine1, engine::UciEngine& engine2,
                  const std::vector<int>& cpus) {
    prepare();

    Player player_1 = Player(engine1);
    Player player_2 = Player(engine2);

    player_1.color = board_.sideToMove();
    player_2.color = ~board_.sideToMove();

    player_1.engine.refreshUci();
    player_2.engine.refreshUci();

    player_1.engine.setCpus(cpus);
    player_2.engine.setCpus(cpus);

    const auto start = clock::now();

    try {
        while (true) {
            if (atomic::stop.load()) {
                data_.termination = MatchTermination::INTERRUPT;
                break;
            }

            if (!playMove(player_1, player_2)) break;

            if (atomic::stop.load()) {
                data_.termination = MatchTermination::INTERRUPT;
                break;
            }

            if (!playMove(player_2, player_1)) break;
        }
    } catch (const std::exception& e) {
    }

    const auto end = clock::now();

    data_.end_time = util::time::datetime("%Y-%m-%dT%H:%M:%S %z");
    data_.duration = util::time::duration(chrono::duration_cast<chrono::seconds>(end - start));

    data_.players =
        std::make_pair(MatchData::PlayerInfo{engine1.getConfig(), player_1.result, player_1.color},
                       MatchData::PlayerInfo{engine2.getConfig(), player_2.result, player_2.color});
}

bool Match::playMove(Player& us, Player& opponent) {
    const auto gameover = board_.isGameOver();
    const auto name     = us.engine.getConfig().name;

    if (gameover.second == GameResult::DRAW) {
        setDraw(us, opponent);
    }

    if (gameover.second == GameResult::LOSE) {
        setLose(us, opponent);
    }

    if (gameover.first != GameResultReason::NONE) {
        data_.reason = convertChessReason(name, gameover.first);
        return false;
    }

    // disconnect
    if (!us.engine.isResponsive()) {
        setLose(us, opponent);

        data_.termination = MatchTermination::DISCONNECT;
        data_.reason      = name + Match::DISCONNECT_MSG;

        return false;
    }

    // write new uci position
    std::vector<std::string> uci_moves;
    std::transform(data_.moves.begin(), data_.moves.end(), std::back_inserter(uci_moves),
                   [](const MoveData& data) { return data.move; });

    us.engine.writeEngine(Player::buildPositionInput(uci_moves, start_position_));
    // write go command
    us.engine.writeEngine(us.buildGoInput(board_.sideToMove(), opponent.getTimeControl()));

    // wait for bestmove
    auto t0 = clock::now();

    try {
        us.engine.readEngine("bestmove", us.getTimeoutThreshold());
    } catch (const std::exception& e) {
        us.engine.writeLog();
        throw;
    }

    auto t1 = clock::now();

    us.engine.writeLog();

    if (atomic::stop) {
        data_.termination = MatchTermination::INTERRUPT;

        return false;
    }

    const auto elapsed_millis = chrono::duration_cast<chrono::milliseconds>(t1 - t0).count();

    // Illegal move
    const auto best_move = us.engine.bestmove();
    const auto move      = uci::uciToMove(board_, best_move);
    const auto legal     = isLegal(move);

    addMoveData(us, elapsed_millis, legal);

    if (!legal) {
        setLose(us, opponent);

        data_.termination = MatchTermination::ILLEGAL_MOVE;
        data_.reason      = name + Match::ILLEGAL_MSG;

        Logger::log<Logger::Level::WARN>("Warning; Illegal move", best_move, "played by", name);

        return false;
    }

    // Time forfeit
    if (!us.updateTime(elapsed_millis)) {
        setLose(us, opponent);

        data_.termination = MatchTermination::TIMEOUT;
        data_.reason      = name + Match::TIMEOUT_MSG;

        Logger::log<Logger::Level::WARN>("Warning; Engine", name, "loses on time");

        // we send a stop command to the engine to prevent it from thinking
        // and wait for a bestmove to appear

        us.engine.writeEngine("stop");

        if (!us.engine.outputIncludesBestmove()) {
            // wait for bestmove, indefinitely
            // is this a good idea? what if the engine is stuck?
            us.engine.readEngine("bestmove", 0ms);
        }

        return false;
    }

    draw_tracker_.update(us.engine.lastScore(), data_.moves.size() / 2, us.engine.lastScoreType());
    resign_tracker_.update(us.engine.lastScore(), us.engine.lastScoreType());
    maxmoves_tracker_.update();

    board_.makeMove<true>(move);

    return !adjudicate(us, opponent);
}

bool Match::isLegal(Move move) const noexcept {
    Movelist moves;
    movegen::legalmoves(moves, board_);

    return std::find(moves.begin(), moves.end(), move) != moves.end();
}

bool Match::isUciMove(const std::string& move) noexcept {
    bool is_uci = false;

    constexpr auto is_digit     = [](char c) { return c >= '0' && c <= '9'; };
    constexpr auto is_file      = [](char c) { return c >= 'a' && c <= 'h'; };
    constexpr auto is_promotion = [](char c) {
        return c == 'n' || c == 'b' || c == 'r' || c == 'q';
    };

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
    const auto verifyPv = [&](const std::vector<std::string>& tokens, std::string_view info) {
        auto tmp      = board_;
        auto it_start = std::find(tokens.begin(), tokens.end(), "pv") + 1;
        auto it_end   = std::find_if(it_start, tokens.end(),
                                     [](const auto& token) { return !isUciMove(token); });

        Movelist moves;

        std::for_each(it_start, it_end, [&](const auto& token) {
            movegen::legalmoves(moves, tmp);

            if (std::find(moves.begin(), moves.end(), uci::uciToMove(tmp, token)) == moves.end()) {
                Logger::log<Logger::Level::WARN>("Warning; Illegal pv move ", token, "pv:", info);
            }

            tmp.makeMove<true>(uci::uciToMove(tmp, token));
        });
    };

    for (const auto& info : us.engine.output()) {
        const auto tokens = str_utils::splitString(info.line, ' ');

        // skip lines without pv
        if (!str_utils::contains(tokens, "pv")) continue;

        verifyPv(tokens, info.line);
    }
}

void Match::setDraw(Player& us, Player& them) noexcept {
    us.result   = GameResult::DRAW;
    them.result = GameResult::DRAW;
}

void Match::setWin(Player& us, Player& them) noexcept {
    us.result   = GameResult::WIN;
    them.result = GameResult::LOSE;
}

void Match::setLose(Player& us, Player& them) noexcept {
    us.result   = GameResult::LOSE;
    them.result = GameResult::WIN;
}

bool Match::adjudicate(Player& us, Player& them) noexcept {
    if (tournament_options_.draw.enabled && draw_tracker_.adjudicatable()) {
        setDraw(us, them);

        data_.termination = MatchTermination::ADJUDICATION;
        data_.reason      = Match::ADJUDICATION_MSG;

        return true;
    }

    if (tournament_options_.maxmoves.enabled && maxmoves_tracker_.maxmovesreached()) {
        setDraw(us, them);

        data_.termination = MatchTermination::ADJUDICATION;
        data_.reason      = Match::ADJUDICATION_MSG;

        return true;
    }

    if (tournament_options_.resign.enabled && resign_tracker_.resignable()) {
        data_.termination = MatchTermination::ADJUDICATION;
        data_.reason      = us.engine.getConfig().name;

        if (us.engine.lastScore() < tournament_options_.resign.score) {
            setLose(us, them);
            data_.reason += Match::ADJUDICATION_LOSE_MSG;
        } else {
            setWin(us, them);
            data_.reason += Match::ADJUDICATION_WIN_MSG;
        }

        return true;
    }

    return false;
}

std::string Match::convertChessReason(const std::string& engine_name,
                                      GameResultReason reason) noexcept {
    if (reason == GameResultReason::CHECKMATE) {
        return engine_name + Match::CHECKMATE_MSG;
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
