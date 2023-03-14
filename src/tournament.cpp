#include "tournament.hpp"

#include <cassert>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>

#include "elo.hpp"
#include "pgn_builder.hpp"
#include "rand.hpp"

namespace fast_chess
{

Tournament::Tournament(const CMD::GameManagerOptions &mc)
{
    const std::string filename = (mc.pgn.file.empty() ? "fast-chess" : mc.pgn.file) + ".pgn";

    file_.open(filename, std::ios::app);

    loadConfig(mc);
}

void Tournament::loadConfig(const CMD::GameManagerOptions &mc)
{
    Random::mersenne_rand.seed(match_config_.seed);

    match_config_ = mc;

    if (!match_config_.opening.file.empty())
    {
        std::ifstream openingFile;
        std::string line;
        openingFile.open(match_config_.opening.file);

        while (std::getline(openingFile, line))
        {
            opening_book_.emplace_back(line);
        }

        openingFile.close();

        if (match_config_.opening.order == "random")
        {
            // Fisher-Yates / Knuth shuffle
            for (size_t i = 0; i <= opening_book_.size() - 2; i++)
            {
                size_t j = i + (Random::mersenne_rand() % (opening_book_.size() - i));
                std::swap(opening_book_[i], opening_book_[j]);
            }
        }
    }

    pool_.resize(match_config_.concurrency);

    sprt_ = SPRT(match_config_.sprt.alpha, match_config_.sprt.beta, match_config_.sprt.elo0,
                 match_config_.sprt.elo1);
}

std::string Tournament::fetchNextFen()
{
    if (opening_book_.size() == 0)
    {
        return startpos_;
    }
    else if (match_config_.opening.format == "pgn")
    {
        // todo: implementation
    }
    else if (match_config_.opening.format == "epd")
    {
        return opening_book_[(match_config_.opening.start + fen_index_++) % opening_book_.size()];
    }

    return startpos_;
}

std::vector<std::string> Tournament::getPGNS() const
{
    return pgns_;
}

void Tournament::setStorePGN(bool v)
{
    store_pgns_ = v;
}

void Tournament::printElo() const
{
    Elo elo(wins_, losses_, draws_);
    Elo white_advantage(white_wins_, white_losses_, draws_);
    std::stringstream ss;

    // clang-format off
    ss << "--------------------------------------------------------\n"
       << "Score of " << engine_names_[0] << " vs " << engine_names_[1] << " after " << round_count_ * match_config_.games << " games: "
       << wins_ << " - " << losses_ << " - " << draws_
       << " (" << std::fixed << std::setprecision(2) << (float(wins_) + (float(draws_) * 0.5)) / (round_count_ * match_config_.games) << ")\n"
       << "Ptnml:   "
       << std::right << std::setw(7) << "WW"
       << std::right << std::setw(7) << "WD"
       << std::right << std::setw(7) << "DD/WL"
       << std::right << std::setw(7) << "LD"
       << std::right << std::setw(7) << "LL" << "\n"
       << "Distr:   "
       << std::right << std::setw(7) << penta_WW_
       << std::right << std::setw(7) << penta_WD_
       << std::right << std::setw(7) << penta_WL_
       << std::right << std::setw(7) << penta_LD_
       << std::right << std::setw(7) << penta_LL_ << "\n";
    // clang-format on

    if (sprt_.isValid())
    {
        ss << "LLR: " << sprt_.getLLR(wins_, draws_, losses_) << " " << sprt_.getBounds() << " "
           << sprt_.getElo() << "\n";
    }
    ss << std::setprecision(1) << "Stats:  "
       << "W: " << (float(wins_) / (round_count_ * match_config_.games)) * 100 << "%   "
       << "L: " << (float(losses_) / (round_count_ * match_config_.games)) * 100 << "%   "
       << "D: " << (float(draws_) / (round_count_ * match_config_.games)) * 100 << "%   "
       << "TF: " << timeouts_ << "\n";
    ss << "White advantage: " << white_advantage.getElo() << "\n";
    ss << "Elo difference: " << elo.getElo()
       << "\n--------------------------------------------------------\n";
    std::cout << ss.str();
}

void Tournament::writeToFile(const std::string &data)
{
    // Acquire the lock
    const std::lock_guard<std::mutex> lock(file_mutex_);

    file_ << data << std::endl;
}

bool Tournament::checkEngineStatus(UciEngine &engine, Match &match, int roundId) const
{
    if (!engine.checkErrors(roundId).empty())
    {
        match.needs_restart = match_config_.recover;
        return false;
    }
    return true;
}

bool Tournament::playNextMove(UciEngine &engine, std::string &positionInput, Board &board,
                              TimeControl &timeLeftUs, const TimeControl &timeLeftThem,
                              GameResult &res, Match &match, DrawAdjTracker &drawTracker,
                              ResignAdjTracker &resignTracker, int roundId)
{
    std::vector<std::string> output;
    output.reserve(30);

    bool timeout = false;

    // engine's turn
    if (!engine.isResponsive())
    {
        Logger::coutInfo("Warning: Engine", engine.getConfig().name,
                         "disconnects. It was not responsive.");

        if (!match_config_.recover)
        {
            throw std::runtime_error("Warning: Engine not responsive");
        }

        return false;
    }

    // Write new position
    engine.writeProcess(positionInput);
    if (!checkEngineStatus(engine, match, roundId))
        return false;

    engine.writeProcess(engine.buildGoInput(board.getSideToMove(), timeLeftUs, timeLeftThem));
    if (!checkEngineStatus(engine, match, roundId))
        return false;

    // Start measuring time
    const auto t0 = std::chrono::high_resolution_clock::now();

    output =
        engine.readProcess("bestmove", timeout, timeLeftUs.fixed_time != 0 ? 0 : timeLeftUs.time);

    const auto t1 = std::chrono::high_resolution_clock::now();

    if (!engine.getError().empty())
    {
        match.needs_restart = match_config_.recover;
        Logger::coutInfo("Warning: Engine", engine.getConfig().name, "disconnects #", roundId);

        if (!match_config_.recover)
        {
            throw std::runtime_error("Warning: Can't write to engine.");
        }

        return false;
    }

    // Subtract measured time
    const auto measuredTime =
        std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

    timeLeftUs.time -= measuredTime;

    if (engine.getConfig().plies == 0 && ((timeLeftUs.fixed_time != 0 &&
         measuredTime - match_config_.overhead > timeLeftUs.fixed_time) ||
        (timeLeftUs.fixed_time == 0 && timeLeftUs.time + match_config_.overhead < 0)))
    {
        res = GameResult(~board.getSideToMove());
        match.termination = "timeout";
        timeouts_++;
        Logger::coutInfo("Warning: Engine", engine.getConfig().name, "loses on time #", roundId);
        return false;
    }

    timeLeftUs.time += timeLeftUs.increment;

    // find bestmove and add it to the position string
    const auto bestMove =
        findElement<std::string>(CMD::Options::splitString(output.back(), ' '), "bestmove");

    if (match.moves.size() == 0)
        positionInput += " moves";
    positionInput += " " + bestMove;

    // play move on internal board and store it for later pgn creation
    match.legal = board.makeMove(convertUciToMove(board, bestMove));
    match.moves.emplace_back(parseEngineOutput(board, output, bestMove, measuredTime));

    if (!match.legal)
    {
        res = GameResult(~board.getSideToMove());
        match.termination = "illegal move";

        Logger::coutInfo("Warning: Engine", engine.getConfig().name,
                         "played an illegal move:", bestMove, "#", roundId);
        return false;
    }

    // Update Trackers
    updateTrackers(drawTracker, resignTracker, match.moves.back().score, match.moves.size());

    // Check for game over
    if ((res = board.isGameOver()) != GameResult::NONE ||
        (res = checkAdj(match, drawTracker, resignTracker, match.moves.back().score,
                        ~board.getSideToMove())) != GameResult::NONE)
        return false;

    return true;
}

Match Tournament::startMatch(UciEngine &engine1, UciEngine &engine2, int roundId,
                             std::string openingFen)
{
    ResignAdjTracker resignTracker = ResignAdjTracker(match_config_.resign.score, 0);
    DrawAdjTracker drawTracker = DrawAdjTracker(match_config_.draw.score, 0);
    GameResult res = GameResult::NONE;
    Match match = {};

    Board board = {};
    board.loadFen(openingFen);

    match.white_engine = board.getSideToMove() == WHITE ? engine1.getConfig() : engine2.getConfig();
    match.black_engine = board.getSideToMove() != WHITE ? engine1.getConfig() : engine2.getConfig();

    engine1.sendUciNewGame();
    engine2.sendUciNewGame();

    match.fen = board.getFen();
    match.start_time = save_time_header_ ? Logger::getDateTime() : "";
    match.date = save_time_header_ ? Logger::getDateTime("%Y-%m-%d") : "";

    std::string positionInput =
        board.getFen() == startpos_ ? "position startpos" : "position fen " + board.getFen();

    auto timeLeft_1 = engine1.getConfig().tc;
    auto timeLeft_2 = engine2.getConfig().tc;

    const auto start = std::chrono::high_resolution_clock::now();

    while (!pool_.stop_)
    {
        if (!playNextMove(engine1, positionInput, board, timeLeft_1, timeLeft_2, res, match,
                          drawTracker, resignTracker, roundId))
            break;

        if (!playNextMove(engine2, positionInput, board, timeLeft_2, timeLeft_1, res, match,
                          drawTracker, resignTracker, roundId))
            break;
    }

    const auto end = std::chrono::high_resolution_clock::now();

    match.round = roundId;
    match.result = res;
    match.end_time = save_time_header_ ? Logger::getDateTime() : "";
    match.duration =
        save_time_header_
            ? Logger::formatDuration(std::chrono::duration_cast<std::chrono::seconds>(end - start))
            : "";

    return match;
}

std::vector<Match> Tournament::runH2H(CMD::GameManagerOptions localMatchConfig,
                                      const std::vector<EngineConfiguration> &configs,
                                      const std::string &fen)
{
    // Initialize variables
    std::vector<Match> matches;

    UciEngine engine1, engine2;
    engine1.loadConfig(configs[0]);
    engine1.resetError();
    engine1.startEngine();

    engine2.loadConfig(configs[1]);
    engine2.resetError();
    engine2.startEngine();

    engine1.checkErrors(round_count_ + 1);
    engine2.checkErrors(round_count_ + 1);

    // engine1 always starts first
    engine1.turn = Turn::FIRST;
    engine2.turn = Turn::SECOND;

    const int games = localMatchConfig.games;

    int local_wins = 0;
    int local_losses = 0;
    int local_draws = 0;

    // Game stats from white pov for white advantage
    int white_local_wins = 0;
    int white_local_losses = 0;

    for (int i = 0; i < games; i++)
    {
        Match match;
        if (engine1.turn == Turn::FIRST)
            match = startMatch(engine1, engine2, round_count_ + 1, fen);
        else
            match = startMatch(engine2, engine1, round_count_ + 1, fen);

        if (match.needs_restart)
        {
            i--;
            engine1.restartEngine();
            engine2.restartEngine();
            continue;
        }

        total_count_++;
        matches.emplace_back(match);

        const std::string positive_engine =
            engine1.turn == Turn::FIRST ? engine1.getConfig().name : engine2.getConfig().name;
        const std::string negative_engine =
            engine1.turn == Turn::FIRST ? engine2.getConfig().name : engine1.getConfig().name;

        engine1.turn = ~engine1.turn;
        engine2.turn = ~engine2.turn;

        if (match.result == GameResult::WHITE_WIN)
        {
            local_wins += match.white_engine.name == configs[0].name ? 1 : 0;
            local_losses += match.white_engine.name == configs[0].name ? 0 : 1;
            white_local_wins += 1;
        }
        else if (match.result == GameResult::BLACK_WIN)
        {
            local_wins += match.black_engine.name == configs[0].name ? 1 : 0;
            local_losses += match.black_engine.name == configs[0].name ? 0 : 1;
            white_local_losses += 1;
        }
        else if (match.result == GameResult::DRAW)
        {
            local_draws++;
        }
        else
        {
            std::cout << "Couldn't obtain Game Result\n";
        }

        std::stringstream ss;
        ss << "Finished game " << i + 1 << "/" << games << " in round " << round_count_ + 1 << "/"
           << localMatchConfig.rounds << " total played " << total_count_ << "/"
           << localMatchConfig.rounds * games << " " << positive_engine << " vs " << negative_engine
           << ": " << resultToString(match.result) << "\n";

        std::cout << ss.str();
    }

    penta_WW_ += local_wins == 2 ? 1 : 0;
    penta_WD_ += local_wins == 1 && local_draws == 1 ? 1 : 0;
    penta_WL_ += (local_wins == 1 && local_losses == 1) || local_draws == 2 ? 1 : 0;
    penta_LD_ += local_losses == 1 && local_draws == 1 ? 1 : 0;
    penta_LL_ += local_losses == 2 ? 1 : 0;

    wins_ += local_wins;
    losses_ += local_losses;
    draws_ += local_draws;
    // Update white advantage stats
    white_wins_ += white_local_wins;
    white_losses_ += white_local_losses;

    round_count_++;

    if (round_count_ % localMatchConfig.ratinginterval == 0)
        printElo();

    // Write matches to file
    for (const auto &match : matches)
    {
        PgnBuilder pgn(match, match_config_);

        writeToFile(pgn.getPGN());
    }

    return matches;
}

void Tournament::startTournament(const std::vector<EngineConfiguration> &configs)
{
    if (configs.size() < 2)
    {
        throw std::runtime_error("Warning: Need at least two engines to start!");
    }

    Logger::coutInfo("Starting tournament...");

    std::vector<std::future<std::vector<Match>>> results;

    for (int i = 1; i <= match_config_.rounds; i++)
    {
        results.emplace_back(pool_.enqueue(
            std::bind(&Tournament::runH2H, this, match_config_, configs, fetchNextFen())));
    }

    engine_names_.push_back(configs[0].name);
    engine_names_.push_back(configs[1].name);

    while (sprt_.isValid() && !pool_.stop_)
    {
        const double llr = sprt_.getLLR(wins_, draws_, losses_);
        if (sprt_.getResult(llr) != SPRT_CONTINUE)
        {
            pool_.kill();
            std::cout << "Finished match\n";
            printElo();

            return;
        }

        std::this_thread::sleep_for(std::chrono::microseconds(250));
    }

    if (store_pgns_)
    {
        for (auto &&result : results)
        {
            const auto res = result.get();

            for (const Match &match : res)
            {
                PgnBuilder pgn(match, match_config_);
                pgns_.emplace_back(pgn.getPGN());
            }
        }
    }

    while (!pool_.stop_ && round_count_ < match_config_.rounds)
    {
        // prevent accessive atomic checks
        std::this_thread::sleep_for(std::chrono::microseconds(250));
    }

    std::cout << "Finished match\n";
    printElo();
}

void Tournament::stopPool()
{
    pool_.kill();
}

Stats Tournament::getStats() const
{
    return Stats(wins_, draws_, losses_, penta_WW_, penta_WD_, penta_WL_, penta_LD_, penta_LL_,
                 round_count_, total_count_, timeouts_);
}

void Tournament::setStats(const Stats &stats)
{
    wins_ = stats.wins;
    losses_ = stats.losses;
    draws_ = stats.draws;

    penta_WW_ = stats.penta_WW;
    penta_WD_ = stats.penta_WD;
    penta_WL_ = stats.penta_WL;
    penta_LD_ = stats.penta_LD;
    penta_LL_ = stats.penta_LL;

    round_count_ = stats.round_count;
    total_count_ = stats.total_count;
    timeouts_ = stats.timeouts;
}

MoveData Tournament::parseEngineOutput(const Board &board, const std::vector<std::string> &output,
                                       const std::string &move, int64_t measuredTime) const
{
    std::string score_string = "0.00";
    uint64_t nodes = 0;
    int score = 0;
    int depth = 0;

    // extract last info line
    if (output.size() > 1)
    {
        const auto info = CMD::Options::splitString(output[output.size() - 2], ' ');

        std::string scoreType = findElement<std::string>(info, "score");
        depth = findElement<int>(info, "depth");
        nodes = findElement<uint64_t>(info, "nodes");

        if (scoreType == "cp")
        {
            score = findElement<int>(info, "cp");

            std::stringstream ss;
            ss << (score >= 0 ? '+' : '-');
            ss << std::fixed << std::setprecision(2) << (float(std::abs(score)) / 100);
            score_string = ss.str();
        }
        else if (scoreType == "mate")
        {
            score = findElement<int>(info, "mate");
            score_string = (score > 0 ? "+M" : "-M") + std::to_string(std::abs(score));
            score = mate_score_;
        }
    }

    // verify pv
    for (const auto &info : output)
    {
        auto tmp = board;
        const auto tokens = CMD::Options::splitString(info, ' ');

        if (!CMD::Options::contains(tokens, "moves"))
            continue;

        std::size_t index = std::find(tokens.begin(), tokens.end(), "pv") - tokens.begin();
        index++;
        for (; index < tokens.size(); index++)
        {
            if (!tmp.makeMove(convertUciToMove(tmp, tokens[index])))
            {
                std::stringstream ss;
                ss << "Warning: Illegal pv move " << tokens[index] << ".\n";
                std::cout << ss.str();
                break;
            };
        }
    }

    return MoveData(move, score_string, measuredTime, depth, score, nodes);
}

void Tournament::updateTrackers(DrawAdjTracker &drawTracker, ResignAdjTracker &resignTracker,
                                const Score moveScore, const int move_number) const
{
    // Score is low for draw adj, increase the counter
    if (move_number >= match_config_.draw.move_number && abs(moveScore) < drawTracker.draw_score)
    {
        drawTracker.move_count++;
    }
    // Score wasn't low enough for draw adj, since we care about consecutive
    // moves we have to reset the counter
    else
    {
        drawTracker.move_count = 0;
    }

    // Score is low for resign adj, increase the counter (this purposely makes
    // it possible that a move can work for both draw and resign adj for
    // whatever reason that might be the case)
    if (abs(moveScore) > resignTracker.resign_score)
    {
        resignTracker.move_count++;
    }
    else
    {
        resignTracker.move_count = 0;
    }
}

GameResult Tournament::checkAdj(Match &match, const DrawAdjTracker &drawTracker,
                                const ResignAdjTracker &resignTracker, const Score score,
                                const Color lastSideThatMoved) const

{
    // Check draw adj
    if (match_config_.draw.enabled && drawTracker.move_count >= match_config_.draw.move_count)
    {
        match.termination = "adjudication";
        return GameResult::DRAW;
    }

    // Check Resign adj
    if (match_config_.resign.enabled &&
        resignTracker.move_count >= match_config_.resign.move_count && score != mate_score_)
    {
        match.termination = "adjudication";

        // We have the Score for the last side that moved, if it's bad that side
        // is the resigning one so give the other side the win.
        return score < resignTracker.resign_score ? GameResult(~lastSideThatMoved)
                                                  : GameResult(lastSideThatMoved);
    }

    return GameResult::NONE;
}

} // namespace fast_chess
