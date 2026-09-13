#include <engine/uci_engine.hpp>

#include <algorithm>
#include <condition_variable>
#include <iterator>
#include <mutex>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include <chess.hpp>

#include <core/config/config.hpp>
#include <core/helper.hpp>
#include <core/logger/logger.hpp>
#include "expected.hpp"

namespace fastchess::engine {

// A counting semaphore to limit the number of threads
class CountingSemaphore {
   public:
    CountingSemaphore(int max_) : max__(max_), current_(0) {}

    void acquire() {
        std::unique_lock<std::mutex> lock(mtx_);
        cond_.wait(lock, [this] { return current_ < max__; });
        ++current_;
    }

    void release() {
        std::lock_guard<std::mutex> lock(mtx_);
        --current_;
        cond_.notify_one();
    }

   private:
    std::mutex mtx_;
    std::condition_variable cond_;
    int max__;
    int current_;
};

// RAII class to acquire and release the semaphore
class AcquireSemaphore {
   public:
    explicit AcquireSemaphore(CountingSemaphore& semaphore) : semaphore_(semaphore) { semaphore_.acquire(); }

    ~AcquireSemaphore() { semaphore_.release(); }

   private:
    CountingSemaphore& semaphore_;
};

namespace {
CountingSemaphore semaphore(16);

}  // namespace

UciEngine::UciEngine(const EngineConfiguration& config, bool realtime_logging) : realtime_logging_(realtime_logging) {
    loadConfig(config);
    output_.reserve(100);

    process_.setRealtimeLogging(realtime_logging_);
}

process::Result UciEngine::isready(std::optional<std::chrono::milliseconds> threshold) {
    const auto is_alive = process_.alive();
    if (!is_alive) return is_alive;

    LOG_TRACE_THREAD("Pinging engine {}", config_.name);

    writeEngine("isready");

    setupReadEngine();

    std::vector<process::Line> output;
    const auto res = process_.readOutput(output, "readyok", threshold.value_or(getPingTime()));

    // print output in case we are using delayed logging
    if (!realtime_logging_) {
        for (const auto& line : output) {
            Logger::readFromEngine(line.line, line.time, config_.name, line.std == process::Standard::ERR);
        }
    }

    if (!res) {
        if (!atomic::stop) {
            LOG_TRACE_THREAD("Engine {} didn't respond to isready", config_.name);
            Logger::print<Logger::Level::WARN>("Warning; Engine {} is not responsive", config_.name);
        }

        return res;
    }

    LOG_TRACE_THREAD("Engine {} is {}", config_.name, "responsive");

    return res;
}

bool UciEngine::position(const std::vector<std::string_view>& moves, const std::string& fen) {
    std::string command;

    if (fen == "startpos") {
        command = "position startpos";
    } else {
        command = fmt::format("position fen {}", fen);
    }

    if (!moves.empty()) {
        command += " moves";

        for (std::string_view move : moves) {
            fmt::format_to(std::back_inserter(command), " {}", move);
        }
    }

    return writeEngine(command);
}

bool UciEngine::go(const TimeControl& our_tc, const TimeControl& enemy_tc, chess::Color stm) {
    std::string input = "go";

    if (config_.limit.nodes) {
        fmt::format_to(std::back_inserter(input), " nodes {}", config_.limit.nodes);
    }

    if (config_.limit.plies) {
        fmt::format_to(std::back_inserter(input), " depth {}", config_.limit.plies);
    }

    // We cannot use st and tc together
    if (our_tc.isFixedTime()) {
        fmt::format_to(std::back_inserter(input), " movetime {}", our_tc.getFixedTime());
        return writeEngine(input);
    }

    const auto& white = stm == chess::Color::WHITE ? our_tc : enemy_tc;
    const auto& black = stm == chess::Color::WHITE ? enemy_tc : our_tc;

    if (our_tc.isTimed() || our_tc.isIncrement()) {
        if (white.isTimed() || white.isIncrement()) {
            fmt::format_to(std::back_inserter(input), " wtime {}", white.getTimeLeft());
        }

        if (black.isTimed() || black.isIncrement()) {
            fmt::format_to(std::back_inserter(input), " btime {}", black.getTimeLeft());
        }

        if (white.isIncrement()) {
            fmt::format_to(std::back_inserter(input), " winc {}", white.getIncrement());
        }

        if (black.isIncrement()) {
            fmt::format_to(std::back_inserter(input), " binc {}", black.getIncrement());
        }
    }

    if (our_tc.isMoves()) {
        fmt::format_to(std::back_inserter(input), " movestogo {}", our_tc.getMovesLeft());
    }

    return writeEngine(input);
}

bool UciEngine::ucinewgame() {
    LOG_TRACE_THREAD("Sending ucinewgame to engine {}", config_.name);
    auto res = writeEngine("ucinewgame");

    if (!res) {
        LOG_WARN_THREAD("Failed to send ucinewgame to engine {}", config_.name);
        return false;
    }

    return isready(getUciNewGameTime()).code == process::Status::OK;
}

std::optional<std::string> UciEngine::idName() {
    if (initialized_) return id_name_;

    if (!uci()) {
        Logger::print<Logger::Level::WARN>("Warning; Engine {} didn't respond to uci.", config_.name);

        return std::nullopt;
    }

    if (!uciok()) {
        Logger::print<Logger::Level::WARN>("Warning; Engine {} didn't respond to uci.", config_.name);
        return std::nullopt;
    }

    return id_name_;
}

std::optional<std::string> UciEngine::idAuthor() {
    if (initialized_) return id_author_;

    if (!uci()) {
        Logger::print<Logger::Level::WARN>("Warning; Engine {} didn't respond to uci.", config_.name);

        return std::nullopt;
    }

    if (!uciok()) {
        Logger::print<Logger::Level::WARN>("Warning; Engine {} didn't respond to uci.", config_.name);

        return std::nullopt;
    }

    return id_author_;
}

bool UciEngine::uci() {
    LOG_TRACE_THREAD("Sending uci to engine {}", config_.name);
    const auto res = writeEngine("uci");

    if (!res) {
        LOG_WARN_THREAD("Failed to send uci to engine {}", config_.name);
        return false;
    }

    return res;
}

bool UciEngine::uciok(std::optional<ms> threshold) {
    LOG_TRACE_THREAD("Waiting for uciok from engine {}", config_.name);

    const auto res = readEngine("uciok", threshold.value_or(getPingTime()));
    const auto ok  = res.code == process::Status::OK;

    for (const auto& line : output_) {
        if (!realtime_logging_) {
            Logger::readFromEngine(line.line, line.time, config_.name, line.std == process::Standard::ERR);
        }
    }

    for (const auto& line : getStdoutLines()) {
        if (line->line.compare(0, 8, "id name ") == 0) {
            id_name_ = line->line.substr(8);
        } else if (line->line.compare(0, 10, "id author ") == 0) {
            id_author_ = line->line.substr(10);
        }

        auto option = UCIOptionFactory::parseUCIOptionLine(line->line);

        if (option != nullptr) {
            uci_options_.addOption(std::move(option));
        }
    }

    if (!ok) LOG_WARN_THREAD("Engine {} did not respond to uciok in time.", config_.name);

    return ok;
}

void UciEngine::loadConfig(const EngineConfiguration& config) { config_ = config; }

void UciEngine::quit() {
    if (!initialized_) return;

    if (!process_.alive()) return;

    LOG_TRACE_THREAD("Trying to stop engine gracefully {}", config_.name);

    if (!writeEngine("stop")) {
        LOG_WARN_THREAD("Failed to send stop to engine {}", config_.name);
    }

    if (!writeEngine("quit")) {
        LOG_WARN_THREAD("Failed to send quit to engine {}", config_.name);
    }
}

void UciEngine::sendSetoption(const std::string& name, const std::string& value) {
    auto option = uci_options_.getOption(name);

    if (!option.has_value()) {
        Logger::print<Logger::Level::WARN>("Warning; {} doesn't have option {}", config_.name, name);

        return;
    }

    if (!option.value()->isValid(value)) {
        Logger::print<Logger::Level::WARN>("Warning; Invalid value for option {}; {}", name, value);

        return;
    }

    LOG_TRACE_THREAD("Sending setoption to engine {} {} {}", config_.name, name, value);

    if (option.value()->getType() == UCIOption::Type::Button) {
        if (value != "true") {
            return;
        }

        if (!writeEngine(fmt::format("setoption name {}", name))) {
            LOG_WARN_THREAD("Failed to send setoption to engine {} {}", config_.name, name);
            return;
        }

        option.value()->setValue(value);
        return;
    }

    if (!writeEngine(fmt::format("setoption name {} value {}", name, value))) {
        LOG_WARN_THREAD("Failed to send setoption to engine {} {} {}", config_.name, name, value);
        return;
    }

    option.value()->setValue(value);
}

tl::expected<bool, std::string> UciEngine::start(const std::optional<std::vector<int>>& cpus) {
    if (initialized_) return true;

    AcquireSemaphore semaphore_acquire(semaphore);

    const auto path = config_.cmd;
    LOG_TRACE_THREAD("Starting engine {} at {}", config_.name, path);

    // Creates the engine process and sets the pipes
    if (auto res = process_.init(config_.dir, path, config_.args, config_.name); !res) {
        return tl::make_unexpected(res.message);
    }

    if (cpus) {
        setCpus(*cpus);
    }

    initialized_ = true;

    // Wait for the engine to start
    if (!uci()) {
        return tl::make_unexpected("Couldn't write uci to engine");
    }

    if (!uciok(getStartupTime())) {
        return tl::make_unexpected("Engine didn't respond to uciok after startup");
    }

    return true;
}

bool UciEngine::refreshUci() {
    LOG_TRACE_THREAD("Refreshing engine {}", config_.name);

    // Reorder to send Threads option first, to help multi-threaded configurations

    auto options = config_.options;
    std::stable_partition(options.begin(), options.end(), [](const auto& p) { return p.first == "Threads"; });

    for (const auto& option : options) {
        try {
            sendSetoption(option.first, option.second);

        } catch (const std::exception& e) {
            Logger::print<Logger::Level::WARN>("Warning; Failed to set option {} with value {} for engine {}: {}",
                                               option.first, option.second, config_.name, e.what());
        }
    }

    if (config_.variant == VariantType::FRC) {
        try {
            sendSetoption("UCI_Chess960", "true");
        } catch (const std::exception& e) {
            Logger::print<Logger::Level::WARN>("Warning; Failed to set UCI_Chess960 option for engine {}: {}",
                                               config_.name, e.what());
        }
    }

    if (!ucinewgame()) {
        LOG_WARN_THREAD("Engine {} failed to start/refresh (ucinewgame).", config_.name);
        return false;
    }

    LOG_TRACE_THREAD("Engine {} refreshed.", config_.name);

    return true;
}

process::Result UciEngine::readEngine(std::string_view last_word, std::optional<ms> threshold) {
    setupReadEngine();
    return process_.readOutput(output_, last_word, threshold.value_or(getPingTime()));
}

void UciEngine::writeLog() const {
    for (const auto& line : output_) {
        Logger::readFromEngine(line.line, line.time, config_.name, line.std == process::Standard::ERR);
    }
}

std::string UciEngine::lastInfoLine() const {
    std::string fallback;

    // iterate backwards over the output and save the info line
    const auto lines = getStdoutLines();
    for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
        const auto& line = (*it)->line;

        // skip "info string" lines
        if (line.find("info string") != std::string::npos) {
            continue;
        }

        // only consider info lines with score and (multipv 1 if present)
        if (line.find("info") == std::string::npos || line.find(" score ") == std::string::npos ||
            (line.find(" multipv ") != std::string::npos && line.find(" multipv 1") == std::string::npos)) {
            continue;
        }

        bool isBound = UciEngine::isBound(line);

        // prefer exact scores
        if (!isBound) return line;

        // save as fallback if we don’t find an exact one
        if (fallback.empty()) fallback = line;
    }

