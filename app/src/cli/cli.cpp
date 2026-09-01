#include <cli/cli.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <limits>
#include <sstream>
#include <string_view>
#include <type_traits>

#include <cli/cli_args.hpp>
#include <cli/sanitize.hpp>
#include <core/helper.hpp>
#include <core/logger/logger.hpp>
#include <core/rand.hpp>
#include <matchmaking/output/output_factory.hpp>
#include <matchmaking/scoreboard.hpp>
#include <types/engine_config.hpp>
#include <types/exception.hpp>
#include <types/tournament.hpp>

namespace {
template <typename T>
T parseScalar(std::string_view value) {
    std::string str(value);

    if constexpr (std::is_same_v<T, bool>) {
        if (str == "true") return true;
        if (str == "false") return false;
        throw fastchess::fastchess_exception::format("Expected boolean value (true/false), got: {}", str);
    } else if constexpr (std::is_same_v<T, std::string>) {
        return str;
    } else {
        static_assert(std::is_arithmetic_v<T>, "parseScalar only supports arithmetic and string types");

        if (str.empty() || std::any_of(str.begin(), str.end(), [](unsigned char c) { return std::isspace(c); })) {
            throw fastchess::fastchess_exception::format("Invalid numeric value: \"{}\"", str);
        }

        try {
            if constexpr (std::is_floating_point_v<T>) {
                std::size_t parsed_length = 0;
                const auto parsed         = std::stold(str, &parsed_length);
                if (parsed_length != str.size() || !std::isfinite(parsed) ||
                    parsed < std::numeric_limits<T>::lowest() || parsed > std::numeric_limits<T>::max()) {
                    throw fastchess::fastchess_exception::format("Invalid numeric value: \"{}\"", str);
                }
                return static_cast<T>(parsed);
            } else {
                const auto parsed = fastchess::str_utils::parseInteger<T>(value);
                if (!parsed.has_value()) {
                    throw fastchess::fastchess_exception::format("Invalid numeric value: \"{}\"", str);
                }
                return *parsed;
            }
        } catch (const fastchess::fastchess_exception&) {
            throw;
        } catch (const std::exception&) {
            throw fastchess::fastchess_exception::format("Invalid numeric value: \"{}\"", str);
        }
    }
}

std::pair<std::string_view, std::string_view> splitTimeControl(std::string_view value, char delimiter) {
    const auto separator = value.find(delimiter);
    if (separator == std::string_view::npos || separator == 0 || separator + 1 == value.size() ||
        value.find(delimiter, separator + 1) != std::string_view::npos) {
        throw fastchess::fastchess_exception::format("Invalid time control: \"{}\"", value);
    }

    return {value.substr(0, separator), value.substr(separator + 1)};
}

int64_t parseDuration(std::string_view value, int64_t multiplier) {
    if (value.size() > 1 && value.back() == 's' && multiplier == 1000) {
        value.remove_suffix(1);
    }

    const auto duration = parseScalar<long double>(value);
    const auto scaled   = static_cast<long double>(duration) * multiplier;
    const auto rounded  = std::round(scaled);

    if (duration < 0 || rounded > std::numeric_limits<int64_t>::max()) {
        throw fastchess::fastchess_exception::format("Invalid time control duration: \"{}\"", value);
    }

    return static_cast<int64_t>(rounded);
}

// Parse a list of integers on the form 5,10,13-17,23 -> 5,10,13,14,15,16,17,23
bool parseIntList(std::istringstream& iss, std::vector<int>& list) {
    int int1, int2;
    char c;

    if (!(iss >> int1)) return true;
    list.emplace_back(int1);
    if (iss >> c) {
        switch (c) {
            case '-':
                if (!(iss >> int2) || int2 <= int1) return true;
                for (int i = int1 + 1; i <= int2; i++) list.emplace_back(i);
                if (!(iss >> c))
                    return false;
                else if (c == ',')
                    return parseIntList(iss, list);
                return true;
                break;
            case ',':
                return parseIntList(iss, list);
            default:
                return true;
        }
    }
    return false;
}

std::string concat(const std::vector<std::string>& params) {
    std::string str;

    for (const auto& param : params) {
        str += param;
    }

    return str;
}

bool is_number(const std::string& s) {
    static const auto is_digit = [](unsigned char c) { return !std::isdigit(c); };
    return !s.empty() && std::find_if(s.begin(), s.end(), is_digit) == s.end();
}

bool is_bool(const std::string& s) { return s == "true" || s == "false"; }

}  // namespace

