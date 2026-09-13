#pragma once

#include <chrono>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <chess.hpp>
#include <expected.hpp>

#ifdef _WIN64
#    include <engine/process/process_win.hpp>
#else
#    include <engine/process/process_posix.hpp>
#endif

#include <core/config/config.hpp>
#include <engine/option/option_factory.hpp>
#include <engine/option/options.hpp>
#include <types/engine_config.hpp>
#include <types/score.hpp>

namespace fastchess::engine {

using ms = std::chrono::milliseconds;

struct UciInfo {
    tl::expected<Score, std::string> score = tl::make_unexpected(std::string("Element 'score' not found"));
    std::optional<int64_t> time;
    std::optional<int64_t> depth;
    std::optional<int64_t> seldepth;
    std::optional<uint64_t> nodes;
    std::optional<uint64_t> nps;
    std::optional<uint64_t> tbhits;
    std::optional<int64_t> hashfull;
    std::optional<int64_t> multipv;
    std::vector<std::string> pv;
    bool lowerbound = false;
    bool upperbound = false;

    [[nodiscard]] bool isBound() const noexcept { return lowerbound || upperbound; }
};

class UciEngine {
   public:
    explicit UciEngine(const EngineConfiguration& config, bool realtime_logging);

    UciEngine(const UciEngine&)            = delete;
    UciEngine(UciEngine&&)                 = delete;
    UciEngine& operator=(const UciEngine&) = delete;
    UciEngine& operator=(UciEngine&&)      = delete;

    ~UciEngine() { quit(); }

    void terminate() { process_.terminate(); }

    // Starts the engine, does nothing after the first call.
    // Returns false if the engine is not alive.
    [[nodiscard]] tl::expected<bool, std::string> start(const std::optional<std::vector<int>>& cpus);

    // Restarts the engine, if necessary and reapplies the options.
    bool refreshUci();

    [[nodiscard]] std::optional<std::string> idName();
    [[nodiscard]] std::optional<std::string> idAuthor();

    // Returns false in case of failure.
    [[nodiscard]] bool uci();
    // Returns false in case of failure.
    [[nodiscard]] bool uciok(std::optional<ms> threshold = std::nullopt);
    // Returns false in case of failure.
    [[nodiscard]] bool ucinewgame();

    // Sends "isready" to the engine
    [[nodiscard]] process::Result isready(std::optional<ms> threshold = std::nullopt);

    // Sends "position" to the engine and waits for a response.
    [[nodiscard]] bool position(const std::vector<std::string_view>& moves, const std::string& fen);

    [[nodiscard]] bool go(const TimeControl& our_tc, const TimeControl& enemy_tc, chess::Color stm);

    void quit();

    // Writes the input to the engine. Appends a newline to the input.
    // Returns false in case of failure.
    bool writeEngine(const std::string& input);

    // Waits for the engine to output the last_word or until the threshold_ms is reached.
    // May throw if the read fails.
    process::Result readEngine(std::string_view last_word, std::optional<ms> threshold = std::nullopt);

    process::Result readEngineLowLat(std::string_view last_word, std::optional<ms> threshold = std::nullopt) {
        return readEngine(last_word, threshold);
    }

    void setupReadEngine() { process_.setupRead(); }

    // Logs are not written in realtime to avoid slowing down the engine.
    // This function writes the logs to the logger.
    void writeLog() const;

    void setCpus(const std::vector<int>& cpus) {
        if (cpus.empty()) return;

// Apple does not support setting the affinity of a pid
#ifdef __APPLE__
        return;
#endif

        if (process_.setAffinity(cpus)) return;

        // turn cpus vector into a string for error logging
        std::string cpu_str;
        for (const auto& cpu : cpus) {
            if (!cpu_str.empty()) cpu_str += ", ";
            fmt::format_to(std::back_inserter(cpu_str), "{}", cpu);
        }

        Logger::print<Logger::Level::WARN>(
            "Warning; Failed to set CPU affinity for the engine process to {}. Please restart.", cpu_str);
    }

    // Get the bestmove from the last output.
    [[nodiscard]] std::optional<std::string> bestmove(bool warn_on_error = true) const;

    // Get the last info line from the last output.
    [[nodiscard]] std::string lastInfoLine() const;

    [[nodiscard]] ms lastTime() const;

    // Get all the info lines from the last output.
    [[nodiscard]] std::vector<const std::string*> getInfoLines() const;

    // Parse an info line in one pass.
    [[nodiscard]] static UciInfo parseInfo(std::string_view info_line);

    // Get the score from an info line.
    [[nodiscard]] static tl::expected<Score, std::string> getScore(std::string_view info_line);

    // Get the last score from the last output.
    [[nodiscard]] tl::expected<Score, std::string> lastScore() const;

    // Get the PV from an info line.
    [[nodiscard]] static std::optional<std::vector<std::string>> getPv(std::string_view info_line);

    [[nodiscard]] const EngineConfiguration& getConfig() const noexcept { return config_; }

    std::vector<const process::Line*> getStdoutLines() const { return getLines(process::Standard::OUTPUT); }
    std::vector<const process::Line*> getStderrLines() const { return getLines(process::Standard::ERR); }

    // @TODO: expose this to the user?

    [[nodiscard]] const UCIOptions& uciOptions() const noexcept { return uci_options_; }
    UCIOptions& uciOptions() noexcept { return uci_options_; }

    [[nodiscard]] bool isRealtimeLogging() const noexcept { return realtime_logging_; }

    [[nodiscard]] static ms getPingTime() { return config::TournamentConfig->ping_time; }
    [[nodiscard]] static ms getUciNewGameTime() { return config::TournamentConfig->ucinewgame_time; }
    [[nodiscard]] static ms getStartupTime() { return config::TournamentConfig->startup_time; }

    [[nodiscard]] static bool isBound(std::string_view info_line) {
        return (info_line.find("lowerbound") != std::string::npos || info_line.find("upperbound") != std::string::npos);
    }

   private:
    void loadConfig(const EngineConfiguration& config);
    void sendSetoption(const std::string& name, const std::string& value);

    std::vector<const process::Line*> getLines(process::Standard type) const {
        std::vector<const process::Line*> lines;
        for (const auto& line : output_) {
            if (line.std == type) {
                lines.push_back(&line);
            }
        }
        return lines;
    }

    process::Process process_ = {};
    UCIOptions uci_options_   = {};
    EngineConfiguration config_;

    std::optional<std::string> id_name_;
    std::optional<std::string> id_author_;

    std::vector<process::Line> output_;

    // init on first use
    bool initialized_ = false;

    const bool realtime_logging_;
};
}  // namespace fastchess::engine
