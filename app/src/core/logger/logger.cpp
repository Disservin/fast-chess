#include <core/logger/logger.hpp>

#include <chrono>
#include <iomanip>
#include <thread>
#include <variant>

#include <core/time/time.hpp>
#include <types/exception.hpp>

#ifdef USE_ZLIB
#    include <core/logger/gzstream.hpp>
#endif

namespace fastchess {

bool Logger::compress_               = false;
Logger::Level Logger::level_         = Logger::Level::WARN;
bool Logger::strict_                 = false;
std::atomic_bool Logger::should_log_ = false;

std::mutex Logger::log_mutex_;
Logger::log_file_type Logger::log_;
bool Logger::engine_coms_ = false;

void Logger::openFile(const std::string& file, bool append) {
    if (file.empty()) {
        return;
    }

    auto flag = (append && !compress_) ? std::ios::app : std::ios::out;
#ifdef USE_ZLIB
    if (compress_) {
        auto t   = std::chrono::system_clock::now();
        auto fmt = fmt::format("{}{:%Y-%m-%dT.%H.%M.%S}.gz", file, t);
        log_.emplace<fcgzstream>(fmt.c_str(), flag);
    } else {
        log_.emplace<std::ofstream>(file.c_str(), flag);
    }
#else
    if (!compress_) {
        log_.emplace<std::ofstream>(file.c_str(), flag);
    } else {
        throw fastchess_exception("Compress is enabled but program wasn't compiled with zlib.");
    }
#endif

    Logger::should_log_ = true;

    // verify that the file was opened

    std::visit(
        [&](auto&& arg) {
            if (!arg.is_open()) {
                std::cerr << "Failed to open log file." << std::endl;
                Logger::should_log_ = false;
            }
        },
        log_);
}

void Logger::writeToEngine(const std::string& msg, const std::string& time, const std::string& name) {
    if (!should_log_ || !engine_coms_) {
        return;
    }

    const auto timestamp   = time.empty() ? time::datetime_precise() : time;
    const auto id          = std::this_thread::get_id();
    const auto fmt_message = fmt::format("{} {} <--- {}\n", make_prefix("Engine", timestamp, id), name, msg);

    const std::lock_guard<std::mutex> lock(log_mutex_);
    std::visit([&](auto&& arg) { arg << fmt_message << std::flush; }, log_);
}

void Logger::readFromEngine(const std::string& msg, const std::string& time, const std::string& name, bool err,
                            std::thread::id id) {
    if (!should_log_ || !engine_coms_) {
        return;
    }

    const auto fmt_message =
        fmt::format("{} {}{} ---> {}\n", make_prefix("Engine", time, id), (err ? "<stderr> " : ""), name, msg);

    const std::lock_guard<std::mutex> lock(log_mutex_);
    std::visit([&](auto&& arg) { arg << fmt_message << std::flush; }, log_);
}

}  // namespace fastchess
