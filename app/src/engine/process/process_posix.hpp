#pragma once

#ifndef _WIN64

#    include <engine/process/iprocess.hpp>

#    include <array>
#    include <cassert>
#    include <chrono>
#    include <optional>
#    include <string>
#    include <string_view>
#    include <thread>
#    include <vector>

#    include <errno.h>
#    include <fcntl.h>
#    include <poll.h>
#    include <signal.h>
#    include <spawn.h>
#    include <string.h>
#    include <sys/types.h>
#    include <sys/wait.h>
#    include <unistd.h>

#    include <affinity/affinity.hpp>
#    include <argv_split.hpp>
#    include <core/globals/globals.hpp>
#    include <core/logger/logger.hpp>
#    include <core/threading/thread_vector.hpp>
#    include <engine/process/interrupt.hpp>
#    include <engine/process/pipe.hpp>
#    include <engine/process/signal.hpp>
#    include <types/exception.hpp>

extern char** environ;

#    if !defined(HAVE_POSIX_SPAWN_FILE_ACTIONS_ADDCHDIR) && !defined(HAVE_POSIX_SPAWN_FILE_ACTIONS_ADDCHDIR_NP)
#        define NO_POSIX_SPAWN_FILE_ACTIONS_ADDCHDIR 1
#    endif

/* Available on:
 * - Solaris/illumos
 * - macOS 10.15 (Catalina) and newer
 * - glibc 2.29 and newer
 * - FreeBSD 13.1 and newer
 */
static inline int portable_spawn_file_actions_addchdir(posix_spawn_file_actions_t* file_actions, const char* path) {
#    ifdef HAVE_POSIX_SPAWN_FILE_ACTIONS_ADDCHDIR
    return posix_spawn_file_actions_addchdir(file_actions, path);
#    elif HAVE_POSIX_SPAWN_FILE_ACTIONS_ADDCHDIR_NP
    return posix_spawn_file_actions_addchdir_np(file_actions, path);
#    else
    // Fall back to no-op or error if neither variant is available
    (void)file_actions;
    (void)path;
    return ENOSYS;
#    endif
}

namespace fastchess {
extern util::ThreadVector<ProcessInformation> process_list;

namespace engine::process {

class Stream {
   public:
    Stream() = default;

    Stream(int fd, bool realtime_logging, Standard type, const std::string& log_name)
        : fd_(fd), realtime_logging_(realtime_logging), log_name_(log_name), type_(type) {
        line_buffer_.clear();
        line_buffer_.reserve(300);
    }

    Result readLine(std::vector<Line>& lines, std::string_view searchword) {
        const ssize_t bytes_read = read(fd_, buffer_.data(), buffer_.size());

        if (bytes_read == -1) {
            return Result::Error("read failed");
        }

        if (bytes_read == 0) {
            return Result::Error("EOF");
        }

        std::string_view data(buffer_.data(), static_cast<size_t>(bytes_read));
        size_t start = 0;

        while (start < data.size()) {
            const size_t nl = data.find('\n', start);

            if (nl == std::string_view::npos) {
                line_buffer_.append(data.substr(start));
                break;
            }

            line_buffer_.append(data.substr(start, nl - start));

            if (!line_buffer_.empty()) {
                const auto ts = Logger::should_log_ ? time::datetime_precise() : "";

                lines.emplace_back(Line{line_buffer_, ts, type_});

                if (realtime_logging_) {
                    Logger::readFromEngine(line_buffer_, ts, log_name_, type_ == Standard::ERR);
                }

                if (!searchword.empty() && line_buffer_.rfind(searchword, 0) == 0) {
                    line_buffer_.clear();
                    return Result::OK();
                }
            }

            line_buffer_.clear();
            start = nl + 1;
        }

        return Result{Status::NONE, ""};
    }

    void setup() { line_buffer_.clear(); }

    bool hasPartial() const { return !line_buffer_.empty(); }

    void flushPartial(std::vector<Line>& lines) {
        if (line_buffer_.empty()) return;

        auto ts = time::datetime_precise();
        lines.emplace_back(Line{line_buffer_, ts, type_});

        if (realtime_logging_) {
            Logger::readFromEngine(line_buffer_, ts, log_name_, type_ == Standard::ERR);
        }

        line_buffer_.clear();
    }

   private:
    int fd_;
    bool realtime_logging_;

    std::string line_buffer_;
    std::string log_name_;
    Standard type_;

    std::array<char, 4096> buffer_{};
};

class Process : public IProcess {
   public:
    virtual ~Process() override { terminate(); }

