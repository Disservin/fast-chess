#include <cli/cli.hpp>

#include <random>

#include <matchmaking/output/output_factory.hpp>
#include <matchmaking/scoreboard.hpp>
#include <types/engine_config.hpp>
#include <types/tournament.hpp>
#include <util/file_system.hpp>
#include <util/logger/logger.hpp>
#include <util/rand.hpp>

namespace {
// Parse -name key=value key=value
void parseDashOptions(const std::vector<std::string> &params,
                      const std::function<void(std::string, std::string)> &func) {
    for (const auto &param : params) {
        size_t pos = param.find('=');
        if (pos != std::string::npos) {
            std::string key   = param.substr(0, pos);
            std::string value = param.substr(pos + 1);
            func(key, value);
        } else {
            std::cerr << "Invalid parameter: " << param << std::endl;
        }
    }
}

// Parse a standalone value after a dash command. i.e -concurrency 10
template <typename T>
void parseValue(const std::vector<std::string> &params, T &value) {
    std::string str;

    for (const auto &param : params) {
        str += param;
    }

    // convert the string to the desired type
    if constexpr (std::is_same_v<T, int>)
        value = std::stoi(str);
    else if constexpr (std::is_same_v<T, uint32_t>)
        value = std::stoul(str);
    else if constexpr (std::is_same_v<T, uint64_t>)
        value = std::stoull(str);
    else if constexpr (std::is_same_v<T, float>)
        value = std::stof(str);
    else if constexpr (std::is_same_v<T, double>)
        value = std::stod(str);
    else if constexpr (std::is_same_v<T, bool>)
        value = str == "true";
    else
        value = str;
}

std::string concat(const std::vector<std::string> &params) {
    std::string str;

    for (const auto &param : params) {
        str += param;
    }

    return str;
}

bool is_number(const std::string &s) {
    static const auto is_digit = [](unsigned char c) { return !std::isdigit(c); };
    return !s.empty() && std::find_if(s.begin(), s.end(), is_digit) == s.end();
}

bool is_bool(const std::string &s) { return s == "true" || s == "false"; }

bool containsEqualSign(const std::vector<std::string> &params) {
    for (const auto &param : params) {
        if (param.find('=') != std::string::npos) {
            return true;
        }
    }
    return false;
}
}  // namespace

