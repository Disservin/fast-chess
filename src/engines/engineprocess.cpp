#include "engineprocess.hpp"

#include <cassert>
#include <chrono>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>

#include "../logger.hpp"
#include "../options.hpp"

namespace fast_chess
{

EngineProcess::EngineProcess(const std::string &command)
{
    initProcess(command);
}

#ifdef _WIN64

void EngineProcess::initProcess(const std::string &command)
{
    is_initalized_ = true;
    STARTUPINFOA si = STARTUPINFOA();
    si.dwFlags = STARTF_USESTDHANDLES;

    SECURITY_ATTRIBUTES sa = SECURITY_ATTRIBUTES();
    sa.bInheritHandle = TRUE;

    HANDLE childStdOutRd, childStdOutWr;
    CreatePipe(&childStdOutRd, &childStdOutWr, &sa, 0);
    SetHandleInformation(childStdOutRd, HANDLE_FLAG_INHERIT, 0);
    si.hStdOutput = childStdOutWr;

    HANDLE childStdInRd, childStdInWr;
    CreatePipe(&childStdInRd, &childStdInWr, &sa, 0);
    SetHandleInformation(childStdInWr, HANDLE_FLAG_INHERIT, 0);
    si.hStdInput = childStdInRd;

    CreateProcessA(nullptr, const_cast<char *>(command.c_str()), nullptr, nullptr, TRUE, 0, nullptr,
                   nullptr, &si, &pi_);

    CloseHandle(childStdOutWr);
    CloseHandle(childStdInRd);

    child_std_out_ = childStdOutRd;
    child_std_in_ = childStdInWr;
}

void EngineProcess::closeHandles()
{
    try
    {
        CloseHandle(pi_.hThread);
        CloseHandle(pi_.hProcess);

        CloseHandle(child_std_out_);
        CloseHandle(child_std_in_);
    }
    catch (const std::exception &)
    {
        err_code_ = 1;
        err_str_ = "Error in closing handles.";
    }
}

EngineProcess::~EngineProcess()
{
    killProcess();
}

std::vector<std::string> EngineProcess::readProcess(std::string_view last_word, bool &timeout,
                                                    int64_t timeoutThreshold)
{
    assert(is_initalized_);

    std::vector<std::string> lines;
    lines.reserve(30);

    std::string currentLine;
    currentLine.reserve(300);

    char buffer[4096];
    DWORD bytesRead;
    DWORD bytesAvail = 0;

    int checkTime = 255;

    timeout = false;

    auto start = std::chrono::high_resolution_clock::now();

    while (true)
    {

        // Check if timeout milliseconds have elapsed
        if (timeoutThreshold > 0 && checkTime-- == 0)
        {
            /* To achieve "non blocking" file reading on windows with anonymous pipes the only
            solution that I found was using peeknamedpipe however it turns out this function is
            terribly slow and leads to timeouts for the engines. Checking this only after n runs
            seems to reduce the impact of this. For high concurrency windows setups timeoutThreshold
            should probably be 0. Using the assumption that the engine works rather clean and is
            able to send the last word.*/
            if (!PeekNamedPipe(child_std_out_, NULL, 0, 0, &bytesAvail, nullptr))
            {
                err_code_ = 1;
                err_str_ = "Cant peek pipe.";
            }

            if (std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::high_resolution_clock::now() - start)
                    .count() > timeoutThreshold)
            {
                lines.emplace_back(currentLine);
                timeout = true;
                break;
            }

            checkTime = 255;
        }

        // no new bytes to read
        if (timeoutThreshold > 0 && bytesAvail == 0)
            continue;

        if (!ReadFile(child_std_out_, buffer, sizeof(buffer), &bytesRead, nullptr))
        {
            err_code_ = 1;
            err_str_ = "Cant read process correctly.";
        }

        // Iterate over each character in the buffer
        for (DWORD i = 0; i < bytesRead; i++)
        {
            // If we encounter a newline, add the current line to the vector and reset the
            // currentLine on windows newlines are \r\n
            if (buffer[i] == '\n' || buffer[i] == '\r')
            {
                // dont add empty lines
                if (!currentLine.empty())
                {
                    lines.emplace_back(currentLine);

                    Logger::readLog(currentLine, std::this_thread::get_id());

                    if (currentLine.rfind(last_word, 0) == 0)
                    {
                        return lines;
                    }

                    currentLine = "";
                }
            }
            // Otherwise, append the character to the current line
            else
            {
                currentLine += buffer[i];
            }
        }
    }

    return lines;
}

void EngineProcess::writeProcess(const std::string &input)
{
    assert(is_initalized_);

    try
    {
        if (!isAlive())
        {
            closeHandles();

            std::stringstream ss;
            ss << "Process is not alive and write occured with message: " << input << "\n";
            std::cout << ss.str();
            return;
        }

        Logger::writeLog(input, std::this_thread::get_id());

        constexpr char endLine = '\n';
        DWORD bytesWritten;
        WriteFile(child_std_in_, input.c_str(), input.length(), &bytesWritten, nullptr);
        WriteFile(child_std_in_, &endLine, 1, &bytesWritten, nullptr);
    }
    catch (const std::exception &e)
    {
        err_code_ = 1;
        err_str_ = "Error in writing to process.\n" + std::string(e.what());
    }
}

bool EngineProcess::isAlive()
{
    assert(is_initalized_);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi_.hProcess, &exitCode);
    return exitCode == STILL_ACTIVE;
}

void EngineProcess::killProcess()
{
    if (is_initalized_)
    {
        try
        {
            DWORD exitCode = 0;
            GetExitCodeProcess(pi_.hProcess, &exitCode);
            if (exitCode == STILL_ACTIVE)
            {
                UINT uExitCode = 0;
                TerminateProcess(pi_.hProcess, uExitCode);
            }
            // Clean up the child process resources
            closeHandles();
        }
        catch (const std::exception &e)
        {
            err_code_ = 1;
            err_str_ = "Error in writing to process.\n" + std::string(e.what());
        }
    }
}
#else

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