    Result init(const std::string& wd, const std::string& command, const std::string& args,
                const std::string& log_name) override {
        assert(!is_initalized_);

        wd_       = wd;
        command_  = command;
        args_     = args;
        log_name_ = log_name;

        is_initalized_ = true;
        startup_error_ = false;
        exit_code_.reset();

#    ifdef NO_POSIX_SPAWN_FILE_ACTIONS_ADDCHDIR
        command_ = getPath(wd_, command_);
#    endif

        argv_split parser(command_);
        parser.parse(args_);
        char* const* execv_argv = (char* const*)parser.argv();

        const auto success = spawn_with(execv_argv, posix_spawnp)  //
                                 .on_error([&]() { return spawn_with(execv_argv, posix_spawn); });

        if (success.code != Status::OK) {
            startup_error_ = true;
            return Result::Error(success.message);
        }

        sg_out_ = Stream(in_pipe_.read_end(), realtime_logging_, Standard::OUTPUT, log_name_);

        if (Logger::should_log_) {
            sg_err_ = Stream(err_pipe_.read_end(), realtime_logging_, Standard::ERR, log_name_);
        }

        // create a control pipe to interrupt polling when terminating
        interrupt_.cleanup();
        interrupt_.setup();

        // Append the process to the list of running processes
        // which are killed when the program exits, as a last resort
        process_list.push(ProcessInformation{process_pid_, interrupt_.get_write_fd()});

        // Close the parent's duplicates of the child-owned pipe ends so EOF/HUP
        // is observable when the child exits. macOS uses a pipe-based interrupt
        // fallback, so this cannot depend on eventfd availability.
        in_pipe_.close_write_end();
        err_pipe_.close_write_end();
        out_pipe_.close_read_end();

        if (interrupt_.has_eventfd()) {
            LOG_TRACE("Using eventfd for process interrupt signaling");
        }

        return Result::OK();
    }

    Result alive() noexcept override {
        if (terminated_) return Result::Error("process terminated");
        if (startup_error_) return Result::Error("process failed to start");

        assert(is_initalized_);

        int status    = 0;
        const pid_t r = waitpid(process_pid_, &status, WNOHANG);
        if (r != 0) exit_code_ = status;

        return (r == 0) ? Result::OK() : Result::Error("process terminated");
    }

    bool setAffinity(const std::vector<int>& cpus) noexcept override {
        assert(is_initalized_);
        // Apple does not support setting the affinity of a pid
#    ifdef __APPLE__
        assert(false && "This function should not be called on Apple devices");
#    endif

        return affinity::setProcessAffinity(cpus, process_pid_);
    }

    void terminate() {
        if (startup_error_) {
            is_initalized_ = false;
            return;
        }

        if (!is_initalized_) return;

        process_list.remove_if([this](const ProcessInformation& pi) { return pi.identifier == process_pid_; });

        int status = 0;
        if (exit_code_.has_value()) {
            status = *exit_code_;
        } else {
            status     = wait_with_timeout_or_kill();
            exit_code_ = status;
        }

        // log the status of the process
        Logger::readFromEngine(signalToString(status), time::datetime_precise(), log_name_, true);
        is_initalized_ = false;
        terminated_    = true;
    }

    void setupRead() override {
        sg_out_.setup();
        sg_err_.setup();
    }

    // Read stdout until the line matches searchword or timeout is reached
    // 0 means no timeout clears the lines vector
    Result readOutput(std::vector<Line>& lines, std::string_view searchword,
                      std::chrono::milliseconds threshold) override {
        assert(is_initalized_);
        lines.clear();

        const int poll_timeout_ms = normalize_poll_timeout_ms(threshold);

        std::array<pollfd, 3> fds;
        size_t fds_count = 0;

        const size_t STDOUT_IDX = fds_count++;
        fds[STDOUT_IDX]         = create_pollfd(in_pipe_.read_end(), POLLIN | POLLERR | POLLHUP);

        const size_t INT_IDX = fds_count++;
        fds[INT_IDX]         = create_pollfd(interrupt_.get_read_fd(), POLLIN);

        std::optional<size_t> STDERR_IDX;
        if (Logger::should_log_) {
            STDERR_IDX       = fds_count++;
            fds[*STDERR_IDX] = create_pollfd(err_pipe_.read_end(), POLLIN | POLLERR | POLLHUP);
        }

        // Continue reading output lines until the line
        // matches the specified searchword or a timeout occurs
        while (true) {
            // We prefer to use poll instead of select because
            // poll is more efficient and select has a filedescriptor limit of 1024
            // which can be a problem when running with a high concurrency
            const int ready = poll(fds.data(), static_cast<nfds_t>(fds_count), poll_timeout_ms);

            if (ready == -1) return Result::Error("poll failed");
            if (ready == 0) {
                flush_partials_on_timeout(lines);
                return Result{Status::TIMEOUT, "timeout"};
            }

            // 1. Check Interrupt
            if (fds[INT_IDX].revents & POLLIN) {
                [[maybe_unused]] uint64_t junk;
                [[maybe_unused]] auto r = read(fds[INT_IDX].fd, &junk, sizeof(junk));
                assert(junk > 0);
                return Result::Error("Interrupted by control pipe");
            }

            // 2. Check Stdout
            auto& out_fd = fds[STDOUT_IDX];
            if (out_fd.revents & (POLLIN | POLLHUP | POLLERR)) {
                if (out_fd.revents & (POLLHUP | POLLERR)) return Result::Error("Engine crashed (stdout)");
                if (auto r = sg_out_.readLine(lines, searchword); r.code != Status::NONE) return r;
            }

            // 3. Check Stderr
            if (STDERR_IDX) {
                auto& err_fd = fds[*STDERR_IDX];
                if (err_fd.revents & POLLIN) {
                    if (auto r = sg_err_.readLine(lines, ""); r.code != Status::NONE) return r;
                }
                if (err_fd.revents & (POLLHUP | POLLERR)) return Result::Error("Engine crashed (stderr)");
            }
        }

        return Result::OK();
    }

