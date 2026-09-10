#pragma once

#include <functional>
#include <iostream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <core/printing/printing.h>
#include <cli/cli_args.hpp>
#include <cli/man.hpp>
#include <core/config/config.hpp>
#include <matchmaking/scoreboard.hpp>
#include <types/engine_config.hpp>
#include <types/exception.hpp>
#include <types/tournament.hpp>

#include <fmt/include/fmt/core.h>

namespace fastchess {
extern const char* version;
}

namespace fastchess::cli {

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
    using parseFunc = std::function<void(const std::vector<std::string>&, ArgumentData&)>;

    enum class ParamStyle { Free, None, Single, KeyValue, KeyValueOptional };

    struct OptionEntry {
        parseFunc func;
        bool deferred = false;
    };

    static std::string getVersion() {
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
        std::stringstream date(__DATE__);  // {month} {date} {year}
        std::string result = fmt::format("fastchess {}", version);

#ifdef COMPILE_MSG
        fmt::format_to(std::back_inserter(result), "{} ", COMPILE_MSG);
#endif

#ifndef RELEASE
#    ifdef GIT_DATE
        fmt::format_to(std::back_inserter(result), "{}", GIT_DATE);
#    else
        date >> month >> day >> year;
        fmt::format_to(std::back_inserter(result), "{}{}{:0>2}", year.substr(2), months.at(month), day);
#    endif

#    ifdef GIT_SHA
        fmt::format_to(std::back_inserter(result), "-{}", GIT_SHA);
#    endif
#endif

#ifdef USE_CUTE
        result += " (compiled with cutechess output)";
#endif

#ifndef NDEBUG
        result += " (assertions)";
#endif

        return result;
    }

   public:
    OptionsParser(const cli::Args& args);

    static void throwMissing(std::string_view name, std::string_view key, std::string_view value) {
        throw fastchess_exception::format("Unrecognized {} option \"{}\" with value \"{}\".", name, key, value);
    }

    static int levenshtein(std::string_view a, std::string_view b) {
        // Always use the shorter string for the DP rows.
        if (a.size() > b.size()) std::swap(a, b);

        std::vector<int> prev(a.size() + 1);
        std::vector<int> curr(a.size() + 1);

        std::iota(prev.begin(), prev.end(), 0);

        for (std::size_t j = 1; j <= b.size(); ++j) {
            curr[0] = static_cast<int>(j);

            for (std::size_t i = 1; i <= a.size(); ++i) {
                const int substitution = prev[i - 1] + (a[i - 1] != b[j - 1]);
                const int insertion    = curr[i - 1] + 1;
                const int deletion     = prev[i] + 1;

                curr[i] = std::min({substitution, insertion, deletion});
            }

            std::swap(prev, curr);
        }

        return prev.back();
    }

    std::optional<std::string_view> findSuggestion(std::string_view arg) const {
        std::optional<std::string_view> best;
        int bestDistance = std::numeric_limits<int>::max();

        for (const auto& [option, value] : options_) {
            const int distance = levenshtein(arg, option);

            if (distance < bestDistance) {
                bestDistance = distance;
                best         = option;
            }
        }

        if (bestDistance <= 2) {
            return best;
        }

        return std::nullopt;
    }

    static void printHelp() {
        setTerminalOutput();

        for (const auto c : man::man) {
            std::cout << c;
        }

        std::flush(std::cout);

        std::exit(0);
    }

    [[nodiscard]] std::vector<EngineConfiguration> getEngineConfigs() const { return argument_data_.configs; }

    [[nodiscard]] config::Tournament getTournamentConfig() const { return argument_data_.tournament_config; }

    [[nodiscard]] stats_map getResults() const { return argument_data_.stats; }

    const inline static auto Version = getVersion();

   private:
    enum class Dispatch { Immediate, Deferred };

    template <typename Handler>
    void addOption(const std::string& optionName, Handler&& handler) {
        addOption<ParamStyle::Free, Dispatch::Immediate>(optionName, std::forward<Handler>(handler));
    }

    template <ParamStyle Style, typename Handler>
    void addOption(const std::string& optionName, Handler&& handler) {
        addOption<Style, Dispatch::Immediate>(optionName, std::forward<Handler>(handler));
    }

    template <ParamStyle Style, Dispatch Mode, typename Handler>
    void addOption(const std::string& optionName, Handler&& handler) {
        std::string flag = optionName;
        flag.insert(flag.begin(), '-');

        options_.insert(std::make_pair(
            flag, OptionEntry{wrapHandler<Style>(flag, std::forward<Handler>(handler)), Mode == Dispatch::Deferred}));
    }

    void registerOptions();

