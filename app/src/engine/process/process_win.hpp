#pragma once

#ifdef _WIN64

#    include <engine/process/iprocess.hpp>

#    include <cassert>
#    include <chrono>
#    include <string>
#    include <thread>
#    include <vector>

#    include <windows.h>

#    include <affinity/affinity.hpp>
#    include <core/globals/globals.hpp>
#    include <core/logger/logger.hpp>
#    include <core/threading/thread_vector.hpp>
#    include <engine/process/anon_pipe.hpp>

namespace fastchess {

extern util::ThreadVector<ProcessInformation> process_list;

namespace engine::process {

class Process : public IProcess {
    constexpr static int buffer_size = 4096;

    HANDLE hChildStdoutRead  = NULL;
    HANDLE hChildStdoutWrite = NULL;
    HANDLE hChildStdinRead   = NULL;
    HANDLE hChildStdinWrite  = NULL;
    std::array<char, buffer_size> buffer;

   public:
    ~Process() override {
        LOG_TRACE_THREAD("Process destructor called for {} with pid {}", log_name_, pi_.dwProcessId);
        terminate();
    }

    Result init(const std::string& dir, const std::string& path, const std::string& args,
                const std::string& log_name) override {
        wd_       = dir;
        command_  = getPath(dir, path);
        args_     = args;
        log_name_ = log_name;

        current_line_.reserve(300);
        pi_ = PROCESS_INFORMATION{};

        SECURITY_ATTRIBUTES saAttr;
        saAttr.nLength              = sizeof(SECURITY_ATTRIBUTES);
        saAttr.bInheritHandle       = TRUE;
        saAttr.lpSecurityDescriptor = NULL;

        if (!CreatePipeEx(&hChildStdoutRead, &hChildStdoutWrite, &saAttr)) {
            return Result::Error("failed to create stdout pipe");
        }

        if (!CreatePipeEx(&hChildStdinRead, &hChildStdinWrite, &saAttr)) {
            CloseHandle(hChildStdoutRead);
            CloseHandle(hChildStdoutWrite);
            return Result::Error("failed to create stdin pipe");
        }

        if (!SetHandleInformation(hChildStdoutRead, HANDLE_FLAG_INHERIT, 0)) {
            closesHandles();
            return Result::Error("failed to set stdout handle information");
        }

        if (!SetHandleInformation(hChildStdinWrite, HANDLE_FLAG_INHERIT, 0)) {
            closesHandles();
            return Result::Error("failed to set stdin handle information");
        }

        try {
            STARTUPINFOA si = {};
            si.cb           = sizeof(STARTUPINFOA);
            si.hStdOutput   = hChildStdoutWrite;
            si.hStdInput    = hChildStdinRead;
            si.hStdError    = hChildStdoutWrite;
            si.dwFlags |= STARTF_USESTDHANDLES;

            if (createProcess(si)) {
                process_list.push(ProcessInformation{pi_.hProcess, hChildStdoutWrite});
                return Result::OK();
            }
        } catch (const std::exception& e) {
            return Result::Error(e.what());
        }

        return Result::Error("process creation failed");
    }

    void closesHandles() {
        if (hChildStdoutRead) CloseHandle(hChildStdoutRead);
        if (hChildStdoutWrite) CloseHandle(hChildStdoutWrite);
        if (hChildStdinRead) CloseHandle(hChildStdinRead);
        if (hChildStdinWrite) CloseHandle(hChildStdinWrite);
        hChildStdoutRead = hChildStdoutWrite = hChildStdinRead = hChildStdinWrite = NULL;
    }

    [[nodiscard]] Result alive() noexcept override {
        if (terminated_) return Result::Error("process terminated");

        assert(is_initialized_);

        DWORD exitCode = 0;

        return GetExitCodeProcess(pi_.hProcess, &exitCode) && exitCode == STILL_ACTIVE
                   ? Result::OK()
                   : Result::Error("process terminated");
    }

    bool setAffinity(const std::vector<int>& cpus) noexcept override {
        assert(is_initialized_);
        return affinity::setProcessAffinity(cpus, pi_.hProcess);
    }

