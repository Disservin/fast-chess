#include "uci_engine.h"
#include "engine_config.h"

#include <sstream>
#include <stdexcept>

void UciEngine::setConfig(const EngineConfiguration &rhs)
{
    config = rhs;
}

void UciEngine::sendUciNewGame()
{
    bool timeout = false;
    writeProcess("ucinewgame");
    isResponsive();
}

void UciEngine::sendUci()
{
    writeProcess("uci");
}

std::vector<std::string> UciEngine::readUci()
{
    bool timeout = false;
    return readProcess("uciok", timeout);
}

std::string UciEngine::buildGoInput()
{
    std::stringstream input;
    input << "go";

    if (config.nodes != 0)
        input << " nodes " << config.nodes;

    if (config.plies != 0)
        input << " depth " << config.plies;

    if (color == WHITE)
    {
        if (config.tc.time != 0)
        {
            input << " wtime " << config.tc.time;
        }

        if (config.tc.increment != 0)
        {
            input << " winc " << config.tc.increment;
        }

        if (config.tc.moves != 0)
        {
            input << " movestogo " << config.tc.moves;
        }
    }

    return input.str();
}

void UciEngine::loadConfig(const EngineConfiguration &config)
{
    this->config = config;
}

void UciEngine::sendQuit()
{
    writeProcess("quit");
}

void UciEngine::sendSetoption(const std::string &name, const std::string &value)
{
    writeProcess("setoption name " + name + " value " + value);
}

void UciEngine::sendGo(const std::string &limit)
{
    writeProcess("go " + limit);
}

void UciEngine::startEngine()
{
    initProcess(config.cmd);
    sendUci();
    readUci();
    if (!pingEngine())
    {
        throw std::runtime_error("Something went wrong when pinging the engine.");
    }

    /*
    TODO: set all the engine options
    sendSetoption("Hash", "16");
    */
}

void UciEngine::stopEngine()
{
    sendQuit();
    killProcess();
}