#include "uci_engine.hpp"

#include <sstream>
#include <stdexcept>

#include "../logger.hpp"
#include "engine_config.hpp"
namespace fast_chess
{

void UciEngine::setConfig(const EngineConfiguration &rhs)
{
    config_ = rhs;
}

EngineConfiguration UciEngine::getConfig() const
{
    return config_;
}

std::string UciEngine::checkErrors(int id)
{
    if (!getError().empty())
    {
        std::stringstream ss;
        ss << getError() << "\nCant write to engine " << getConfig().name
           << (id != -1 ? ("# " + std::to_string(id)) : "");

        if (!getConfig().recover)
        {
            throw std::runtime_error(ss.str());
        }
        return ss.str();
    }
    return "";
}

bool UciEngine::isResponsive(int64_t threshold)
{
    if (!isAlive())
        return false;

    bool timeout = false;
    Logger::coutInfo("Responsive test");

    writeProcess("isready");
    readProcess("readyok", timeout, threshold);
    return !timeout;
}

void UciEngine::sendUciNewGame()
{
    writeProcess("ucinewgame");
    isResponsive(60000);
}

void UciEngine::sendUci()
{
    Logger::coutInfo("write uci");
    writeProcess("uci");
}

std::vector<std::string> UciEngine::readUci()
{
    bool timeout = false;
    Logger::coutInfo("read uci");

    return readProcess("uciok", timeout, ping_time_);
}

std::string UciEngine::buildGoInput(Color stm, const TimeControl &tc, const TimeControl &tc_2) const
{
    std::stringstream input;
    input << "go";

    if (config_.nodes != 0)
        input << " nodes " << config_.nodes;

    if (config_.plies != 0)
        input << " depth " << config_.plies;

    if (tc.fixed_time != 0)
    {
        input << " movetime " << tc.fixed_time;
    }
    // We cannot use st and tc together
    else
    {
        auto white = stm == WHITE ? tc : tc_2;
        auto black = stm == WHITE ? tc_2 : tc;

        if (tc.time != 0)
        {

            input << " wtime " << white.time << " btime " << black.time;
        }

        if (tc.increment != 0)
        {
            input << " winc " << white.increment << " binc " << black.increment;
        }

        if (tc.moves != 0)
            input << " movestogo " << tc.moves;
    }
    return input.str();
}

void UciEngine::loadConfig(const EngineConfiguration &config)
{
    this->config_ = config;
}

void UciEngine::sendQuit()
{
    writeProcess("quit");
    checkErrors();
}

void UciEngine::sendSetoption(const std::string &name, const std::string &value)
{
    writeProcess("setoption name " + name + " value " + value);
}

void UciEngine::sendGo(const std::string &limit)
{
    writeProcess("go " + limit);
}

void UciEngine::restartEngine()
{
    resetError();
    killProcess();
    initProcess(config_.cmd);
}

void UciEngine::startEngine()
{
    initProcess(config_.cmd);

    sendUci();
    readUci();

    if (!isResponsive(60000))
    {
        throw std::runtime_error("Warning: Something went wrong when pinging the engine.");
    }

    for (const auto &option : config_.options)
    {
        sendSetoption(option.first, option.second);
    }
}

void UciEngine::startEngine(const std::string &cmd)
{
    Logger::coutInfo("init process:", cmd);
    initProcess(cmd);

    sendUci();
    readUci();

    if (!isResponsive(60000))
    {
        throw std::runtime_error("Warning: Something went wrong when pinging the engine.");
    }

    for (const auto &option : config_.options)
    {
        Logger::coutInfo("send ucioption", option.first, option.second);
        sendSetoption(option.first, option.second);
    }
}

} // namespace fast_chess
