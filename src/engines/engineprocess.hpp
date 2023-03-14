#pragma once

#include <cstdint>
#include <string>
#include <vector>

#ifdef _WIN64
#include <windows.h>
#endif

namespace fast_chess
{

class Process
{
  public:
    // Read engine's stdout until the line matches last_word or timeout is reached
    virtual std::vector<std::string> readProcess(std::string_view last_word, bool &timeout,
                                                 int64_t timeoutThreshold = 1000) = 0;
    // Write input to the engine's stdin
    virtual void writeProcess(const std::string &input) = 0;

    // Returns true if the engine process is alive
    virtual bool isAlive() = 0;

    bool is_initalized_ = false;

    int err_code_ = 0;
    std::string err_str_;

    std::string getError()
    {
        if (err_code_ != 0)
            return err_str_ + ": " + std::to_string(err_code_) + "\n";
        return "";
    }

    void resetError()
    {
        err_code_ = 0;
        err_str_ = "";
    }
};

#ifdef _WIN64

class EngineProcess : public Process
{
  public:
    EngineProcess() = default;

    EngineProcess(const std::string &command);

    ~EngineProcess();

    void initProcess(const std::string &command);

    void killProcess();

    void closeHandles();

    virtual bool isAlive() override;

    virtual std::vector<std::string> readProcess(std::string_view last_word, bool &timeout,
                                                 int64_t timeoutThreshold = 1000) override;
    virtual void writeProcess(const std::string &input) override;

  private:
    PROCESS_INFORMATION pi_ = PROCESS_INFORMATION();
    HANDLE child_std_out_;
    HANDLE child_std_in_;
};

#else

class EngineProcess : public Process
{
  public:
    EngineProcess() = default;

    EngineProcess(const std::string &command);

    ~EngineProcess();

    void initProcess(const std::string &command);

    void killProcess();

    virtual bool isAlive() override;

    virtual std::vector<std::string> readProcess(std::string_view last_word, bool &timeout,
                                                 int64_t timeoutThreshold = 1000) override;
    virtual void writeProcess(const std::string &input) override;

  private:
    pid_t process_pid_;
    int in_pipe_[2], out_pipe_[2];
};

#endif

} // namespace fast_chess