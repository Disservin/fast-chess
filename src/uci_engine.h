#pragma once

#include "engine.h"
#include "types.h"

enum class Turn
{
    FIRST,
    SECOND
};

constexpr Turn operator~(Turn t)
{
    return Turn(static_cast<int>(t) ^ static_cast<int>(Turn::SECOND));
}

class UciEngine : public Engine
{

  private:
    static const int64_t PING_TIME = 60000;

  public:
    UciEngine() : Engine()
    {
    }

    UciEngine(EngineConfiguration config)
    {
        setConfig(config);
    }

    ~UciEngine()
    {
        sendQuit();
    }

    bool isResponsive(int64_t threshold = PING_TIME);
    void sendUciNewGame();

    void sendUci();
    std::vector<std::string> readUci();

    std::string buildGoInput(Color stm, const TimeControl &tc, const TimeControl &tc_2);

    void loadConfig(const EngineConfiguration &config);

    void sendQuit();
    void sendSetoption(const std::string &name, const std::string &value);
    void sendGo(const std::string &limit);

    void setConfig(const EngineConfiguration &rhs);

    void startEngine();
    void startEngine(const std::string &cmd);

    Turn turn;
};
