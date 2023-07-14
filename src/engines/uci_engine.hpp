#pragma once

#include <third_party/chess.hpp>
#include <third_party/communication.hpp>

#include <engines/engine_config.hpp>

namespace fast_chess {

class UciEngine : private Communication::Process {
   public:
    explicit UciEngine(const EngineConfiguration &config) { loadConfig(config); }

    ~UciEngine() override { sendQuit(); }

    /// @brief Just writes "uci" to the engine
    void sendUci();
    /// @brief Reads until "uciok" is found, uses the default ping_time_ as the timeout thresholdd.
    /// @return
    [[nodiscard]] bool readUci();

    /// @brief Sends "ucinewgame" to the engine and waits for a response. Also uses the ping_time_
    /// as the timeout thresholdd.
    [[nodiscard]] bool sendUciNewGame();
    /// @brief Sends "quit" to the engine.
    void sendQuit();

    /// @brief Sends "isready" to the engine and waits for a response.
    /// @param threshold
    /// @return
    [[nodiscard]] bool isResponsive(int64_t threshold = ping_time_);

    [[nodiscard]] EngineConfiguration getConfig() const;

    /// @brief Build the uci position input from the given moves and fen.
    /// @param moves
    /// @param fen
    /// @return
    [[nodiscard]] static std::string buildPositionInput(const std::vector<std::string> &moves,
                                                 const std::string &fen) ;

    /// @brief Build the uci go input from the given time controls. tc is the time control for the
    /// current player.
    /// @param stm
    /// @param tc
    /// @param tc_2
    /// @return
    [[nodiscard]] std::string buildGoInput(chess::Color stm, const TimeControl &tc,
                                           const TimeControl &tc_2) const;

    /// @brief [untested... and unused]
    void restartEngine();
    /// @brief Creates a new process and starts the engine.
    void startEngine();

    /// @brief Reads from the engine until the last_word is found or the timeout_threshold is
    /// reached. May throw if the read fails.
    /// @param last_word
    /// @param timeout_threshold
    /// @return
    std::vector<std::string> readEngine(std::string_view last_word,
                                        int64_t timeout_threshold = ping_time_);
    /// @brief Writes the input to the engine. May throw if the write fails.
    /// @param input
    void writeEngine(const std::string &input);

    /// @brief Get the bestmove from the last output.
    /// @return
    [[nodiscard]] std::string bestmove() const;
    /// @brief Get the last info from the last output.
    /// @return
    [[nodiscard]] std::vector<std::string> lastInfo() const;
    /// @brief Get the last score type from the last output. cp or mate.
    /// @return
    [[nodiscard]] std::string lastScoreType() const;
    /// @brief Get the last score from the last output. Becareful, mate scores are not converted. So
    /// the score might 1, while it's actually mate 1. Always check lastScoreType() first.
    /// @return
    [[nodiscard]] int lastScore() const;

    [[nodiscard]] std::vector<std::string> output() const;

    /// @brief Check if the engine timed out.
    /// @return
    [[nodiscard]] bool timedout() const;

    /// @brief TODO: expose this to the user
    static const int64_t ping_time_ = 60000;

   private:
    void loadConfig(const EngineConfiguration &config);
    void sendSetoption(const std::string &name, const std::string &value);

    std::vector<std::string> output_;

    EngineConfiguration config_;
};
}  // namespace fast_chess