namespace fastchess::cli {
using json = nlohmann::json;

namespace engine {
TimeControl::Limits parseTc(const std::string &tcString) {
    if (str_utils::contains(tcString, "hg")) throw std::runtime_error("Hourglass time control not supported.");
    if (tcString == "infinite" || tcString == "inf") return {};

    TimeControl::Limits tc;

    std::string remainingStringVector = tcString;
    const bool has_moves              = str_utils::contains(tcString, "/");
    const bool has_inc                = str_utils::contains(tcString, "+");
    const bool has_minutes            = str_utils::contains(tcString, ":");

    if (has_moves) {
        const auto moves = str_utils::splitString(tcString, '/');
        if (moves[0] == "inf" || moves[0] == "infinite") {
            tc.moves = 0;
        } else {
            tc.moves = std::stoi(moves[0]);
        }
        remainingStringVector = moves[1];
    }

    if (has_inc) {
        const auto inc        = str_utils::splitString(remainingStringVector, '+');
        tc.increment          = static_cast<uint64_t>(std::stod(inc[1]) * 1000);
        remainingStringVector = inc[0];
    }

    if (has_minutes) {
        const auto clock_vector = str_utils::splitString(remainingStringVector, ':');
        tc.time                 = static_cast<int64_t>(std::stod(clock_vector[0]) * 1000 * 60) +
                  static_cast<int64_t>(std::stod(clock_vector[1]) * 1000);
    } else {
        tc.time = static_cast<int64_t>(std::stod(remainingStringVector) * 1000);
    }

    return tc;
}

bool isEngineSettableOption(std::string_view stringFormat) { return str_utils::startsWith(stringFormat, "option."); }

void parseEngineKeyValues(EngineConfiguration &engineConfig, const std::string &key, const std::string &value) {
    if (key == "cmd") {
        engineConfig.cmd = value;
    } else if (key == "name")
        engineConfig.name = value;
    else if (key == "tc")
        engineConfig.limit.tc = parseTc(value);
    else if (key == "st")
        engineConfig.limit.tc.fixed_time = static_cast<int64_t>(std::stod(value) * 1000);
    else if (key == "timemargin") {
        engineConfig.limit.tc.timemargin = std::stoi(value);
        if (engineConfig.limit.tc.timemargin < 0) {
            throw std::runtime_error("The value for timemargin cannot be a negative number.");
        }
    } else if (key == "nodes")
        engineConfig.limit.nodes = std::stoll(value);
    else if (key == "plies" || key == "depth")
        engineConfig.limit.plies = std::stoll(value);
    else if (key == "dir")
        engineConfig.dir = value;
    else if (key == "args")
        engineConfig.args = value;
    else if (isEngineSettableOption(key)) {
        // Strip option.Name of the option. Part
        const std::size_t pos         = key.find('.');
        const std::string strippedKey = key.substr(pos + 1);
        engineConfig.options.emplace_back(strippedKey, value);
    } else if (key == "proto") {
        if (value != "uci") {
            throw std::runtime_error("Unsupported protocol.");
        }
    } else
        OptionsParser::throwMissing("engine", key, value);
}

void parseEngine(const std::vector<std::string> &params, ArgumentData &argument_data) {
    argument_data.configs.emplace_back();

    parseDashOptions(params, [&](const std::string &key, const std::string &value) {
        engine::parseEngineKeyValues(argument_data.configs.back(), key, value);
    });
}

}  // namespace engine

void parseEach(const std::vector<std::string> &params, ArgumentData &argument_data) {
    parseDashOptions(params, [&](const std::string &key, const std::string &value) {
        for (auto &config : argument_data.configs) {
            engine::parseEngineKeyValues(config, key, value);
        }
    });
}

void parsePgnOut(const std::vector<std::string> &params, ArgumentData &argument_data) {
    if (containsEqualSign(params)) {
        parseDashOptions(params, [&](const std::string &key, const std::string &value) {
            if (key == "file") {
                argument_data.tournament_config.pgn.file = value;
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
            } else {
                OptionsParser::throwMissing("pgnout", key, value);
            }
        });
    } else {
        // try to read as cutechess pgnout
        argument_data.tournament_config.pgn.file = params[0];
        argument_data.tournament_config.pgn.min  = std::find(params.begin(), params.end(), "min") != params.end();
    }
    if (argument_data.tournament_config.pgn.file.empty())
        throw std::runtime_error("Please specify filename for pgn output.");
}

void parseEpdOut(const std::vector<std::string> &params, ArgumentData &argument_data) {
    if (containsEqualSign(params)) {
        parseDashOptions(params, [&](const std::string &key, const std::string &value) {
            if (key == "file") {
                argument_data.tournament_config.epd.file = value;
            } else {
                OptionsParser::throwMissing("epdout", key, value);
            }
        });
    } else {
        // try to read as cutechess epdout
        parseValue(params, argument_data.tournament_config.epd.file);
    }
    if (argument_data.tournament_config.epd.file.empty())
        throw std::runtime_error("Please specify filename for epd output.");
}

void parseOpening(const std::vector<std::string> &params, ArgumentData &argument_data) {
    parseDashOptions(params, [&](const std::string &key, const std::string &value) {
        if (key == "file") {
            argument_data.tournament_config.opening.file = value;
            if (str_utils::endsWith(value, ".epd")) {
                argument_data.tournament_config.opening.format = FormatType::EPD;
            } else if (str_utils::endsWith(value, ".pgn")) {
                argument_data.tournament_config.opening.format = FormatType::PGN;
            }

#ifndef NO_STD_FILESYSTEM
            if (!std::filesystem::exists(value)) {
                throw std::runtime_error("Opening file does not exist: " + value);
            }
#endif
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
            argument_data.tournament_config.opening.plies = std::stoi(value);
        } else if (key == "start") {
            argument_data.tournament_config.opening.start = std::stoi(value);
            if (argument_data.tournament_config.opening.start < 1)
                throw std::runtime_error("Starting offset must be at least 1!");
        } else if (key == "policy") {
            if (value != "round") throw std::runtime_error("Unsupported opening book policy.");
        } else {
            OptionsParser::throwMissing("openings", key, value);
        }
    });
}

void parseSprt(const std::vector<std::string> &params, ArgumentData &argument_data) {
    parseDashOptions(params, [&](const std::string &key, const std::string &value) {
        argument_data.tournament_config.sprt.enabled = true;

        if (argument_data.tournament_config.rounds == 0) {
            argument_data.tournament_config.rounds = 500000;
        }

        if (key == "elo0") {
            argument_data.tournament_config.sprt.elo0 = std::stod(value);
        } else if (key == "elo1") {
            argument_data.tournament_config.sprt.elo1 = std::stod(value);
        } else if (key == "alpha") {
            argument_data.tournament_config.sprt.alpha = std::stod(value);
        } else if (key == "beta") {
            argument_data.tournament_config.sprt.beta = std::stod(value);
        } else if (key == "model") {
            argument_data.tournament_config.sprt.model = value;
        } else {
            OptionsParser::throwMissing("sprt", key, value);
        }
    });
}

void parseDraw(const std::vector<std::string> &params, ArgumentData &argument_data) {
    parseDashOptions(params, [&](const std::string &key, const std::string &value) {
        argument_data.tournament_config.draw.enabled = true;

        if (key == "movenumber") {
            argument_data.tournament_config.draw.move_number = std::stoi(value);
        } else if (key == "movecount") {
            argument_data.tournament_config.draw.move_count = std::stoi(value);
        } else if (key == "score") {
            if (std::stoi(value) >= 0) {
                argument_data.tournament_config.draw.score = std::stoi(value);
            } else {
                throw std::runtime_error("Score cannot be negative.");
            }
        } else {
            OptionsParser::throwMissing("draw", key, value);
        }
    });
}

void parseResign(const std::vector<std::string> &params, ArgumentData &argument_data) {
    parseDashOptions(params, [&](const std::string &key, const std::string &value) {
        argument_data.tournament_config.resign.enabled = true;

        if (key == "movecount") {
            argument_data.tournament_config.resign.move_count = std::stoi(value);
        } else if (key == "twosided" && is_bool(value)) {
            argument_data.tournament_config.resign.twosided = value == "true";
        } else if (key == "score") {
            if (std::stoi(value) >= 0) {
                argument_data.tournament_config.resign.score = std::stoi(value);
            } else {
                throw std::runtime_error("Score cannot be negative.");
            }
        } else {
            OptionsParser::throwMissing("resign", key, value);
        }
    });
}

void parseMaxMoves(const std::vector<std::string> &params, ArgumentData &argument_data) {
    parseValue(params, argument_data.tournament_config.maxmoves.move_count);
    argument_data.tournament_config.maxmoves.enabled = true;
}

void parseAutoSaveInterval(const std::vector<std::string> &params, ArgumentData &argument_data) {
    parseValue(params, argument_data.tournament_config.autosaveinterval);
}

void parseLog(const std::vector<std::string> &params, ArgumentData &argument_data) {
    std::string filename;
    parseDashOptions(params, [&](const std::string &key, const std::string &value) {
        if (key == "file") {
            filename                                 = value;
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
        } else if (key == "compress" && is_bool(value)) {
            argument_data.tournament_config.log.compress = value == "true";
        } else if (key == "realtime" && is_bool(value)) {
            argument_data.tournament_config.log.realtime = value == "true";
        } else {
            OptionsParser::throwMissing("log", key, value);
        }
    });

    if (filename.empty()) throw std::runtime_error("Please specify filename for log output.");
}

namespace json_config {
void loadJson(ArgumentData &argument_data, const std::string &filename) {
    Logger::info("Loading config file: {}", filename);
    std::ifstream f(filename);
    json jsonfile = json::parse(f);

    argument_data.old_configs           = argument_data.configs;
    argument_data.old_tournament_config = argument_data.tournament_config;

    argument_data.tournament_config = jsonfile.get<config::Tournament>();

    argument_data.configs.clear();

    for (const auto &engine : jsonfile["engines"]) {
        argument_data.configs.push_back(engine.get<EngineConfiguration>());
    }

    argument_data.stats = jsonfile["stats"].get<stats_map>();
}

void parseConfig(const std::vector<std::string> &params, ArgumentData &argument_data) {
    bool drop_stats = false;

    parseDashOptions(params, [&](const std::string &key, const std::string &value) {
        if (key == "file") {
            loadJson(argument_data, value);
        } else if (key == "outname") {
            argument_data.tournament_config.config_name = value;
        } else if (key == "discard" && value == "true") {
            Logger::info("Discarding config file");
            argument_data.tournament_config = argument_data.old_tournament_config;
            argument_data.configs           = argument_data.old_configs;
            argument_data.stats.clear();
        } else if (key == "stats") {
            drop_stats = value == "false";
        }
    });

    if (drop_stats) {
        argument_data.stats.clear();
    }
}

}  // namespace json_config

void parseReport(const std::vector<std::string> &params, ArgumentData &argument_data) {
    parseDashOptions(params, [&](const std::string &key, const std::string &value) {
        if (key == "penta" && is_bool(value)) {
            argument_data.tournament_config.report_penta = value == "true";
        } else {
            OptionsParser::throwMissing("report", key, value);
        }
    });
}

void parseOutput(const std::vector<std::string> &params, ArgumentData &argument_data) {
    parseDashOptions(params, [&](const std::string &key, const std::string &value) {
        if (key == "format" && (value == "cutechess" || value == "fastchess")) {
            argument_data.tournament_config.output = OutputFactory::getType(value);
        } else {
            OptionsParser::throwMissing("output", key, value);
        }
    });
}

void parseConcurrency(const std::vector<std::string> &params, ArgumentData &argument_data) {
    parseValue(params, argument_data.tournament_config.concurrency);
}

void parseEvent(const std::vector<std::string> &params, ArgumentData &argument_data) {
    argument_data.tournament_config.pgn.event_name = concat(params);
}

void parseSite(const std::vector<std::string> &params, ArgumentData &argument_data) {
    parseValue(params, argument_data.tournament_config.pgn.site);
}

void parseGames(const std::vector<std::string> &params, ArgumentData &argument_data) {
    parseValue(params, argument_data.tournament_config.games);
}

void parseRounds(const std::vector<std::string> &params, ArgumentData &argument_data) {
    parseValue(params, argument_data.tournament_config.rounds);
}

void parseWait(const std::vector<std::string> &params, ArgumentData &argument_data) {
    parseValue(params, argument_data.tournament_config.wait);
}

void parseNoswap(const std::vector<std::string> &, ArgumentData &argument_data) {
    argument_data.tournament_config.noswap = true;
}

void parseRatinginterval(const std::vector<std::string> &params, ArgumentData &argument_data) {
    parseValue(params, argument_data.tournament_config.ratinginterval);
}

void parseScoreinterval(const std::vector<std::string> &params, ArgumentData &argument_data) {
    parseValue(params, argument_data.tournament_config.scoreinterval);
}

void parseSRand(const std::vector<std::string> &params, ArgumentData &argument_data) {
    parseValue(params, argument_data.tournament_config.seed);
}

void parseVersion(const std::vector<std::string> &, ArgumentData &) { OptionsParser::printVersion(); }

void parseHelp(const std::vector<std::string> &, ArgumentData &) { OptionsParser::printHelp(); }

void parseRecover(const std::vector<std::string> &, ArgumentData &argument_data) {
    argument_data.tournament_config.recover = true;
}

void parseRepeat(const std::vector<std::string> &params, ArgumentData &argument_data) {
    if (params.size() == 1 && is_number(params[0])) {
        parseValue(params, argument_data.tournament_config.games);
    } else {
        argument_data.tournament_config.games = 2;
    }
}

void parseVariant(const std::vector<std::string> &params, ArgumentData &argument_data) {
    std::string val;

    parseValue(params, val);

    if (val == "fischerandom") argument_data.tournament_config.variant = VariantType::FRC;
    if (val != "fischerandom" && val != "standard") throw std::runtime_error("Unknown variant.");
}

void parseTournament(const std::vector<std::string> &params, ArgumentData &) {
    std::string val;
    // Do nothing
    parseValue(params, val);
}

void parseQuick(const std::vector<std::string> &params, ArgumentData &argument_data) {
    parseDashOptions(params, [&](const std::string &key, const std::string &value) {
        if (key == "cmd") {
            argument_data.configs.emplace_back();
            argument_data.configs.back().cmd  = value;
            argument_data.configs.back().name = value;

            argument_data.configs.back().limit.tc.time      = 10 * 1000;
            argument_data.configs.back().limit.tc.increment = 100;

            argument_data.configs.back().recover = true;
        } else if (key == "book") {
            argument_data.tournament_config.opening.file  = value;
            argument_data.tournament_config.opening.order = OrderType::RANDOM;
            if (str_utils::endsWith(value, ".pgn"))
                argument_data.tournament_config.opening.format = FormatType::PGN;
            else if (str_utils::endsWith(value, ".epd"))
                argument_data.tournament_config.opening.format = FormatType::EPD;
            else
                throw std::runtime_error("Please include the .png or .epd file extension for the opening book.");
        } else {
            OptionsParser::throwMissing("quick", key, value);
        }
    });

    if (argument_data.configs[0].name == argument_data.configs[1].name) {
        argument_data.configs[0].name += "1";
        argument_data.configs[1].name += "2";
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

void parseAffinity(const std::vector<std::string> &, ArgumentData &argument_data) {
    argument_data.tournament_config.affinity = true;
}

void parseDebug(const std::vector<std::string> &, ArgumentData &) {
    // throw error
    std::string error_message =
        "The 'debug' option does not exist in fastchess."
        " Use the 'log' option instead to write all engine input"
        " and output into a text file.";
    throw std::runtime_error(error_message);
}

OptionsParser::OptionsParser(int argc, char const *argv[]) {
    Logger::trace("Reading options...");

    if (argc == 1) {
        printHelp();
    }

    addOption("engine", engine::parseEngine);
    addOption("each", parseEach);
    addOption("pgnout", parsePgnOut);
    addOption("epdout", parseEpdOut);
    addOption("openings", parseOpening);
    addOption("sprt", parseSprt);
    addOption("draw", parseDraw);
    addOption("resign", parseResign);
    addOption("maxmoves", parseMaxMoves);
    addOption("autosaveinterval", parseAutoSaveInterval);
    addOption("log", parseLog);
    addOption("config", json_config::parseConfig);
    addOption("report", parseReport);
    addOption("output", parseOutput);
    addOption("concurrency", parseConcurrency);
    addOption("event", parseEvent);
    addOption("site", parseSite);
    addOption("games", parseGames);
    addOption("rounds", parseRounds);
    addOption("wait", parseWait);
    addOption("noswap", parseNoswap);
    addOption("ratinginterval", parseRatinginterval);
    addOption("scoreinterval", parseScoreinterval);
    addOption("srand", parseSRand);
    addOption("version", parseVersion);
    addOption("-version", parseVersion);
    addOption("v", parseVersion);
    addOption("-v", parseVersion);
    addOption("help", parseHelp);
    addOption("-help", parseHelp);
    addOption("recover", parseRecover);
    addOption("repeat", parseRepeat);
    addOption("variant", parseVariant);
    addOption("tournament", parseTournament);
    addOption("quick", parseQuick);
    addOption("use-affinity", parseAffinity);
    addOption("debug", parseDebug);

    parse(argc, argv);

    // apply the variant type to all configs
    for (auto &config : argument_data_.configs) {
        config.variant = argument_data_.tournament_config.variant;
    }
}

}  // namespace fastchess::cli