    return fallback;
}

std::vector<const std::string*> UciEngine::getInfoLines() const {
    std::vector<const std::string*> info_lines;
    for (const auto& it : getStdoutLines()) {
        const std::string& line = it->line;
        if (line.find("info string") != std::string::npos) continue;
        if (line.find("info") == std::string::npos || line.find(" score ") == std::string::npos) continue;
        info_lines.push_back(&line);
    }
    return info_lines;
}

bool UciEngine::writeEngine(const std::string& input) {
    Logger::writeToEngine(input, "", config_.name);
    return process_.writeInput(fmt::format("{}\n", input)).code == process::Status::OK;
}

std::optional<std::string> UciEngine::bestmove(bool warn_on_error) const {
    const auto warn = [&](std::string_view reason, std::string_view last_line = {}) {
        if (!warn_on_error) {
            return;
        }

        if (last_line.empty()) {
            Logger::print<Logger::Level::WARN>("Warning; No output from {}", config_.name);
        } else {
            Logger::print<Logger::Level::WARN>("Warning; No bestmove found from {} because: {}. Last line was: {}",
                                               config_.name, reason, last_line);
        }
    };

    const auto lines = getStdoutLines();

    if (lines.empty()) {
        warn("No output");
        return std::nullopt;
    }

    const auto& last_line = lines.back()->line;

    if (last_line.rfind("bestmove", 0) != 0) {
        warn("Line does not start with 'bestmove'", last_line);
        return std::nullopt;
    }

    const auto bm = str_utils::findElement<std::string>(str_utils::splitString(last_line, ' '), "bestmove");

    if (!bm.has_value()) {
        warn(bm.error(), last_line);
        return std::nullopt;
    }

    return *bm;
}

