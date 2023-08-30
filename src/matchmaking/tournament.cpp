#include <matchmaking/tournament.hpp>

#include <logger.hpp>
#include <rand.hpp>

namespace fast_chess {

Tournament::Tournament(const cmd::TournamentOptions& game_config) : round_robin_(game_config) {
    loadConfig(game_config);
}

void Tournament::loadConfig(const cmd::TournamentOptions& game_config) {
    tournament_options_ = game_config;

    if (tournament_options_.games > 2) {
        // wrong config, lets try to fix it
        std::swap(tournament_options_.games, tournament_options_.rounds);

        if (tournament_options_.games > 2) {
            throw std::runtime_error("Error: Exceeded -game limit! Must be less than 2");
        }
    }

    if (game_config.report_penta && game_config.output == OutputType::CUTECHESS)
        tournament_options_.report_penta = false;

    round_robin_.setGameConfig(tournament_options_);
}

void Tournament::validateEngines(std::vector<EngineConfiguration>& configs) {
    if (configs.size() < 2) {
        throw std::runtime_error("Error: Need at least two engines to start!");
    }

    for (std::size_t i = 0; i < configs.size(); i++) {
        for (std::size_t j = 0; j < i; j++) {
            if (configs[i].name == configs[j].name) {
                throw std::runtime_error("Error: Engine with the same name are not allowed!: " +
                                         configs[i].name);
            }
        }
    }
}

void Tournament::start(std::vector<EngineConfiguration> engine_configs) {
    validateEngines(engine_configs);

    Logger::cout("Starting tournament...");

    round_robin_.start(engine_configs);

    Logger::cout("Finished tournament\nSaving results...");
}

}  // namespace fast_chess