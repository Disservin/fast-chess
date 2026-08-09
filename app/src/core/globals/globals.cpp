#include <core/globals/globals.hpp>

#include <cassert>

#ifdef _WIN64
#    include <windows.h>
#else
#    include <signal.h>
#    include <unistd.h>
#    include <cstdlib>
#endif

#include <core/logger/logger.hpp>
#include <core/threading/thread_vector.hpp>

namespace fastchess {

namespace atomic {
std::atomic_bool stop                 = false;
std::atomic_bool abnormal_termination = false;
}  // namespace atomic

util::ThreadVector<ProcessInformation> process_list;

void writeToOpenPipes() {
    const uint64_t dummy_data = 1;

    process_list.lock();

    for (const auto& process : process_list) {
        LOG_TRACE("Writing to process with pid/handle: {}", process.identifier);
#ifdef _WIN64
        [[maybe_unused]] DWORD bytes_written;
        WriteFile(process.fd_write, &dummy_data, sizeof(dummy_data), &bytes_written, nullptr);
#else
        [[maybe_unused]] ssize_t bytes_written;
        bytes_written = write(process.fd_write, &dummy_data, sizeof(dummy_data));
#endif
        assert(bytes_written == sizeof(dummy_data));
    }

    process_list.unlock();
}

void stopProcesses() {
    process_list.lock();

    for (const auto& process : process_list) {
        LOG_TRACE("Cleaning up process with pid/handle: {}", process.identifier);

#ifdef _WIN64
        TerminateProcess(process.identifier, 1);
        CloseHandle(process.identifier);
#else
        kill(process.identifier, SIGINT);
        kill(process.identifier, SIGKILL);
#endif
    }

    process_list.unlock();
}

void consoleHandlerAction() {
    // Logger::print<Logger::Level::WARN>("Received signal, stopping tournament.");

    atomic::stop                 = true;
    atomic::abnormal_termination = true;
}

#ifdef _WIN64
BOOL WINAPI handler(DWORD signal) {
    switch (signal) {
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
            consoleHandlerAction();
        default:
            break;
    }
    return TRUE;
}

void setCtrlCHandler() {
    if (!SetConsoleCtrlHandler(handler, TRUE)) {
        LOG_FATAL("Could not set control handler.");
    }
}

#else
void handler(int, siginfo_t*, void*) { consoleHandlerAction(); }

void setCtrlCHandler() {
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));

    // Let pipe writes report EPIPE so engine failures can be handled normally.
    sa.sa_handler = SIG_IGN;
    if (sigaction(SIGPIPE, &sa, NULL) == -1) {
        perror("Error ignoring SIGPIPE");
    }

    sa.sa_sigaction = handler;
    sa.sa_flags     = SA_SIGINFO;

    sigfillset(&sa.sa_mask);

    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("Error setting up signal handler");
    }
}
#endif

}  // namespace fastchess