std::chrono::milliseconds UciEngine::lastTime() const {
    const auto lines = getStdoutLines();

    for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
        const auto& line = (*it)->line;

        // skip "info string" lines
        if (line.find("info string") != std::string::npos) {
            continue;
        }

        if (line.find("info") == std::string::npos || line.find("time") == std::string::npos) {
            continue;
        }

        return std::chrono::milliseconds(parseInfo(line).time.value_or(0));
    }

    return std::chrono::milliseconds(0);
}

UciInfo UciEngine::parseInfo(std::string_view info_line) {
    UciInfo result;
    const auto tokens = str_utils::splitString(info_line, ' ');

    const auto parse_integer = [&](auto& value, size_t index) {
        if (index >= tokens.size()) return;

        using Value = typename std::decay_t<decltype(value)>::value_type;
        value       = str_utils::parseInteger<Value>(tokens[index]);
    };

    for (size_t i = 0; i < tokens.size(); ++i) {
        const auto& token = tokens[i];

        if (token == "score") {
            if (i + 2 >= tokens.size()) {
                result.score = tl::make_unexpected(std::string("Element 'score' has no type or value"));
                continue;
            }

            Score score;
            score.type = ScoreType::fromString(tokens[i + 1]);
            if (score.isErr()) {
                result.score = tl::make_unexpected(fmt::format("Unexpected score type: {}", info_line));
                continue;
            }

            std::optional<int64_t> value;
            parse_integer(value, i + 2);
            if (!value.has_value()) {
                result.score = tl::make_unexpected(fmt::format("Invalid score value: {}", tokens[i + 2]));
                continue;
            }
            score.value  = *value;
            result.score = score;
        } else if (token == "time") {
            parse_integer(result.time, i + 1);
        } else if (token == "depth") {
            parse_integer(result.depth, i + 1);
        } else if (token == "seldepth") {
            parse_integer(result.seldepth, i + 1);
        } else if (token == "nodes") {
            parse_integer(result.nodes, i + 1);
        } else if (token == "nps") {
            parse_integer(result.nps, i + 1);
        } else if (token == "tbhits") {
            parse_integer(result.tbhits, i + 1);
        } else if (token == "hashfull") {
            parse_integer(result.hashfull, i + 1);
        } else if (token == "multipv") {
            parse_integer(result.multipv, i + 1);
        } else if (token == "lowerbound") {
            result.lowerbound = true;
        } else if (token == "upperbound") {
            result.upperbound = true;
        } else if (token == "pv") {
            for (++i; i < tokens.size() && chess::uci::isUciMove(tokens[i]); ++i) result.pv.push_back(tokens[i]);
            break;
        }
    }

    return result;
}

tl::expected<Score, std::string> UciEngine::getScore(std::string_view info_line) {
    if (info_line.empty()) {
        return tl::make_unexpected(fmt::format("No info line available to extract score from: {}", info_line));
    }
    return parseInfo(info_line).score;
}

tl::expected<Score, std::string> UciEngine::lastScore() const { return getScore(lastInfoLine()); }

std::optional<std::vector<std::string>> UciEngine::getPv(std::string_view info_line) {
    if (info_line.empty()) return std::nullopt;

    auto info = parseInfo(info_line);
    if (info.pv.empty()) return std::nullopt;
    return std::move(info.pv);
}

}  // namespace fastchess::engine
