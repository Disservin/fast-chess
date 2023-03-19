#include "matchmaking/tournament.hpp"

#include <vector>

#include "elo.hpp"
#include "matchmaking/match.hpp"
#include "pgn_builder.hpp"
#include "rand.hpp"
#include "tournament.hpp"

namespace fast_chess
{

Tournament::Tournament(const CMD::GameManagerOptions &game_config)
{
    const std::string filename =
        (game_config.pgn.file.empty() ? "fast-chess" : game_config.pgn.file) + ".pgn";

    file_.open(filename, std::ios::app);

    loadConfig(game_config);
}

void Tournament::loadConfig(const CMD::GameManagerOptions &game_config)
{
    game_config_ = game_config;

    Random::mersenne_rand.seed(game_config_.seed);

    if (!game_config_.opening.file.empty())
    {
        std::ifstream openingFile;
        std::string line;
        openingFile.open(game_config_.opening.file);

        while (std::getline(openingFile, line))
        {
            opening_book_.emplace_back(line);
        }

        openingFile.close();

        if (game_config_.opening.order == "random")
        {
            // Fisher-Yates / Knuth shuffle
            for (std::size_t i = 0; i <= opening_book_.size() - 2; i++)
            {
                std::size_t j = i + (Random::mersenne_rand() % (opening_book_.size() - i));
                std::swap(opening_book_[i], opening_book_[j]);
            }
        }
    }

    pool_.resize(game_config_.concurrency);

    sprt_ = SPRT(game_config_.sprt.alpha, game_config_.sprt.beta, game_config_.sprt.elo0,
                 game_config_.sprt.elo1);
}

void Tournament::stop()
{
    pool_.kill();
}

void Tournament::printElo(const std::string &first, const std::string &second)
{
    Stats stats;
    {
        const std::unique_lock<std::mutex> lock(results_mutex_);

        stats = results_[first][second];
    }

    Elo elo(stats.wins, stats.losses, stats.draws);
    // Elo white_advantage(white_stats.wins, white_stats.losses, stats.draws);
    std::stringstream ss;

    int64_t games = total_count_;

    // clang-format off
    ss << "--------------------------------------------------------\n"
       << "Score of " << first << " vs " <<second << " after " << games << " games: "
       << stats.wins << " - " << stats.losses << " - " << stats.draws
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
    // clang-format on

    if (sprt_.isValid())
    {
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

void Tournament::startTournament(const std::vector<EngineConfiguration> &engine_configs)
{
    if (engine_configs.size() < 2)
    {
        throw std::runtime_error("Warning: Need at least two engines to start!");
    }

    Logger::coutInfo("Starting tournament...");

    std::vector<std::future<bool>> results;

    // Round robin
    for (std::size_t i = 0; i < engine_configs.size(); i++)
    {
        for (std::size_t j = i + 1; j < engine_configs.size(); j++)
        {
            for (int n = 1; n <= game_config_.rounds; n++)
            {
                std::cout << engine_configs[i].name << " vs " << engine_configs[j].name
                          << std::endl;
                results.emplace_back(pool_.enqueue(std::bind(
                    &Tournament::launchMatch, this,
                    std::make_pair(engine_configs[i], engine_configs[j]), fetchNextFen())));
            }
        }
    }

    for (auto &&result : results)
    {
        bool res = result.get();
    }

    if (engine_configs.size() == 2)
        printElo(engine_configs[0].name, engine_configs[1].name);
}

std::string Tournament::fetchNextFen()
{
    if (opening_book_.size() == 0)
    {
        return startpos_;
    }
    else if (game_config_.opening.format == "pgn")
    {
        // todo: implementation
    }
    else if (game_config_.opening.format == "epd")
    {
        return opening_book_[(game_config_.opening.start + fen_index_++) % opening_book_.size()];
    }

    return startpos_;
}

void Tournament::writeToFile(const std::string &data)
{
    // Acquire the lock
    const std::lock_guard<std::mutex> lock(file_mutex_);

    file_ << data << std::endl;
}

void fast_chess::Tournament::updateStats(const std::string &us, const std::string &them,
                                         const Stats &stats)
{
    const std::unique_lock<std::mutex> lock(results_mutex_);

    results_[us][them] += stats;
}

bool Tournament::launchMatch(const std::pair<EngineConfiguration, EngineConfiguration> &configs,
                             const std::string &fen)
{
    std::vector<MatchData> matches;

    std::string result = "*";

    Stats stats;

    auto config_copy = configs;

    for (int i = 0; i < game_config_.games; i++)
    {
        total_count_++;

        Match match = Match(game_config_, config_copy.first, config_copy.second);
        match.playMatch(fen);

        MatchData match_data = match.getMatchData();

        if (match_data.players.first.score == GameResult::WIN)
        {
            if (match_data.players.first == configs.first)
            {
                stats.wins++;
            }
            else
            {
                stats.losses++;
            }
        }

        if (match_data.players.first.score == GameResult::LOSE)
        {
            if (match_data.players.first == configs.first)
            {
                stats.losses++;
            }
            else
            {
                stats.wins++;
            }
        }

        if (match_data.players.first.score == GameResult::DRAW)
        {
            stats.draws++;
        }

        std::stringstream ss;
        ss << "Finished game " << i + 1 << "/" << game_config_.games << " in round " << round_count_
           << "/" << game_config_.rounds << " total played " << total_count_ << "/" << total_count_
           << " " << match_data.players.first.config.name << " vs "
           << match_data.players.second.config.name << ": " << resultToString(match_data) << "\n";

        std::cout << ss.str();

        matches.push_back(match_data);
        std::swap(config_copy.first, config_copy.second);

        printElo(configs.first.name, configs.second.name);
    }

    updateStats(configs.first.name, configs.second.name, stats);

    for (const auto &played_matches : matches)
    {
        PgnBuilder pgn(played_matches, game_config_, true);

        writeToFile(pgn.getPGN());
    }

    return true;
}

} // namespace fast_chess
