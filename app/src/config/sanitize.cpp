#include "sanitize.hpp"

#include <algorithm>
#include <limits>
#include <random>
#include <thread>
#include <utility>
#include <vector>

#include <util/file_system.hpp>
#include <util/logger/logger.hpp>

namespace fastchess::config {

void sanitize(config::Tournament& config) {
    if (config.games > 2) {
        // wrong config, lets try to fix it
        std::swap(config.games, config.rounds);

        if (config.games > 2) {
            throw std::runtime_error("Error: Exceeded -game limit! Must be less than 2");
        }
    }

    // fix wrong config
    if (config.report_penta && config.output == OutputType::CUTECHESS) config.report_penta = false;

    if (config.report_penta && config.games != 2) config.report_penta = false;

    // throw error for invalid sprt config
    if (config.sprt.enabled) {
        if (config.sprt.elo0 >= config.sprt.elo1) {
            throw std::runtime_error("Error; SPRT: elo0 must be less than elo1!");
        } else if (config.sprt.alpha <= 0 || config.sprt.alpha >= 1) {
            throw std::runtime_error("Error; SPRT: alpha must be a decimal number between 0 and 1!");
        } else if (config.sprt.beta <= 0 || config.sprt.beta >= 1) {
            throw std::runtime_error("Error; SPRT: beta must be a decimal number between 0 and 1!");
        } else if (config.sprt.alpha + config.sprt.beta >= 1) {
            throw std::runtime_error("Error; SPRT: sum of alpha and beta must be less than 1!");
        } else if (config.sprt.model != "normalized" && config.sprt.model != "bayesian" &&
                   config.sprt.model != "logistic") {
            throw std::runtime_error("Error; SPRT: invalid SPRT model!");
        }

        if (config.sprt.model == "bayesian" && config.report_penta) {
            Logger::warn(
                "Warning: Bayesian SPRT model not available with pentanomial statistics. Disabling "
                "pentanomial reports...");
            config.report_penta = false;
        }
    }

    if (config.concurrency > static_cast<int>(std::thread::hardware_concurrency()) && !config.force) {
        throw std::runtime_error("Error: Concurrency exceeds number of CPUs. Use -force to override.");
    }

    if (config.variant == VariantType::FRC && config.opening.file.empty()) {
        throw std::runtime_error("Error: Please specify a Chess960 opening book");
    }

    if (config.opening.file.empty()) {
        Logger::warn(
            "Warning: No opening book specified! Consider using one, otherwise all games will be "
            "played from the starting position.");
    }

    if (config.opening.format != FormatType::EPD && config.opening.format != FormatType::PGN) {
        Logger::warn("Warning: Unknown opening format, {}. All games will be played from the starting position.",
                     int(config.opening.format));
    }

    if (config.ratinginterval == 0) config.ratinginterval = std::numeric_limits<int>::max();

    if (config.scoreinterval == 0) config.scoreinterval = std::numeric_limits<int>::max();
}

void sanitize(std::vector<EngineConfiguration>& configs) {
    if (configs.size() < 2) {
        throw std::runtime_error("Error: Need at least two engines to start!");
    }

    if (configs.size() > 2) {
        throw std::runtime_error("Error: Exceeded -engine limit! Must be 2!");
    }

    for (std::size_t i = 0; i < configs.size(); i++) {
#ifdef _WIN64
        // add .exe if . is not present
        if (configs[i].cmd.find('.') == std::string::npos) {
            configs[i].cmd += ".exe";
        }
#endif

#ifndef NO_STD_FILESYSTEM
        // convert path to a filesystem path
        auto p    = std::filesystem::path(configs[i].dir) / std::filesystem::path(configs[i].cmd);
        auto path = p.string();
        if (!std::filesystem::exists(path)) {
            throw std::runtime_error("Engine not found at: " + path);
        }
#endif

        if (configs[i].name.empty()) {
            throw std::runtime_error("Error; please specify a name for each engine!");
        }

        if (configs[i].limit.tc.time + configs[i].limit.tc.increment == 0 && configs[i].limit.tc.fixed_time == 0 &&
            configs[i].limit.nodes == 0 && configs[i].limit.plies == 0) {
            throw std::runtime_error("Error; no TimeControl specified!");
        }

        if (((configs[i].limit.tc.time + configs[i].limit.tc.increment != 0) + (configs[i].limit.tc.fixed_time != 0)) >
            1) {
            throw std::runtime_error("Error; cannot use tc and st together!");
        }

        if (configs[i].limit.tc.time + configs[i].limit.tc.increment == 0 && configs[i].limit.tc.moves != 0) {
            throw std::runtime_error("Error; invalid TimeControl!");
        }

        for (std::size_t j = 0; j < i; j++) {
            if (configs[i].name == configs[j].name) {
                throw std::runtime_error("Error: Engine with the same name are not allowed!: " + configs[i].name);
            }
        }
    }
}

}  // namespace fastchess::config
