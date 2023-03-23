#include "matchmaking/tournament.hpp"

#include <vector>

#include "elo.hpp"
#include "matchmaking/match.hpp"
#include "pgn_builder.hpp"
#include "rand.hpp"
#include "tournament.hpp"

namespace fast_chess {

Tournament::Tournament(const CMD::GameManagerOptions &game_config) {
    // Open the pgn file
    const std::string filename =
        (game_config.pgn.file.empty() ? "fast-chess" : game_config.pgn.file) + ".pgn";

    file_.open(filename, std::ios::app);

    loadConfig(game_config);
}

void Tournament::loadConfig(const CMD::GameManagerOptions &game_config) {
    this->game_config_ = game_config;

    // Set the seed for the random number generator
    Random::mersenne_rand.seed(game_config_.seed);

    // Read the opening book from file
    if (!game_config_.opening.file.empty()) {
        std::ifstream openingFile;
        std::string line;
        openingFile.open(game_config_.opening.file);

        while (std::getline(openingFile, line)) {
            opening_book_.emplace_back(line);
        }

        openingFile.close();

        if (game_config_.opening.order == "random") {
            // Fisher-Yates / Knuth shuffle
            for (std::size_t i = 0; i <= opening_book_.size() - 2; i++) {
                std::size_t j = i + (Random::mersenne_rand() % (opening_book_.size() - i));
                std::swap(opening_book_[i], opening_book_[j]);
            }
        }
    }

    // Initialize the thread pool
    pool_.resize(game_config_.concurrency);

    // Initialize the SPRT test
    sprt_ = SPRT(game_config_.sprt.alpha, game_config_.sprt.beta, game_config_.sprt.elo0,
                 game_config_.sprt.elo1);
}

void Tournament::stop() { pool_.kill(); }

void Tournament::printElo(const std::string &first, const std::string &second) {
    if (engine_count != 2) return;

    Stats stats;
    {
        const std::unique_lock<std::mutex> lock(results_mutex_);

        if (results_.find(first) != results_.end())
            stats = results_[first][second];
        else
            stats = results_[second][first];
    }

    Elo elo(stats.wins, stats.losses, stats.draws);
    // Elo white_advantage(white_stats.wins, white_stats.losses, stats.draws);
    std::stringstream ss;

    int64_t games = stats.sum();

    // clang-format off
    ss << "--------------------------------------------------------\n"
       << "Score of " << first << " vs " <<second << " after " << games << " games: "
       << stats.wins << " - " << stats.losses << " - " << stats.draws;

    if (game_config_.report_penta)
    {
       ss
       << " (" << std::fixed << std::setprecision(2) << (float(stats.wins) + (float(stats.draws) * 0.5)) / games << ")\n"
       << "Ptnml:   "
       << std::right << std::setw(7) << "WW"
       << std::right << std::setw(7) << "WD"
       << std::right << std::setw(7) << "DD/WL"
       << std::right << std::setw(7) << "LD"
       << std::right << std::setw(7) << "LL" << "\n"
       << "Distr:   "
       << std::right << std::setw(7) << stats.penta_WW
       << std::right << std::setw(7) << stats.penta_WD
       << std::right << std::setw(7) << stats.penta_WL
       << std::right << std::setw(7) << stats.penta_LD
       << std::right << std::setw(7) << stats.penta_LL << "\n";
    }
    else
    {
        ss << "\n";
    }
    // clang-format on

    ss << std::fixed;

    if (sprt_.isValid()) {
        ss << "LLR: " << sprt_.getLLR(stats.wins, stats.draws, stats.losses) << " "
           << sprt_.getBounds() << " " << sprt_.getElo() << "\n";
    }

    ss << std::setprecision(1) << "Stats:  "
       << "W: " << (float(stats.wins) / games) * 100 << "%   "
       << "L: " << (float(stats.losses) / games) * 100 << "%   "
       << "D: " << (float(stats.draws) / games) * 100 << "%   "
       << "TF: " << timeouts_ << "\n";
    // ss << "White advantage: " << white_advantage.getElo() << "\n";
    ss << "Elo difference: " << elo.getElo()
       << "\n--------------------------------------------------------\n";
    std::cout << ss.str();
}

void Tournament::startTournament(const std::vector<EngineConfiguration> &engine_configs) {
    validateConfig(engine_configs);

    Logger::coutInfo("Starting tournament...");

    std::vector<std::future<bool>> results;

    createRoundRobin(engine_configs, results);

    if (runSprt(engine_configs)) return;

    for (auto &&result : results) {
        if (!result.get()) throw std::runtime_error("Unknown error during match playing.");
    }

    std::cout << "Finished match\n";

    if (engine_configs.size() == 2) printElo(engine_configs[0].name, engine_configs[1].name);
}

bool Tournament::runSprt(const std::vector<EngineConfiguration> &engine_configs) {
    while (engine_configs.size() == 2 && sprt_.isValid() && match_count_ < total_count_ &&
           !pool_.getStop()) {
        Stats stats = getResults(engine_configs[0].name, engine_configs[1].name);

        const double llr = sprt_.getLLR(stats.wins, stats.draws, stats.losses);

        if (sprt_.getResult(llr) != SPRT_CONTINUE) {
            pool_.kill();
            std::cout << "Finished match\n";
            printElo(engine_configs[0].name, engine_configs[1].name);

            return true;
        }

        std::this_thread::sleep_for(std::chrono::microseconds(250));
    }
    return false;
}

void Tournament::validateConfig(const std::vector<EngineConfiguration> &configs) {
    if (configs.size() < 2) {
        throw std::runtime_error("Warning: Need at least two engines to start!");
    }

    for (size_t i = 0; i < configs.size(); i++) {
        for (size_t j = 0; j < i; j++) {
            if (configs[i].name == configs[j].name) {
                throw std::runtime_error("Engine with the same are not allowed!: " +
                                         configs[i].name);
            }
        }
    }

    if (game_config_.games > 2)
        throw std::runtime_error("Exceeded -game limit! Must be smaller than 2");

    engine_count = configs.size();
}

void Tournament::createRoundRobin(const std::vector<EngineConfiguration> &engine_configs,
                                  std::vector<std::future<bool>> &results) {
    bool reverse = game_config_.games == 2;
    if (!game_config_.report_penta) {
        game_config_.games = 1;
    }

    // Round robin
    total_count_ = (engine_configs.size() * (engine_configs.size() - 1) / 2) * game_config_.rounds *
                   game_config_.games;

    // Round robin
    for (std::size_t i = 0; i < engine_configs.size(); i++) {
        for (std::size_t j = i + 1; j < engine_configs.size(); j++) {
            // Initialize results entry, if we there isnt already a valid stats entry in the results
            // map we must create one so that we properly report the score from engine_configs[i]
            // perspective!
            auto player1 = engine_configs[i];
            auto player2 = engine_configs[j];
            auto &stats_map = results_[player1.name];
            if (stats_map.find(player2.name) == stats_map.end()) {
                stats_map.emplace(player2.name, Stats{});
            }

            // add json loaded games to current match count
            auto sum = results_[player1.name][player2.name].sum();
            match_count_ += sum;

            for (int n = 1 + sum / game_config_.games; n <= game_config_.rounds; n++) {
                auto fen = fetchNextFen();
                results.emplace_back(pool_.enqueue(std::bind(
                    &Tournament::launchMatch, this, std::make_pair(player1, player2), fen, n)));

                // We need to play reverse games but shall not collect penta stats.
                if (!game_config_.report_penta && reverse) {
                    results.emplace_back(pool_.enqueue(std::bind(
                        &Tournament::launchMatch, this, std::make_pair(player2, player1), fen, n)));
                }
            }
        }
    }
}

Stats Tournament::getResults(const std::string &engine1, const std::string &engine2) {
    const std::unique_lock<std::mutex> lock(results_mutex_);
    return results_[engine1][engine2];
}

std::string Tournament::fetchNextFen() {
    if (opening_book_.size() == 0) {
        return startpos_;
    } else if (game_config_.opening.format == "pgn") {
        // todo: implementation
    } else if (game_config_.opening.format == "epd") {
        return opening_book_[(game_config_.opening.start + fen_index_++) % opening_book_.size()];
    }

    return startpos_;
}

void Tournament::writeToFile(const std::string &data) {
    // Acquire the lock
    const std::lock_guard<std::mutex> lock(file_mutex_);

    file_ << data << std::endl;
}

void fast_chess::Tournament::updateStats(const std::string &us, const std::string &them,
                                         const Stats &stats) {
    const std::unique_lock<std::mutex> lock(results_mutex_);

    if (results_.find(us) != results_.end())
        results_[us][them] += stats;
    else if (results_.find(them) != results_.end())
        // store engine2 vs engine1 to the engine1 vs engine2 entry.
        // For this we need to invert the scores
        results_[them][us] += ~stats;
    else
        results_[us][them] += stats;
}

bool Tournament::launchMatch(const std::pair<EngineConfiguration, EngineConfiguration> &configs,
                             const std::string &fen, int round_id) {
    std::vector<MatchData> matches;

    std::string result = "*";

    Stats stats;

    auto config_copy = configs;

    for (int i = 0; i < game_config_.games; i++) {
        Match match = Match(game_config_, config_copy.first, config_copy.second);
        match.playMatch(fen);

        MatchData match_data = match.getMatchData();
        match_data.round = round_id;

        // If the match needs to be restarted, decrement the match count and restart the match.
        if (match_data.needs_restart && game_config_.recover) {
            i--;
            continue;
        }

        matches.push_back(match_data);

        if (match_data.players.first.score == GameResult::WIN) {
            stats.wins += match_data.players.first == configs.first;
            stats.losses += match_data.players.first != configs.first;
        }

        if (match_data.players.first.score == GameResult::LOSE) {
            stats.wins += match_data.players.first != configs.first;
            stats.losses += match_data.players.first == configs.first;
        }

        // If the game was a draw, increment the draw count.
        if (match_data.players.first.score == GameResult::DRAW) stats.draws++;

        // If the game timed out, increment the timeout count.
        if (match_data.termination == "timeout") timeouts_++;

        match_count_++;

        std::swap(config_copy.first, config_copy.second);

        std::stringstream ss;
        // clang-format off
        ss << "Finished game " 
           << i + 1 
           << "/" 
           << game_config_.games 
           << " in round " 
           << round_id 
           << "/" 
           << game_config_.rounds 
           << " total played " 
           << match_count_ 
           << "/" 
           << total_count_
           << " " 
           << match_data.players.first.config.name 
           << " vs "
           << match_data.players.second.config.name 
           << ": " 
           << resultToString(match_data) 
           << "\n";
        // clang-format on

        std::cout << ss.str();
    }

    round_count_++;

    if ((game_config_.games == 2 ? round_count_ : match_count_) % game_config_.ratinginterval == 0)
        printElo(configs.first.name, configs.second.name);

    stats.penta_WW += stats.wins == 2;
    stats.penta_WD += stats.wins == 1 && stats.draws == 1;
    stats.penta_WL += (stats.wins == 1 && stats.losses == 1) || stats.draws == 2;
    stats.penta_LD += stats.losses == 1 && stats.draws == 1;
    stats.penta_LL += stats.losses == 2;

    updateStats(configs.first.name, configs.second.name, stats);

    for (const auto &played_matches : matches) {
        PgnBuilder pgn(played_matches, game_config_, true);

// not thread safe and is only used for unit tests.
#ifdef TESTS
        pgns_.push_back(pgn.getPGN());
#endif  // TESTS

        writeToFile(pgn.getPGN());
    }

    return true;
}

}  // namespace fast_chess