    // Parses the command line arguments and calls the corresponding option. Parse will
    // increment i if need be.
    void parse(const cli::Args& args) {
        std::unordered_map<std::string, std::vector<std::string>> deferred;

        const auto joinParams = [](const std::vector<std::string>& params) -> std::string {
            if (params.empty()) return std::string("<none>");
            std::string result;
            for (std::size_t i = 0; i < params.size(); ++i) {
                fmt::format_to(std::back_inserter(result), "{}{}", i == 0 ? "" : " ", params[i]);
            }
            return result;
        };

        for (int i = 1; i < args.argc(); i++) {
            const std::string arg = args[i];
            if (options_.count(arg) == 0) {
                auto f = fmt::format("Unrecognized option: {} parsing failed.", arg);

                if (auto suggestion = findSuggestion(arg))
                    throw fastchess_exception::format("{}: Did you mean \"{}\"?", f, *suggestion);

                throw fastchess_exception(f);
            }

            try {
                std::vector<std::string> params;

                while (i + 1 < args.argc()) {
                    const std::string& val = args[i + 1];
                    if (val[0] == '-') {
                        // Allow negative numbers as values
                        if (val.size() > 1 && val[1] >= '0' && val[1] <= '9') {
                            // It is a negative number, consume it
                        } else {
                            // It is likely a new option, stop consuming
                            break;
                        }
                    }
                    params.push_back(args[++i]);
                }

                const auto& entry = options_.at(arg);
                if (entry.deferred) {
                    auto& slot = deferred[arg];
                    slot.insert(slot.end(), params.begin(), params.end());
                    continue;
                }

                entry.func(params, argument_data_);

            } catch (const std::exception& e) {
                auto err =
                    fmt::format("Error while reading option \"{}\" with value \"{}\"", arg, std::string(args[i]));
                auto msg = fmt::format("Reason: {}", e.what());

                throw fastchess_exception::format("{}\n{}", err, msg);
            }
        }

        for (auto& [flag, params] : deferred) {
            try {
                const auto& entry = options_.at(flag);
                entry.func(params, argument_data_);
            } catch (const std::exception& e) {
                auto err = fmt::format("Error while reading option \"{}\" with value \"{}\"", flag, joinParams(params));
                auto msg = fmt::format("Reason: {}", e.what());

                throw fastchess_exception::format("{}\n{}", err, msg);
            }
        }
    }

    template <ParamStyle Style, typename Handler>
    parseFunc wrapHandler(const std::string& flag, Handler&& handler) {
        if constexpr (Style == ParamStyle::None) {
            static_assert(std::is_invocable_r_v<void, Handler, ArgumentData&>, "Handler must accept (ArgumentData&)");
            std::function<void(ArgumentData&)> fn = std::forward<Handler>(handler);
            return [flag, fn](const std::vector<std::string>& params, ArgumentData& data) {
                if (!params.empty()) {
                    throw fastchess_exception::format("Option \"{}\" does not accept parameters.", flag);
                }
                fn(data);
            };
        } else if constexpr (Style == ParamStyle::Single) {
            static_assert(std::is_invocable_r_v<void, Handler, std::string_view, ArgumentData&>,
                          "Handler must accept (std::string_view, ArgumentData&)");
            std::function<void(std::string_view, ArgumentData&)> fn = std::forward<Handler>(handler);
            return [flag, fn](const std::vector<std::string>& params, ArgumentData& data) {
                if (params.size() != 1) {
                    throw fastchess_exception::format("Option \"{}\" expects exactly one value.", flag);
                }
                fn(params.front(), data);
            };
        } else if constexpr (Style == ParamStyle::KeyValue) {
            static_assert(std::is_invocable_r_v<void, Handler, const std::vector<std::pair<std::string, std::string>>&,
                                                ArgumentData&>,
                          "Handler must accept (key/value list, ArgumentData&)");
            std::function<void(const std::vector<std::pair<std::string, std::string>>&, ArgumentData&)> fn =
                std::forward<Handler>(handler);
            return [flag, fn](const std::vector<std::string>& params, ArgumentData& data) {
                if (params.empty()) {
                    throw fastchess_exception::format("Option \"{}\" expects key=value parameters.", flag);
                }

                std::vector<std::pair<std::string, std::string>> kv;
                kv.reserve(params.size());
                for (const auto& param : params) {
                    const auto pos = param.find('=');
                    if (pos == std::string::npos || pos == 0 || pos + 1 == param.size()) {
                        throw fastchess_exception::format("Option \"{}\" expects key=value pairs, got \"{}\".", flag,
                                                          param);
                    }
                    kv.emplace_back(param.substr(0, pos), param.substr(pos + 1));
                }

                fn(kv, data);
            };
        } else if constexpr (Style == ParamStyle::KeyValueOptional) {
            static_assert(std::is_invocable_r_v<void, Handler, const std::vector<std::pair<std::string, std::string>>&,
                                                ArgumentData&>,
                          "Handler must accept (key/value list, ArgumentData&)");
            std::function<void(const std::vector<std::pair<std::string, std::string>>&, ArgumentData&)> fn =
                std::forward<Handler>(handler);
            return [flag, fn](const std::vector<std::string>& params, ArgumentData& data) {
                std::vector<std::pair<std::string, std::string>> kv;
                kv.reserve(params.size());
                for (const auto& param : params) {
                    const auto pos = param.find('=');
                    if (pos == std::string::npos || pos == 0 || pos + 1 == param.size()) {
                        throw fastchess_exception::format("Option \"{}\" expects key=value pairs, got \"{}\".", flag,
                                                          param);
                    }
                    kv.emplace_back(param.substr(0, pos), param.substr(pos + 1));
                }

                fn(kv, data);
            };
        } else {
            static_assert(std::is_invocable_r_v<void, Handler, const std::vector<std::string>&, ArgumentData&>,
                          "Handler must accept (value list, ArgumentData&)");
            std::function<void(const std::vector<std::string>&, ArgumentData&)> fn = std::forward<Handler>(handler);
            return [fn](const std::vector<std::string>& params, ArgumentData& data) { fn(params, data); };
        }
    }

    ArgumentData argument_data_;

    std::unordered_map<std::string, OptionEntry> options_;
};

}  // namespace fastchess::cli
