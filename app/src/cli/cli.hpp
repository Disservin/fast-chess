#pragma once

#include <algorithm>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <cli/man.hpp>
#include <config/config.hpp>
#include <matchmaking/scoreboard.hpp>
#include <types/engine_config.hpp>
#include <types/tournament.hpp>

#define FMT_HEADER_ONLY
#include "../../third_party/fmt/include/fmt/core.h"

namespace fastchess::cli {

constexpr auto version = "alpha 0.9.0 ";

// Holds the data of the OptionParser
struct ArgumentData {
    // Holds all the relevant settings for the handling of the games
    config::Tournament tournament_config;
    /*previous olded values before config*/
    config::Tournament old_tournament_config;

    stats_map stats;

    // Holds all the engines with their options
    std::vector<EngineConfiguration> configs;

    std::vector<EngineConfiguration> old_configs;
};

class OptionsParser {
    using parseFunc = std::function<void((const std::vector<std::string> &, ArgumentData &))>;

   public:
    OptionsParser(int argc, char const *argv[]);

    static void throwMissing(std::string_view name, std::string_view key, std::string_view value) {
        throw std::runtime_error("Unrecognized " + std::string(name) + " option \"" + std::string(key) +
                                 "\" with value \"" + std::string(value) + "\".");
    }

    static void printVersion() {
        const static std::unordered_map<std::string, std::string> months(  //
            {{"Jan", "01"},
             {"Feb", "02"},
             {"Mar", "03"},
             {"Apr", "04"},
             {"May", "05"},
             {"Jun", "06"},
             {"Jul", "07"},
             {"Aug", "08"},
             {"Sep", "09"},
             {"Oct", "10"},
             {"Nov", "11"},
             {"Dec", "12"}});

        std::string month, day, year;
        std::stringstream ss, date(__DATE__);  // {month} {date} {year}

        ss << "fastchess " << version;

#ifdef COMPILE_MSG
        ss << COMPILE_MSG << " ";
#endif

#ifndef RELEASE
#    ifdef GIT_DATE
        ss << GIT_DATE;
#    else
        date >> month >> day >> year;
        if (day.length() == 1) day = "0" + day;
        ss << year.substr(2) << months.at(month) << day;
#    endif

#    ifdef GIT_SHA
        ss << "-" << GIT_SHA;
#    endif
#endif

#ifdef USE_CUTE
        ss << " (compiled with cutechess output)";
#endif

        std::cout << ss.str() << std::endl;
        std::exit(0);
    }

    static void printHelp() {
        std::cout << std::string(man::man, man::man_len) << std::flush;

        std::exit(0);
    }

    [[nodiscard]] std::vector<EngineConfiguration> getEngineConfigs() const { return argument_data_.configs; }

    [[nodiscard]] config::Tournament getTournamentConfig() const { return argument_data_.tournament_config; }

    [[nodiscard]] stats_map getResults() const { return argument_data_.stats; }

   private:
    // Adds an option to the parser
    void addOption(const std::string &optionName, parseFunc func) {
        options_.insert(std::make_pair("-" + optionName, func));
    }

    // Parses the command line arguments and calls the corresponding option. Parse will
    // increment i if need be.
    void parse(int argc, char const *argv[]) {
        for (int i = 1; i < argc; i++) {
            const std::string arg = argv[i];
            if (options_.count(arg) == 0) {
                throw std::runtime_error("Unrecognized option: " + arg + " parsing failed.");
            }

            try {
                std::vector<std::string> params;

                while (i + 1 < argc && argv[i + 1][0] != '-') {
                    params.push_back(argv[++i]);
                }

                options_.at(arg)(params, argument_data_);

            } catch (const std::exception &e) {
                auto err =
                    fmt::format("Error while reading option \"{}\" with value \"{}\"", arg, std::string(argv[i]));
                auto msg = fmt::format("Reason: {}", e.what());

                throw std::runtime_error(err + "\n" + msg);
            }
        }
    }

    ArgumentData argument_data_;

    std::map<std::string, parseFunc> options_;
};

}  // namespace fastchess::cli