void EngineProcess::initProcess(const std::string &command)
{
    is_initalized_ = true;
    // Create input pipe
    if (pipe(in_pipe_) == -1)
    {
        perror("Failed to create input pipe");
        exit(1);
    }

    // Create output pipe
    if (pipe(out_pipe_) == -1)
    {
        perror("Failed to create output pipe");
        exit(1);
    }

    // Fork the current process
    pid_t forkPid = fork();

    if (forkPid < 0)
    {
        perror("Error: InitProcess");
        exit(1);
    }

    // If this is the child process, set up the pipes and start the engine
    if (forkPid == 0)
    {
        // Redirect the child's standard input to the read end of the output pipe
        if (dup2(out_pipe_[0], 0) == -1)
            perror("Failed to duplicate outpipe");

        if (close(out_pipe_[0]) == -1)
            perror("Failed to close outpipe");

        // Redirect the child's standard output to the write end of the input pipe
        if (dup2(in_pipe_[1], 1) == -1)
            perror("Failed to duplicate inpipe");

        if (close(in_pipe_[1]) == -1)
            perror("Failed to close inpipe");

        // Execute the engine
        if (execl(command.c_str(), command.c_str(), (char *)NULL) == -1)
            perror("Error: Execute");

        perror("Error: in child");
        exit(1);
    }
    else
    {
        process_pid_ = forkPid;
    }
}

EngineProcess::~EngineProcess()
{
    killProcess();
}

void EngineProcess::writeProcess(const std::string &input)
{
    assert(is_initalized_);

    if (!isAlive())
    {
        err_code_ = 1;
        err_str_ = "Error in writing process.";

        std::stringstream ss;
        ss << "Process is not alive and write occured with message: " << input << "\n";
        std::cout << ss.str();
        return;
    }

    // Append a newline character to the end of the input string
    constexpr char endLine = '\n';
    Logger::coutInfo("writing process", input);

    // Write the input and a newline to the output pipe
    if (write(out_pipe_[1], input.c_str(), input.size()) == -1)
    {
        std::stringstream ss;
        ss << "Process is not alive and write occured with message: " << input;
        std::cout << ss.str();
        err_code_ = 1;
        err_str_ = ss.str();
        perror("Error: write: ");
    }

    if (write(out_pipe_[1], &endLine, 1) == -1)
    {
        std::stringstream ss;
        ss << "Process is not alive and write occured with message: " << input;
        std::cout << ss.str();
        err_code_ = 1;
        err_str_ = ss.str();
        perror("Error: write\n: ");
    }
}
std::vector<std::string> EngineProcess::readProcess(std::string_view last_word, bool &timeout,
                                                    int64_t timeoutThreshold)
{
    assert(is_initalized_);

    // Disable blocking
    fcntl(in_pipe_[0], F_SETFL, fcntl(in_pipe_[0], F_GETFL) | O_NONBLOCK);

    std::vector<std::string> lines;
    lines.reserve(30);

    std::string currentLine;
    currentLine.reserve(300);

    char buffer[4096];
    timeout = false;

    struct pollfd pollfds[1];
    pollfds[0].fd = in_pipe_[0];
    pollfds[0].events = POLLIN;

    // Set up the timeout for poll
    int timeoutMillis = timeoutThreshold;
    if (timeoutMillis <= 0)
    {
        timeoutMillis = -1; // wait indefinitely
    }

    // Continue reading output lines until the line matches the specified line or a timeout occurs
    while (true)
    {
        const int ret = poll(pollfds, 1, timeoutMillis);

        if (ret == -1)
        {
            err_code_ = 1;
            err_str_ = "Error poll";
            perror("Error: poll: ");
        }
        else if (ret == 0)
        {
            // timeout
            lines.emplace_back(currentLine);
            timeout = true;
            break;
        }
        else if (pollfds[0].revents & POLLIN)
        {
            // input available on the pipe
            const int bytesRead = read(in_pipe_[0], buffer, sizeof(buffer));

            if (bytesRead == -1)
            {
                err_code_ = 1;
                err_str_ = "Error read";
                perror("Error: read");
            }
            // Iterate over each character in the buffer
            for (int i = 0; i < bytesRead; i++)
            {
                // If we encounter a newline, add the current line to the vector and reset the
                // currentLine
                if (buffer[i] == '\n')
                {
                    // dont add empty lines
                    if (!currentLine.empty())
                    {
                        Logger::coutInfo("reading process", currentLine);

                        lines.emplace_back(currentLine);
                        if (currentLine.rfind(last_word, 0) == 0)
                        {
                            return lines;
                        }
                        currentLine = "";
                    }
                }
                // Otherwise, append the character to the current line
                else
                {
                    currentLine += buffer[i];
                }
            }
        }
    }

    return lines;
}

bool EngineProcess::isAlive()
{
    assert(is_initalized_);
    int status;

    const pid_t r = waitpid(process_pid_, &status, WNOHANG);
    if (r == -1)
    {
        perror("isAlive Error: ");
        return false;
    }
    else
    {
        return r == 0;
    }
}

void EngineProcess::killProcess()
{
    if (is_initalized_)
    {
        close(in_pipe_[0]);
        close(in_pipe_[1]);
        close(out_pipe_[0]);
        close(out_pipe_[1]);

        int status;
        pid_t r = waitpid(process_pid_, &status, WNOHANG);

        if (r == 0)
        {
            kill(process_pid_, SIGKILL);
            wait(NULL);
        }
    }
}

#endif
} // namespace fast_chess
