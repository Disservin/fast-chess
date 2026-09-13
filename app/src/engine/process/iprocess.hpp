#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace fastchess::engine::process {

enum class Standard { INPUT, OUTPUT, ERR };
enum class Status { OK, ERR, TIMEOUT, NONE };

struct Result {
    Status code;
    std::string message;

    Result(Status code, std::string message) : code(code), message(std::move(message)) {}

    static Result OK() { return {Status::OK, ""}; }
    static Result Error(std::string msg) { return {Status::ERR, std::move(msg)}; }

    Result on_error(std::function<Result()> next) const {
        if (code == Status::OK) return *this;
        return next();
    }

    explicit operator bool() const { return code == Status::OK; }
};

struct Line {
    std::string line;
    std::string time;
    Standard std = Standard::OUTPUT;
};

class IProcess {
   public:
    virtual ~IProcess() = default;

    IProcess()                           = default;
    IProcess(const IProcess&)            = delete;
    IProcess(IProcess&&)                 = delete;
    IProcess& operator=(const IProcess&) = delete;
    IProcess& operator=(IProcess&&)      = delete;

    void setRealtimeLogging(bool realtime_logging) noexcept { realtime_logging_ = realtime_logging; }

    virtual void setupRead() = 0;

    // Initialize the process
    virtual Result init(const std::string& wd, const std::string& command, const std::string& args,
                        const std::string& log_name) = 0;

    // Returns OK if the process is alive, Error otherwise
    [[nodiscard]] virtual Result alive() noexcept = 0;

    virtual bool setAffinity(const std::vector<int>& cpus) noexcept = 0;

    // Read stdout until the line matches last_word or timeout is reached
    // 0 threshold means no timeout
    virtual Result readOutput(std::vector<Line>& lines, std::string_view last_word,
                              std::chrono::milliseconds threshold) = 0;

    // Write input to the engine's stdin
    virtual Result writeInput(const std::string& input) noexcept = 0;

    constexpr static std::chrono::seconds kill_timeout{10};

   protected:
    [[nodiscard]] std::string getPath(const std::string& dir, const std::string& cmd) const {
        // convert path to a filesystem path
        auto p = std::filesystem::path(dir) / std::filesystem::path(cmd);
        return p.string();
    }

    bool realtime_logging_ = true;
};

}  // namespace fastchess::engine::process
