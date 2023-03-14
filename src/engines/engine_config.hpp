#pragma once

#include <string>
#include <tuple>
#include <vector>

#include "../third_party/json.hpp"

namespace fast_chess
{

/*
Modified version of the NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE macro in nlohmann's json lib.
ordered_json type conversion is not yet supported, though we only have to change the type.
*/
#define NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_ORDERED_JSON(Type, ...)  \
    inline void to_json(nlohmann::ordered_json& nlohmann_json_j, const Type& nlohmann_json_t) { NLOHMANN_JSON_EXPAND(NLOHMANN_JSON_PASTE(NLOHMANN_JSON_TO, __VA_ARGS__)) } \
    inline void from_json(const nlohmann::ordered_json& nlohmann_json_j, Type& nlohmann_json_t) { NLOHMANN_JSON_EXPAND(NLOHMANN_JSON_PASTE(NLOHMANN_JSON_FROM, __VA_ARGS__)) }

struct TimeControl
{
    int moves = 0;
    int64_t time = 0;
    uint64_t increment = 0;
    int64_t fixed_time = 0;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_ORDERED_JSON(TimeControl, moves, time, increment, fixed_time);

inline std::ostream &operator<<(std::ostream &os, const TimeControl &tc)
{
    if (tc.moves > 0)
        os << tc.moves << "/";

    os << (tc.time / 1000.0);

    if (tc.increment > 0)
        os << "+" << (tc.increment / 1000.0);

    return os;
}

struct EngineConfiguration
{
    // engine name
    std::string name;

    // Path to engine
    std::string dir;

    // Engine binary name
    std::string cmd;

    // Custom args that should be sent
    std::string args;

    // UCI options
    std::vector<std::pair<std::string, std::string>> options;

    // time control for the engine
    TimeControl tc;

    // Node limit for the engine
    uint64_t nodes = 0;

    // Ply limit for the engine
    uint64_t plies = 0;

    bool recover = false;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_ORDERED_JSON(EngineConfiguration, name, dir, cmd, args, options, tc, nodes,
                                   plies, recover);

} // namespace fast_chess
