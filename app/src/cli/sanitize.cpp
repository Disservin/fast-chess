#include "sanitize.hpp"

#include <algorithm>
#include <limits>
#include <thread>
#include <utility>
#include <vector>

#include <core/filesystem/fd_limit.hpp>
#include <core/logger/logger.hpp>
#include <matchmaking/sprt/sprt.hpp>
#include <types/exception.hpp>

#include <fmt/include/fmt/core.h>

namespace fastchess::cli {

namespace {

void fixConfig(config::Tournament& config) {
    if (config.games > 2) {
        // wrong config, lets try to fix it
        std::swap(config.games, config.rounds);

        if (config.games > 2) {
            throw fastchess_exception("Error: Exceeded -game limit! Must be less than 2");
        }
    }

    // fix wrong config
    if (config.report_penta && config.output == OutputType::CUTECHESS) config.report_penta = false;

    if (config.report_penta && config.games != 2) config.report_penta = false;
}

void setDefaults(config::Tournament& config) {
    if (config.ratinginterval == 0) config.ratinginterval = std::numeric_limits<int>::max();
    if (config.scoreinterval == 0) config.scoreinterval = std::numeric_limits<int>::max();
}

void adjustConcurrency(config::Tournament& config) {
    if (config.concurrency <= 0) {
        config.concurrency = static_cast<int>(std::thread::hardware_concurrency()) - std::abs(config.concurrency);
        Logger::print<Logger::Level::INFO>(
            "Info: Adjusted concurrency to {} based on number of available hardware threads.", config.concurrency);
    }

    if (config.concurrency > static_cast<int>(std::thread::hardware_concurrency()) && !config.force_concurrency) {
        throw fastchess_exception("Error: Concurrency exceeds number of CPUs. Use -force-concurrency to override.");
    }

#ifdef _WIN64
    if (config.concurrency > 63 && !fd_limit::isWindows11OrNewer()) {
        Logger::print<Logger::Level::WARN>(
            "A concurrency setting of more than 63 is currently not supported on Windows.\nIf this affects "
            "your system, please open an issue or get in touch with the maintainers.");
        config.concurrency = 63;  // not 64 because we need one thread for the main thread
    }
#else
    if (fd_limit::maxSystemFileDescriptorCount() < fd_limit::minFileDescriptorRequired(config.concurrency)) {
        Logger::print<Logger::Level::WARN>(
            "There aren't enough file descriptors available for the specified concurrency.\nPlease increase the limit "
            "using ulimit -n 65536 for each shell manually or \nadjust the defaults (e.g. /etc/security/limits.conf,"
            "/etc/systemd/system.conf, and/or /etc/systemd/user.conf).\nThe maximum number of file descriptors "
            "required for this configuration is: {}",
            fd_limit::minFileDescriptorRequired(config.concurrency));

        const auto max_supported_concurrency = fd_limit::maxConcurrency(fd_limit::maxSystemFileDescriptorCount());

        Logger::print<Logger::Level::WARN>("Limiting concurrency to: {}", max_supported_concurrency);

        if (max_supported_concurrency < 1) {
            throw fastchess_exception("Error: Not enough file descriptors available for the specified concurrency.");
        }

        config.concurrency = max_supported_concurrency;
    }
#endif
}

void validateConfig(config::Tournament& config) {
    // throw error for invalid sprt config
    if (config.sprt.enabled) {
        SPRT::isValid(config.sprt.alpha, config.sprt.beta, config.sprt.elo0, config.sprt.elo1, config.sprt.model,
                      config.report_penta);
    }

    if (config.variant == VariantType::FRC && config.opening.file.empty()) {
        throw fastchess_exception("Error: Please specify a Chess960 opening book");
    }

    if (config.opening.file.empty()) {
        Logger::print<Logger::Level::WARN>(
            "Warning: No opening book specified! Consider using one, otherwise all games will be "
            "played from the starting position.");
    }

    if (config.opening.format != FormatType::EPD && config.opening.format != FormatType::PGN) {
        Logger::print<Logger::Level::WARN>(
            "Warning: Unknown opening format, {}. All games will be played from the starting position.",
            int(config.opening.format));
    }

    if (config.tb_adjudication.enabled) {
        if (config.tb_adjudication.syzygy_dirs.empty()) {
            throw fastchess_exception("Error: Must provide a ;-separated list of Syzygy tablebase directories.");
        }
    }
}

void validateEngine(EngineConfiguration& config) {
#ifdef _WIN64
    // add .exe if . is not present
    if (config.cmd.find('.') == std::string::npos) {
        config.cmd += ".exe";
    }
#endif

    if ((config.limit.tc.time + config.limit.tc.increment) == 0 && config.limit.tc.fixed_time == 0 &&
        config.limit.nodes == 0 && config.limit.plies == 0) {
        throw fastchess_exception("Error; no TimeControl specified!");
    }

    if ((((config.limit.tc.time + config.limit.tc.increment) != 0) + (config.limit.tc.fixed_time != 0)) > 1) {
        throw fastchess_exception("Error; cannot use tc and st together!");
    }

    auto enginePath = config.getEnginePath();

    if (!config.dir.empty() || enginePath.is_absolute()) {
        if (!std::filesystem::is_regular_file(enginePath)) {
            throw fastchess_exception::format("Engine binary does not exist: {}", enginePath.string());
        }
    }

    if (config.name.empty()) {
        // strip directory and extension
        std::filesystem::path p(config.cmd);
        config.name = p.stem().string();
    }

    if (config.name.empty()) {
        throw fastchess_exception("Error; please specify a name for each engine!");
    }
}

}  // namespace

void sanitize(config::Tournament& config) {
    fixConfig(config);
    setDefaults(config);
    adjustConcurrency(config);
    validateConfig(config);
}

void sanitize(std::vector<EngineConfiguration>& configs) {
    if (configs.size() < 2) {
        throw fastchess_exception("Error: Need at least two engines to start!");
    }

    for (std::size_t i = 0; i < configs.size(); i++) {
        validateEngine(configs[i]);
    }

    std::unordered_map<std::string, int> seen;
    for (auto& config : configs) {
        int& n = seen[config.name];

        if (n++ > 0) {
            config.name = fmt::format("{}_{}", config.name, n);
        }
    }
}

}  // namespace fastchess::cli