    void terminate() {
        closesHandles();

        if (!is_initialized_) {
            return;
        }

        process_list.remove_if([this](const auto& pi) { return pi.identifier == pi_.hProcess; });

        DWORD exitCode        = 0;
        const auto start_time = std::chrono::steady_clock::now();

        // give the process time to die gracefully
        while (std::chrono::steady_clock::now() - start_time < IProcess::kill_timeout) {
            GetExitCodeProcess(pi_.hProcess, &exitCode);
            if (exitCode != STILL_ACTIVE) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (exitCode == STILL_ACTIVE) {
            LOG_TRACE_THREAD("Force terminating process with pid: {}", pi_.hProcess);
            TerminateProcess(pi_.hProcess, 1);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            GetExitCodeProcess(pi_.hProcess, &exitCode);
        } else {
            LOG_TRACE_THREAD("Process with pid: {} terminated with status: {}", pi_.hProcess, exitCode);
        }

        is_initialized_ = false;
        terminated_     = true;
    }

    void setupRead() override { current_line_.clear(); }

    // Read stdout until the line matches last_word or timeout is reached
    // 0 means no timeout
    Result readOutput(std::vector<Line>& lines, std::string_view last_word,
                      std::chrono::milliseconds threshold) override {
        assert(is_initialized_);

        lines.clear();

        const auto startTime = std::chrono::steady_clock::now();
        const auto timeout   = threshold.count() == 0 ? INFINITE : static_cast<DWORD>(threshold.count());

        while (true) {
            auto currentTime = std::chrono::steady_clock::now();
            auto elapsedMs   = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - startTime).count();

            if (elapsedMs >= timeout) {
                return Result{Status::TIMEOUT, "timeout"};
            }

            if (atomic::stop) {
                return Result::Error("stop requested");
            }

            DWORD bytesRead;
            OVERLAPPED overlapped = {};
            overlapped.hEvent     = CreateEvent(NULL, TRUE, FALSE, NULL);

            if (!overlapped.hEvent) {
                LOG_FATAL_THREAD("Failed to create event for overlapped I/O");
                return Result::Error("failed to create event for overlapped I/O");
            }

            if (!ReadFile(hChildStdoutRead, buffer.data(), buffer.size(), &bytesRead, &overlapped)) {
                if (GetLastError() != ERROR_IO_PENDING) {
                    CloseHandle(overlapped.hEvent);

                    if (!alive()) {
                        return Result::Error("process terminated");
                    }

                    LOG_FATAL_THREAD("Failed to read from pipe");
                    return Result::Error("failed to read from pipe");
                }

                HANDLE wait_handles[] = {overlapped.hEvent, pi_.hProcess};
                DWORD waitResult      = WaitForMultipleObjects(2, wait_handles, FALSE, timeout);
                if (waitResult == WAIT_TIMEOUT) {
                    CancelIo(hChildStdoutRead);
                    CloseHandle(overlapped.hEvent);
                    return Result{Status::TIMEOUT, "timeout"};
                }

                if (waitResult == WAIT_OBJECT_0 + 1) {
                    CloseHandle(overlapped.hEvent);

                    if (!alive()) {
                        return Result::Error("process terminated");
                    }

                    LOG_FATAL_THREAD("Process handle signaled but process is still alive");
                    return Result::Error("wait failed");
                }

                if (waitResult != WAIT_OBJECT_0) {
                    CloseHandle(overlapped.hEvent);
                    LOG_FATAL_THREAD("Wait failed");
                    return Result::Error("wait failed");
                }

                if (!GetOverlappedResult(hChildStdoutRead, &overlapped, &bytesRead, FALSE)) {
                    CloseHandle(overlapped.hEvent);

                    if (!alive()) {
                        return Result::Error("process terminated");
                    }

                    LOG_FATAL_THREAD("Failed to get overlapped result");
                    return Result::Error("failed to get overlapped result");
                }
            }

            CloseHandle(overlapped.hEvent);

            if (bytesRead == 0) {
                if (!alive()) {
                    return Result::Error("process terminated");
                }

                continue;
            }

            if (readBytes(buffer, bytesRead, lines, last_word)) {
                return Result::OK();
            }
        }
    }

    Result writeInput(const std::string& input) noexcept override {
        assert(is_initialized_);

        if (!alive()) {
            return Result::Error("process not alive");
        }

        DWORD bytes_written;
        auto res = WriteFile(hChildStdinWrite, input.c_str(), input.length(), &bytes_written, nullptr);

        return res ? Result::OK() : Result::Error("failed to write to stdin");
    }

   private:
    [[nodiscard]] bool readBytes(const std::array<char, buffer_size>& buffer, DWORD bytes_read,
                                 std::vector<Line>& lines, std::string_view searchword) {
        assert(bytes_read <= buffer.size());

        std::string_view data(buffer.data(), static_cast<size_t>(bytes_read));
        size_t start = 0;

        while (start < data.size()) {
            const size_t pos = data.find_first_of("\r\n", start);

            if (pos == std::string_view::npos) {
                current_line_.append(data.substr(start));
                break;
            }

            assert(pos >= start);
            assert(pos < data.size());

            current_line_.append(data.substr(start, pos - start));

            if (!current_line_.empty()) {
                addLine(lines);

                if (!searchword.empty() && current_line_.rfind(searchword, 0) == 0) {
                    current_line_.clear();
                    return true;
                }

                current_line_.clear();
            }

            // Treat "\r\n" as one delimiter.
            if (data[pos] == '\r' && pos + 1 < data.size() && data[pos + 1] == '\n') {
                start = pos + 2;
            } else {
                start = pos + 1;
            }

            assert(start <= data.size());
        }

        return false;
    }

    [[nodiscard]] bool createProcess(STARTUPINFOA& si) {
        const auto cmd = fmt::format("{} {}", command_, args_);

        const auto success = CreateProcessA(      //
            nullptr,                              //
            const_cast<char*>(cmd.c_str()),       //
            nullptr,                              //
            nullptr,                              //
            TRUE,                                 //
            CREATE_NEW_PROCESS_GROUP,             //
            nullptr,                              //
            wd_.empty() ? nullptr : wd_.c_str(),  //
            &si,                                  //
            &pi_                                  //
        );

        is_initialized_ = true;

        return success;
    }

    void addLine(std::vector<Line>& lines) const {
        const auto timestamp = Logger::should_log_ ? time::datetime_precise() : "";

        lines.emplace_back(Line{current_line_, timestamp});

        if (realtime_logging_) {
            Logger::readFromEngine(current_line_, timestamp, log_name_, false, thread_id);
        }
    }

    // The working directory
    std::string wd_;
    // The command to execute
    std::string command_;
    // The arguments for the engine
    std::string args_;
    // The name in the log file
    std::string log_name_;
    // The current line read from the engine, avoids reallocations
    std::string current_line_;

    // True if the process has been initialized
    bool is_initialized_ = false;
    bool terminated_     = false;

    PROCESS_INFORMATION pi_;

    const std::thread::id thread_id = std::this_thread::get_id();
};

}  // namespace engine::process
}  // namespace fastchess

#endif
