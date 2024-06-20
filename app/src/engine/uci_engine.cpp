#include <engine/uci_engine.hpp>

#include <stdexcept>
#include <string>
#include <vector>

#include <util/file_system.hpp>
#include <util/helper.hpp>
#include <util/logger/logger.hpp>

namespace fast_chess::engine {

bool UciEngine::isResponsive(std::chrono::milliseconds threshold) {
    try {
        if (!alive()) return false;

        Logger::trace<true>("Pinging engine {}", config_.name);

        writeEngine("isready");

        std::vector<process::Line> output;
        const auto res = readProcess(output, "readyok", threshold);

        for (const auto &line : output) {
            Logger::readFromEngine(line.line, line.time, config_.name, line.std == process::Standard::ERR);
        }

        if (res != process::Status::OK) {
            Logger::trace<true>("Engine {} didn't respond to isready.", config_.name);
            Logger::warn<true>("Warning; Engine {} is not responsive.", config_.name);
        }

        Logger::trace<true>("Engine {} is {}", config_.name,
                            res == process::Status::OK ? "responsive." : "not responsive.");

        return res == process::Status::OK;

    } catch (const std::exception &e) {
        Logger::trace<true>("Raised Exception in isResponsive: {}", e.what());

        return false;
    }
}

bool UciEngine::ucinewgame() {
    try {
        Logger::trace<true>("Sending ucinewgame to engine {}", config_.name);
        auto res = writeEngine("ucinewgame");

        if (!res) {
            Logger::trace<true>("Failed to send ucinewgame to engine {}", config_.name);
            return false;
        }

        return isResponsive(initialize_time);
    } catch (const std::exception &e) {
        Logger::trace<true>("Raised Exception in ucinewgame: {}", e.what());
        return false;
    }
}

bool UciEngine::uci() {
    Logger::trace<true>("Sending uci to engine {}", config_.name);
    const auto res = writeEngine("uci");

    if (!res) {
        Logger::trace<true>("Failed to send uci to engine {}", config_.name);
        return false;
    }

    return res;
}

bool UciEngine::uciok() {
    Logger::trace<true>("Waiting for uciok from engine {}", config_.name);

    const auto res = readEngine("uciok") == process::Status::OK;

    Logger::trace<true>("Engine {} did not respond to uciok in time.", config_.name);

    return res;
}

void UciEngine::loadConfig(const EngineConfiguration &config) { config_ = config; }

void UciEngine::quit() {
    Logger::trace<true>("Sending quit to engine {}", config_.name);
    writeEngine("quit");
}

void UciEngine::sendSetoption(const std::string &name, const std::string &value) {
    Logger::trace<true>("Sending setoption to engine {} {} {}", config_.name, name, value);
    if (!writeEngine(fmt::format("setoption name {} value {}", name, value))) {
        Logger::trace<true>("Failed to send setoption to engine {} {} {}", config_.name, name, value);
    }
}

void UciEngine::start() {
    if (initialized_) return;

    std::string path = (config_.dir == "." ? "" : config_.dir) + config_.cmd;

#ifndef NO_STD_FILESYSTEM
    // convert path to a filesystem path
    auto p = std::filesystem::path(config_.dir) / std::filesystem::path(config_.cmd);
    path   = p.string();
#endif

    Logger::trace<true>("Starting engine {} at {}", config_.name, path);

    init(path, config_.args, config_.name);
    if (!uci() || !uciok()) {
        throw std::runtime_error(fmt::format("{} failed to start.", config_.name));
    }

    initialized_ = true;
}

bool UciEngine::refreshUci() {
    Logger::trace<true>("Refreshing engine {}", config_.name);
    start();

    if (!ucinewgame()) {
        // restart the engine
        Logger::trace<true>("Engine {} failed to refresh. Restarting engine.", config_.name);

        restart();
        uci();

        if (!uciok()) {
            return false;
        }

        if (!ucinewgame()) {
            Logger::trace<true>("Engine {} responded to uci but not to ucinewgame/isready.", config_.name);
            return false;
        }
    }

    for (const auto &option : config_.options) {
        sendSetoption(option.first, option.second);
    }

    if (config_.variant == VariantType::FRC) {
        sendSetoption("UCI_Chess960", "true");
    }

    if (!ucinewgame()) {
        Logger::trace<true>("Engine {} didn't respond to ucinewgame.", config_.name);
        return false;
    }

    return true;
}

process::Status UciEngine::readEngine(std::string_view last_word, std::chrono::milliseconds threshold) {
    return readProcess(output_, last_word, threshold);
}

void UciEngine::writeLog() const {
    for (const auto &line : output_) {
        Logger::readFromEngine(line.line, line.time, config_.name, line.std == process::Standard::ERR);
    }
}

std::string UciEngine::lastInfoLine() const {
    // iterate backwards over the output and save the first line
    // that contains "info", "score" and "multipv 1" if it contains multipv
    for (auto it = output_.rbegin(); it != output_.rend(); ++it) {
        if (it->line.find("info") != std::string::npos && it->line.find(" score ") != std::string::npos &&
            (it->line.find(" multipv ") == std::string::npos || it->line.find(" multipv 1") != std::string::npos)) {
            return it->line;
        }
    }

    return {};
}

bool UciEngine::writeEngine(const std::string &input) {
    Logger::writeToEngine(input, util::time::datetime_precise(), config_.name);
    return writeProcess(input + "\n");
}

std::optional<std::string> UciEngine::bestmove() const {
    if (output_.empty()) {
        Logger::warn<true>("Warning; No output from {}", config_.name);
        return std::nullopt;
    }

    const auto bm = str_utils::findElement<std::string>(str_utils::splitString(output_.back().line, ' '), "bestmove");

    if (!bm.has_value()) {
        Logger::warn<true>("Warning; No bestmove found in the last line from {}", config_.name);
        return std::nullopt;
    }

    return bm.value();
}

std::vector<std::string> UciEngine::lastInfo() const {
    const auto last_info = lastInfoLine();
    if (last_info.empty()) {
        Logger::warn<true>(
            "Warning; No info line found in the last line which includes the "
            "score from {}",
            config_.name);
        return {};
    }

    return str_utils::splitString(last_info, ' ');
}

ScoreType UciEngine::lastScoreType() const {
    auto score = str_utils::findElement<std::string>(lastInfo(), "score").value_or("ERR");

    return score == "ERR" ? ScoreType::ERR : score == "cp" ? ScoreType::CP : ScoreType::MATE;
}

int UciEngine::lastScore() const {
    const auto score = lastScoreType();

    if (score == ScoreType::ERR) {
        Logger::warn<true>("Warning; No info string found in the last line from {} which includes the score",
                           config_.name);
        return 0;
    }

    return str_utils::findElement<int>(lastInfo(), lastScoreType() == ScoreType::CP ? "cp" : "mate").value_or(0);
}

bool UciEngine::outputIncludesBestmove() const {
    for (const auto &line : output_) {
        if (line.line.find("bestmove") != std::string::npos) return true;
    }

    return false;
}

}  // namespace fast_chess::engine