    Result writeInput(const std::string& input) noexcept override {
        assert(is_initalized_);
        if (!alive()) return Result::Error("process not alive");

        if (write(out_pipe_.write_end(), input.c_str(), input.size()) == -1) {
            return Result::Error(strerror(errno));
        }

        return Result::OK();
    }

   private:
    struct SpawnFileActions {
        posix_spawn_file_actions_t actions{};
        SpawnFileActions() {
            const int rc = posix_spawn_file_actions_init(&actions);
            if (rc != 0) throw fastchess_exception::format("posix_spawn_file_actions_init failed: {}", strerror(rc));
        }

        ~SpawnFileActions() { posix_spawn_file_actions_destroy(&actions); }

        SpawnFileActions(const SpawnFileActions&)            = delete;
        SpawnFileActions& operator=(const SpawnFileActions&) = delete;

        void add_dup2(int from_fd, int to_fd) {
            const int rc = posix_spawn_file_actions_adddup2(&actions, from_fd, to_fd);
            if (rc != 0) throw fastchess_exception("posix_spawn_file_actions_adddup2 failed");
        }

        void add_workdir(const std::string& wd) {
            if (wd.empty()) return;

#    ifdef NO_POSIX_SPAWN_FILE_ACTIONS_ADDCHDIR
            // wd already baked into command path
            return;
#    endif

            const int rc = portable_spawn_file_actions_addchdir(&actions, wd.c_str());
            if (rc != 0) {
                // chdir is broken on some macOS setups, ignore in that case
#    if !(defined(__MAC_OS_X_VERSION_MIN_REQUIRED) && __MAC_OS_X_VERSION_MIN_REQUIRED >= 101500)
                throw fastchess_exception("posix_spawn_file_actions_addchdir failed");
#    endif
            }
        }
    };

    template <typename SpawnFunc>
    Result spawn_with(char* const* execv_argv, SpawnFunc spawn_func) {
        // reset pipes each attempt
        out_pipe_ = {};
        in_pipe_  = {};

        if (Logger::should_log_) {
            err_pipe_ = {};
        } else {
            err_pipe_.close_fds();
        }

        try {
            SpawnFileActions fa;

            fa.add_dup2(out_pipe_.read_end(), STDIN_FILENO);
            fa.add_dup2(in_pipe_.write_end(), STDOUT_FILENO);

            if (Logger::should_log_) {
                fa.add_dup2(err_pipe_.write_end(), STDERR_FILENO);
            }

            fa.add_workdir(wd_);

            const int rc = spawn_func(&process_pid_, command_.c_str(), &fa.actions, nullptr, execv_argv, environ);
            if (rc != 0) {
                throw fastchess_exception::format("posix_spawn failed: {}", std::strerror(rc));
            }
        } catch (const std::exception& e) {
            return Result::Error(e.what());
        }

        return Result::OK();
    }

    int wait_with_timeout_or_kill() {
        int status = 0;

        const auto start = std::chrono::steady_clock::now();
        pid_t pid        = 0;

        while (std::chrono::steady_clock::now() - start < IProcess::kill_timeout) {
            pid = waitpid(process_pid_, &status, WNOHANG);
            if (pid != 0) break;  // >0 exited, <0 error
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (pid == 0) {
            LOG_TRACE_THREAD("Force terminating process with pid: {} {}", process_pid_, status);
            kill(process_pid_, SIGKILL);
            waitpid(process_pid_, &status, 0);
        } else {
            LOG_TRACE_THREAD("Process with pid: {} terminated with status: {}", process_pid_, status);
        }

        return status;
    }

    static int normalize_poll_timeout_ms(std::chrono::milliseconds threshold) {
        // wait indefinitely
        if (threshold.count() <= 0) return -1;
        if (threshold.count() > (std::chrono::milliseconds::max)().count()) return -1;
        return static_cast<int>(threshold.count());
    }

    void flush_partials_on_timeout(std::vector<Line>& lines) {
        sg_out_.flushPartial(lines);
        if (Logger::should_log_) {
            sg_err_.flushPartial(lines);
        }
    }

    [[nodiscard]] pollfd create_pollfd(int fd, short events) const {
        pollfd pfd;
        pfd.fd      = fd;
        pfd.events  = events;
        pfd.revents = 0;
        return pfd;
    }

   private:
    std::string wd_;
    std::string command_;
    std::string args_;
    std::string log_name_;

    Stream sg_out_;
    Stream sg_err_;

    bool is_initalized_ = false;
    bool startup_error_ = false;
    bool terminated_    = false;

    pid_t process_pid_{0};
    Pipe in_pipe_, out_pipe_, err_pipe_;

    InterruptSignaler interrupt_{};

    std::optional<int> exit_code_;
};

}  // namespace engine::process
}  // namespace fastchess

#endif