namespace fastchess::cli {
using json          = nlohmann::json;
using KeyValuePairs = std::vector<std::pair<std::string, std::string>>;

namespace engine {
TimeControl::Limits parseTc(const std::string& tcString) {
    if (str_utils::contains(tcString, "hg")) throw fastchess_exception("Hourglass time control not supported.");
    if (tcString == "infinite" || tcString == "inf") return {};
    if (tcString.empty()) throw fastchess_exception("Invalid time control: empty value");

    TimeControl::Limits tc;

    std::string_view remaining = tcString;

    if (remaining.find('/') != std::string_view::npos) {
        const auto [moves, rest] = splitTimeControl(remaining, '/');
        if (moves == "inf" || moves == "infinite") {
            tc.moves = 0;
        } else {
            tc.moves = parseScalar<int64_t>(moves);
            if (tc.moves <= 0) throw fastchess_exception("Time control move count must be positive");
        }
        remaining = rest;
    }

    if (remaining.find('+') != std::string_view::npos) {
        const auto [time, increment] = splitTimeControl(remaining, '+');
        tc.increment                 = parseDuration(increment, 1000);
        remaining                    = time;
    }

    if (remaining.find(':') != std::string_view::npos) {
        const auto [minutes, seconds] = splitTimeControl(remaining, ':');
        const auto minute_duration    = parseDuration(minutes, 60 * 1000);
        const auto second_duration    = parseDuration(seconds, 1000);

        if (second_duration > std::numeric_limits<int64_t>::max() - minute_duration) {
            throw fastchess_exception::format("Invalid time control duration: \"{}\"", remaining);
        }
        tc.time = minute_duration + second_duration;
    } else {
        tc.time = parseDuration(remaining, 1000);
    }

    return tc;
}

bool isEngineSettableOption(std::string_view stringFormat) { return str_utils::startsWith(stringFormat, "option."); }

void parseEngineKeyValues(EngineConfiguration& engineConfig, const std::string& key, const std::string& value) {
    if (key == "cmd") {
        engineConfig.cmd = value;
    } else if (key == "name")
        engineConfig.name = value;
    else if (key == "tc")
        engineConfig.limit.tc = parseTc(value);
    else if (key == "st")
        engineConfig.limit.tc.fixed_time = parseDuration(value, 1000);
    else if (key == "timemargin") {
        engineConfig.limit.tc.timemargin = parseScalar<int64_t>(value);
        if (engineConfig.limit.tc.timemargin < 0) {
            throw fastchess_exception("The value for timemargin cannot be a negative number.");
        }
    } else if (key == "nodes")
        engineConfig.limit.nodes = parseScalar<int64_t>(value);
    else if (key == "plies" || key == "depth")
        engineConfig.limit.plies = parseScalar<int64_t>(value);
    else if (key == "dir")
        engineConfig.dir = value;
    else if (key == "args")
        engineConfig.args = value;
    else if (key == "restart") {
        if (value != "on" && value != "off") {
            throw fastchess_exception::format("Invalid parameter (must be either \"on\" or \"off\"): {}", value);
        }
        engineConfig.restart = value == "on";
    } else if (isEngineSettableOption(key)) {
        // Strip option.Name of the option. Part
        const std::size_t pos         = key.find('.');
        const std::string strippedKey = key.substr(pos + 1);
        engineConfig.options.emplace_back(strippedKey, value);
    } else if (key == "proto") {
        if (value != "uci") {
            throw fastchess_exception("Unsupported protocol.");
        }
    } else
        OptionsParser::throwMissing("engine", key, value);
}

void parseEngine(const KeyValuePairs& params, ArgumentData& argument_data) {
    argument_data.configs.emplace_back();

    for (const auto& [key, value] : params) {
        engine::parseEngineKeyValues(argument_data.configs.back(), key, value);
    }
}

}  // namespace engine

void parseEach(const KeyValuePairs& params, ArgumentData& argument_data) {
    for (const auto& [key, value] : params) {
        for (auto& config : argument_data.configs) {
            engine::parseEngineKeyValues(config, key, value);
        }
    }
}

void parsePgnOut(const KeyValuePairs& params, ArgumentData& argument_data) {
    argument_data.tournament_config.pgn.file =
        fmt::format("fastchess_{}.pgn", time::datetime("%Y%m%d_%H%M%S").value_or(""));

    for (const auto& [key, value] : params) {
        if (key == "file") {
            argument_data.tournament_config.pgn.file = value;
        } else if (key == "append" && is_bool(value)) {
            argument_data.tournament_config.pgn.append_file = value == "true";
        } else if (key == "nodes" && is_bool(value)) {
            argument_data.tournament_config.pgn.track_nodes = value == "true";
        } else if (key == "seldepth" && is_bool(value)) {
            argument_data.tournament_config.pgn.track_seldepth = value == "true";
        } else if (key == "nps" && is_bool(value)) {
            argument_data.tournament_config.pgn.track_nps = value == "true";
        } else if (key == "hashfull" && is_bool(value)) {
            argument_data.tournament_config.pgn.track_hashfull = value == "true";
        } else if (key == "tbhits" && is_bool(value)) {
            argument_data.tournament_config.pgn.track_tbhits = value == "true";
        } else if (key == "timeleft" && is_bool(value)) {
            argument_data.tournament_config.pgn.track_timeleft = value == "true";
        } else if (key == "latency" && is_bool(value)) {
            argument_data.tournament_config.pgn.track_latency = value == "true";
        } else if (key == "min" && is_bool(value)) {
            argument_data.tournament_config.pgn.min = value == "true";
        } else if (key == "notation") {
            if (value == "san") {
                argument_data.tournament_config.pgn.notation = NotationType::SAN;
            } else if (value == "lan") {
                argument_data.tournament_config.pgn.notation = NotationType::LAN;
            } else if (value == "uci") {
                argument_data.tournament_config.pgn.notation = NotationType::UCI;
            } else {
                OptionsParser::throwMissing("pgnout notation", key, value);
            }
        } else if (key == "match_line") {
            argument_data.tournament_config.pgn.additional_lines_rgx.push_back(value);
        } else if (key == "pv" && is_bool(value)) {
            argument_data.tournament_config.pgn.track_pv = value == "true";
        } else {
            OptionsParser::throwMissing("pgnout", key, value);
        }
    }
}

void parseEpdOut(const KeyValuePairs& params, ArgumentData& argument_data) {
    argument_data.tournament_config.epd.file =
        fmt::format("fastchess_{}.epd", time::datetime("%Y%m%d_%H%M%S").value_or(""));

    for (const auto& [key, value] : params) {
        if (key == "file") {
            argument_data.tournament_config.epd.file = value;
        } else if (key == "append" && is_bool(value)) {
            argument_data.tournament_config.epd.append_file = value == "true";
        } else {
            OptionsParser::throwMissing("epdout", key, value);
        }
    }
}

void parseOpening(const KeyValuePairs& params, ArgumentData& argument_data) {
    for (const auto& [key, value] : params) {
        if (key == "file") {
            argument_data.tournament_config.opening.file = value;
            if (str_utils::endsWith(value, ".epd")) {
                argument_data.tournament_config.opening.format = FormatType::EPD;
            } else if (str_utils::endsWith(value, ".pgn")) {
                argument_data.tournament_config.opening.format = FormatType::PGN;
            }

            if (!std::filesystem::exists(value)) {
                throw fastchess_exception::format("Opening file does not exist: {}", value);
            }
        } else if (key == "format") {
            if (value == "epd") {
                argument_data.tournament_config.opening.format = FormatType::EPD;
            } else if (value == "pgn") {
                argument_data.tournament_config.opening.format = FormatType::PGN;
            } else {
                OptionsParser::throwMissing("openings format", key, value);
            }
        } else if (key == "order") {
            if (value == "sequential") {
                argument_data.tournament_config.opening.order = OrderType::SEQUENTIAL;
            } else if (value == "random") {
                argument_data.tournament_config.opening.order = OrderType::RANDOM;
            } else {
                OptionsParser::throwMissing("openings order", key, value);
            }
        } else if (key == "plies") {
            argument_data.tournament_config.opening.plies = parseScalar<int>(value);
        } else if (key == "start") {
            argument_data.tournament_config.opening.start = parseScalar<int>(value);
            if (argument_data.tournament_config.opening.start < 1)
                throw fastchess_exception("Starting offset must be at least 1!");
        } else if (key == "policy") {
            if (value != "round") throw fastchess_exception("Unsupported opening book policy.");
        } else {
            OptionsParser::throwMissing("openings", key, value);
        }
    }
}

void parseSprt(const KeyValuePairs& params, ArgumentData& argument_data) {
    argument_data.tournament_config.sprt.enabled = true;

    if (argument_data.tournament_config.rounds == 0) {
        argument_data.tournament_config.rounds = 500000;
    }

    for (const auto& [key, value] : params) {
        if (key == "elo0") {
            argument_data.tournament_config.sprt.elo0 = parseScalar<double>(value);
        } else if (key == "elo1") {
            argument_data.tournament_config.sprt.elo1 = parseScalar<double>(value);
        } else if (key == "alpha") {
            argument_data.tournament_config.sprt.alpha = parseScalar<double>(value);
        } else if (key == "beta") {
            argument_data.tournament_config.sprt.beta = parseScalar<double>(value);
        } else if (key == "model") {
            argument_data.tournament_config.sprt.model = value;
        } else {
            OptionsParser::throwMissing("sprt", key, value);
        }
    }
}

void parseDraw(const KeyValuePairs& params, ArgumentData& argument_data) {
    argument_data.tournament_config.draw.enabled = true;

    for (const auto& [key, value] : params) {
        if (key == "movenumber") {
            argument_data.tournament_config.draw.move_number = parseScalar<int>(value);
        } else if (key == "movecount") {
            argument_data.tournament_config.draw.move_count = parseScalar<int>(value);
        } else if (key == "score") {
            const auto score = parseScalar<int>(value);
            if (score >= 0) {
                argument_data.tournament_config.draw.score = score;
            } else {
                throw fastchess_exception("Score cannot be negative.");
            }
        } else {
            OptionsParser::throwMissing("draw", key, value);
        }
    }
}

void parseResign(const KeyValuePairs& params, ArgumentData& argument_data) {
    argument_data.tournament_config.resign.enabled = true;

    for (const auto& [key, value] : params) {
        if (key == "movecount") {
            argument_data.tournament_config.resign.move_count = parseScalar<int>(value);
        } else if (key == "twosided" && is_bool(value)) {
            argument_data.tournament_config.resign.twosided = value == "true";
        } else if (key == "score") {
            const auto score = parseScalar<int>(value);
            if (score >= 0) {
                argument_data.tournament_config.resign.score = score;
            } else {
                throw fastchess_exception("Score cannot be negative.");
            }
        } else {
            OptionsParser::throwMissing("resign", key, value);
        }
    }
}

void parseMaxMoves(std::string_view value, ArgumentData& argument_data) {
    argument_data.tournament_config.maxmoves.move_count = parseScalar<int>(value);
    argument_data.tournament_config.maxmoves.enabled    = true;
}

void parseTbAdjudication(std::string_view value, ArgumentData& argument_data) {
    argument_data.tournament_config.tb_adjudication.syzygy_dirs = std::string(value);
    argument_data.tournament_config.tb_adjudication.enabled     = true;
}

void parseTbMaxPieces(std::string_view value, ArgumentData& argument_data) {
    argument_data.tournament_config.tb_adjudication.max_pieces = parseScalar<int>(value);
}

void parseTbIgnore50(ArgumentData& argument_data) {
    argument_data.tournament_config.tb_adjudication.ignore_50_move_rule = true;
}

void parseTbAdjudicate(std::string_view value, ArgumentData& argument_data) {
    std::string type(value);
    if (type == "WIN_LOSS") {
        argument_data.tournament_config.tb_adjudication.result_type = config::TbAdjudication::ResultType::WIN_LOSS;
    } else if (type == "DRAW") {
        argument_data.tournament_config.tb_adjudication.result_type = config::TbAdjudication::ResultType::DRAW;
    } else if (type == "BOTH") {
        argument_data.tournament_config.tb_adjudication.result_type = config::TbAdjudication::ResultType::BOTH;
    } else {
        throw fastchess_exception::format("Invalid tb adjudication type: {}", type);
    }
}

void parseAutoSaveInterval(std::string_view value, ArgumentData& argument_data) {
    argument_data.tournament_config.autosaveinterval = parseScalar<int>(value);
}

void parseLog(const KeyValuePairs& params, ArgumentData& argument_data) {
    for (const auto& [key, value] : params) {
        if (key == "file") {
            argument_data.tournament_config.log.file = value;
        } else if (key == "level") {
            Logger::Level level = Logger::Level::WARN;

            if (value == "trace") {
                level = Logger::Level::TRACE;
            } else if (value == "warn") {
                level = Logger::Level::WARN;
            } else if (value == "info") {
                level = Logger::Level::INFO;
            } else if (value == "err") {
                level = Logger::Level::ERR;
            } else if (value == "fatal") {
                level = Logger::Level::FATAL;
            } else {
                OptionsParser::throwMissing("log level", key, value);
            }

            argument_data.tournament_config.log.level = level;
        } else if (key == "append" && is_bool(value)) {
            argument_data.tournament_config.log.append_file = value == "true";
        } else if (key == "compress" && is_bool(value)) {
            argument_data.tournament_config.log.compress = value == "true";
        } else if (key == "realtime" && is_bool(value)) {
            argument_data.tournament_config.log.realtime = value == "true";
        } else if (key == "engine" && is_bool(value)) {
            argument_data.tournament_config.log.engine_coms = value == "true";
        } else {
            OptionsParser::throwMissing("log", key, value);
        }
    }
}

namespace json_config {
void loadJson(ArgumentData& argument_data, const std::string& filename) {
    Logger::print<Logger::Level::INFO>("Loading config file: {}", filename);

    std::ifstream f(filename);

    if (!f.is_open()) {
        throw fastchess_exception::format("File not found: {}", filename);
    }

    json jsonfile = json::parse(f);

    argument_data.old_configs           = argument_data.configs;
    argument_data.old_tournament_config = argument_data.tournament_config;

    argument_data.tournament_config = jsonfile.get<config::Tournament>();

    argument_data.configs.clear();

    for (const auto& engine : jsonfile["engines"]) {
        argument_data.configs.push_back(engine.get<EngineConfiguration>());
    }

    argument_data.stats = jsonfile["stats"].get<stats_map>();
}

void parseConfig(const KeyValuePairs& params, ArgumentData& argument_data) {
    bool drop_stats = false;

    for (const auto& [key, value] : params) {
        if (key == "file") {
            loadJson(argument_data, value);
        } else if (key == "outname") {
            argument_data.tournament_config.config_name = value;
        } else if (key == "discard" && value == "true") {
            Logger::print<Logger::Level::INFO>("Discarding config file");
            argument_data.tournament_config = argument_data.old_tournament_config;
            argument_data.configs           = argument_data.old_configs;
            argument_data.stats.clear();
        } else if (key == "stats") {
            drop_stats = value == "false";
        } else {
            OptionsParser::throwMissing("config", key, value);
        }
    }

    if (argument_data.configs.size() > 2) {
        std::cerr << "Warning: Stats will be dropped for more than 2 engines." << std::endl;
        argument_data.stats.clear();
    }

    if (drop_stats) {
        argument_data.stats.clear();
    }
}

}  // namespace json_config

void parseReport(const KeyValuePairs& params, ArgumentData& argument_data) {
    for (const auto& [key, value] : params) {
        if (key == "penta" && is_bool(value)) {
            argument_data.tournament_config.report_penta = value == "true";
        } else {
            OptionsParser::throwMissing("report", key, value);
        }
    }
}

void parseOutput(const KeyValuePairs& params, ArgumentData& argument_data) {
    for (const auto& [key, value] : params) {
        if (key == "format" && (value == "cutechess" || value == "fastchess")) {
            argument_data.tournament_config.output = OutputFactory::getType(value);
        } else {
            OptionsParser::throwMissing("output", key, value);
        }
    }
}

void parseConcurrency(std::string_view value, ArgumentData& argument_data) {
    argument_data.tournament_config.concurrency = parseScalar<int>(value);
}

void parseCrc(const KeyValuePairs& params, ArgumentData& argument_data) {
    for (const auto& [key, value] : params) {
        if (key == "pgn" && is_bool(value)) {
            argument_data.tournament_config.pgn.crc = value == "true";
        } else {
            OptionsParser::throwMissing("crc", key, value);
        }
    }
}

void parseForceConcurrency(ArgumentData& argument_data) { argument_data.tournament_config.force_concurrency = true; }

void parseEvent(const std::vector<std::string>& params, ArgumentData& argument_data) {
    argument_data.tournament_config.pgn.event_name = concat(params);
}

void parseSite(const std::vector<std::string>& params, ArgumentData& argument_data) {
    argument_data.tournament_config.pgn.site = concat(params);
}

void parseGames(std::string_view value, ArgumentData& argument_data) {
    argument_data.tournament_config.games = parseScalar<int>(value);
}

void parseRounds(std::string_view value, ArgumentData& argument_data) {
    argument_data.tournament_config.rounds = parseScalar<int>(value);
}

void parseWait(std::string_view value, ArgumentData& argument_data) {
    argument_data.tournament_config.wait = parseScalar<int>(value);
}

void parseNoswap(ArgumentData& argument_data) { argument_data.tournament_config.noswap = true; }

void parseReverse(ArgumentData& argument_data) { argument_data.tournament_config.reverse = true; }

void parseRatinginterval(std::string_view value, ArgumentData& argument_data) {
    argument_data.tournament_config.ratinginterval = parseScalar<int>(value);
}

void parseScoreinterval(std::string_view value, ArgumentData& argument_data) {
    argument_data.tournament_config.scoreinterval = parseScalar<int>(value);
}

void parseSRand(std::string_view value, ArgumentData& argument_data) {
    argument_data.tournament_config.seed = parseScalar<uint64_t>(value);
}

void parseSeeds(std::string_view value, ArgumentData& argument_data) {
    argument_data.tournament_config.gauntlet_seeds = parseScalar<int>(value);
}

void parseVersion(ArgumentData&) {
    std::cout << OptionsParser::Version << std::endl;
    std::exit(0);
}

void parseHelp(ArgumentData&) { OptionsParser::printHelp(); }

void parseRecover(ArgumentData& argument_data) { argument_data.tournament_config.recover = true; }

void parseRepeat(const std::vector<std::string>& params, ArgumentData& argument_data) {
    if (params.size() == 1 && is_number(params[0])) {
        argument_data.tournament_config.games = parseScalar<int>(params[0]);
    } else {
        argument_data.tournament_config.games = 2;
    }
}

void parseVariant(std::string_view value, ArgumentData& argument_data) {
    std::string val(value);
    if (val == "fischerandom") argument_data.tournament_config.variant = VariantType::FRC;
    if (val != "fischerandom" && val != "standard") throw fastchess_exception("Unknown variant.");
}

void parseTournament(std::string_view value, ArgumentData& argument_data) {
    std::string val(value);
    if (val == "gauntlet") {
        argument_data.tournament_config.type = TournamentType::GAUNTLET;
        return;
    }

    if (val == "roundrobin") {
        argument_data.tournament_config.type = TournamentType::ROUNDROBIN;
        return;
    }

    throw fastchess_exception("Unsupported tournament format. Only supports roundrobin and gauntlet.");
}

void parseQuick(const KeyValuePairs& params, ArgumentData& argument_data) {
    const auto initial_index = argument_data.configs.size();
    int engine_count         = 0;

    for (const auto& [key, value] : params) {
        if (key == "cmd") {
            argument_data.configs.emplace_back();
            argument_data.configs.back().cmd  = value;
            argument_data.configs.back().name = value;

            argument_data.configs.back().limit.tc.time      = 10 * 1000;
            argument_data.configs.back().limit.tc.increment = 100;

            argument_data.tournament_config.recover = true;
            ++engine_count;
        } else if (key == "book") {
            argument_data.tournament_config.opening.file  = value;
            argument_data.tournament_config.opening.order = OrderType::RANDOM;
            if (str_utils::endsWith(value, ".pgn"))
                argument_data.tournament_config.opening.format = FormatType::PGN;
            else if (str_utils::endsWith(value, ".epd"))
                argument_data.tournament_config.opening.format = FormatType::EPD;
            else
                throw fastchess_exception("Please include the .pgn or .epd file extension for the opening book.");
        } else {
            OptionsParser::throwMissing("quick", key, value);
        }
    }

    if (engine_count != 2) {
        throw std::runtime_error("Option \"-quick\" requires exactly two cmd entries.");
    }

    if (argument_data.tournament_config.opening.file.empty()) {
        throw std::runtime_error("Option \"-quick\" requires a book=FILE entry.");
    }

    auto& first  = argument_data.configs[initial_index];
    auto& second = argument_data.configs[initial_index + 1];

    if (first.name == second.name) {
        first.name += "1";
        second.name += "2";
    }

    argument_data.tournament_config.games  = 2;
    argument_data.tournament_config.rounds = 25000;

    argument_data.tournament_config.concurrency =
        std::max(static_cast<int>(1), static_cast<int>(std::thread::hardware_concurrency() - 2));

    argument_data.tournament_config.recover = true;

    argument_data.tournament_config.draw.enabled     = true;
    argument_data.tournament_config.draw.move_number = 30;
    argument_data.tournament_config.draw.move_count  = 8;
    argument_data.tournament_config.draw.score       = 8;

    argument_data.tournament_config.output = OutputType::CUTECHESS;
}

void parseAffinity(const std::vector<std::string>& params, ArgumentData& argument_data) {
    argument_data.tournament_config.affinity = true;
    if (params.size() > 0) {
        auto iss = std::istringstream(params[0]);
        if (parseIntList(iss, argument_data.tournament_config.affinity_cpus))
            throw fastchess_exception("Bad cpu list.");
    }
}

void parseMatePvs(ArgumentData& argument_data) { argument_data.tournament_config.check_mate_pvs = true; }

void parseLatency(ArgumentData& argument_data) { argument_data.tournament_config.show_latency = true; }

void parseDebug(ArgumentData&) {
    // throw error
    std::string error_message =
        "The 'debug' option does not exist in fastchess."
        " Use the 'log' option instead to write all engine input"
        " and output into a text file.";
    throw fastchess_exception(error_message);
}

void parseTestEnv(ArgumentData& argument_data) { argument_data.tournament_config.test_env = true; }

void parseStrict(ArgumentData& argument_data) { argument_data.tournament_config.strict = true; }

void parseStartupTime(std::string_view value, ArgumentData& argument_data) {
    argument_data.tournament_config.startup_time = std::chrono::milliseconds(parseScalar<uint64_t>(value));
}

void parseUciNewGameTime(std::string_view value, ArgumentData& argument_data) {
    argument_data.tournament_config.ucinewgame_time = std::chrono::milliseconds(parseScalar<uint64_t>(value));
}

void parsePingTime(std::string_view value, ArgumentData& argument_data) {
    argument_data.tournament_config.ping_time = std::chrono::milliseconds(parseScalar<uint64_t>(value));
}

OptionsParser::OptionsParser(const cli::Args& args) {
    LOG_TRACE("Reading options...");

    if (args.argc() == 1) {
        printHelp();
    }

    registerOptions();
    parse(args);

    // apply the variant type to all configs
    for (auto& config : argument_data_.configs) {
        config.variant = argument_data_.tournament_config.variant;
    }

    cli::sanitize(argument_data_.tournament_config);

    cli::sanitize(argument_data_.configs);
}

void OptionsParser::registerOptions() {
    addOption<ParamStyle::KeyValue>("engine", engine::parseEngine);
    addOption<ParamStyle::KeyValue, Dispatch::Deferred>("each", parseEach);
    addOption<ParamStyle::KeyValueOptional>("pgnout", parsePgnOut);
    addOption<ParamStyle::KeyValueOptional>("epdout", parseEpdOut);
    addOption<ParamStyle::KeyValue>("openings", parseOpening);
    addOption<ParamStyle::KeyValue>("sprt", parseSprt);
    addOption<ParamStyle::KeyValue>("draw", parseDraw);
    addOption<ParamStyle::KeyValue>("resign", parseResign);
    addOption<ParamStyle::Single>("maxmoves", parseMaxMoves);
    addOption<ParamStyle::Single>("tb", parseTbAdjudication);
    addOption<ParamStyle::Single>("tbpieces", parseTbMaxPieces);
    addOption<ParamStyle::None>("tbignore50", parseTbIgnore50);
    addOption<ParamStyle::Single>("tbadjudicate", parseTbAdjudicate);
    addOption<ParamStyle::Single>("autosaveinterval", parseAutoSaveInterval);
    addOption<ParamStyle::KeyValue>("log", parseLog);
    addOption<ParamStyle::KeyValue>("config", json_config::parseConfig);
    addOption<ParamStyle::KeyValue>("report", parseReport);
    addOption<ParamStyle::KeyValue>("output", parseOutput);
    addOption<ParamStyle::Single>("concurrency", parseConcurrency);
    addOption<ParamStyle::KeyValue>("crc32", parseCrc);
    addOption<ParamStyle::None>("force-concurrency", parseForceConcurrency);
    addOption("event", parseEvent);
    addOption("site", parseSite);
    addOption<ParamStyle::Single>("games", parseGames);
    addOption<ParamStyle::Single>("rounds", parseRounds);
    addOption<ParamStyle::Single>("wait", parseWait);
    addOption<ParamStyle::None>("noswap", parseNoswap);
    addOption<ParamStyle::None>("reverse", parseReverse);
    addOption<ParamStyle::Single>("ratinginterval", parseRatinginterval);
    addOption<ParamStyle::Single>("scoreinterval", parseScoreinterval);
    addOption<ParamStyle::Single>("srand", parseSRand);
    addOption<ParamStyle::Single>("seeds", parseSeeds);
    addOption<ParamStyle::None>("version", parseVersion);
    addOption<ParamStyle::None>("-version", parseVersion);
    addOption<ParamStyle::None>("v", parseVersion);
    addOption<ParamStyle::None>("-v", parseVersion);
    addOption<ParamStyle::None>("help", parseHelp);
    addOption<ParamStyle::None>("-help", parseHelp);
    addOption<ParamStyle::None>("recover", parseRecover);
    addOption("repeat", parseRepeat);
    addOption<ParamStyle::Single>("variant", parseVariant);
    addOption<ParamStyle::Single>("tournament", parseTournament);
    addOption<ParamStyle::KeyValue>("quick", parseQuick);
    addOption("use-affinity", parseAffinity);
    addOption<ParamStyle::None>("check-mate-pvs", parseMatePvs);
    addOption<ParamStyle::None>("show-latency", parseLatency);
    addOption<ParamStyle::None>("debug", parseDebug);
    addOption<ParamStyle::None>("testEnv", parseTestEnv);
    addOption<ParamStyle::None>("strict", parseStrict);

    addOption<ParamStyle::Single>("startup-ms", parseStartupTime);
    addOption<ParamStyle::Single>("ucinewgame-ms", parseUciNewGameTime);
    addOption<ParamStyle::Single>("ping-ms", parsePingTime);
}

}  // namespace fastchess::cli
