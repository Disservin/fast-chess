#pragma once

#include <chrono>
#include <string>
#include <vector>

#include <core/helper.hpp>
#include <core/rand.hpp>

#include <types/csv.hpp>
#include <types/draw_adjudication.hpp>
#include <types/engine_config.hpp>
#include <types/enums.hpp>
#include <types/epd.hpp>
#include <types/log.hpp>
#include <types/max_moves_adjudication.hpp>
#include <types/opening.hpp>
#include <types/pgn.hpp>
#include <types/resign_adjudication.hpp>
#include <types/sprt.hpp>
#include <types/tb_adjudication.hpp>

namespace fastchess::config {

struct Tournament {
    Opening opening = {};
    Pgn pgn         = {};
    Epd epd         = {};
    // not part of the json config, -csvout has to be passed again when resuming
    Csv csv = {};

    Sprt sprt = {};

    std::string config_name = "config.json";

    DrawAdjudication draw          = {};
    ResignAdjudication resign      = {};
    MaxMovesAdjudication maxmoves  = {};
    TbAdjudication tb_adjudication = {};

    VariantType variant = VariantType::STANDARD;
    TournamentType type = TournamentType::ROUNDROBIN;
    int gauntlet_seeds  = 1;

#ifdef USE_CUTE
    // output format, fastchess or cutechess
    OutputType output    = OutputType::CUTECHESS;
    int autosaveinterval = 0;
    int ratinginterval   = 0;
    int games            = 1;
    int rounds           = 1;
    bool report_penta    = false;
#else
    // output format, fastchess or cutechess
    OutputType output    = OutputType::FASTCHESS;
    int autosaveinterval = 20;
    int ratinginterval   = 10;
    std::size_t games    = 2;
    std::size_t rounds   = 2;
    bool report_penta    = true;
#endif

    uint64_t seed = random::random_uint64();

    int scoreinterval = 1;

    int concurrency        = 1;
    uint32_t wait          = 0;
    bool force_concurrency = false;

    bool noswap         = false;
    bool reverse        = false;
    bool recover        = false;
    bool affinity       = false;
    bool check_mate_pvs = false;
    bool show_latency   = false;
    bool test_env       = false;
    bool strict         = false;

    std::chrono::milliseconds startup_time    = std::chrono::seconds(10);
    std::chrono::milliseconds ucinewgame_time = std::chrono::seconds(60);
    std::chrono::milliseconds ping_time       = std::chrono::seconds(60);

    std::vector<int> affinity_cpus;

    Log log = {};
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Tournament, resign, draw, maxmoves, tb_adjudication, opening, pgn, epd, sprt,
                                   config_name, output, seed, variant, type, gauntlet_seeds, ratinginterval,
                                   scoreinterval, wait, autosaveinterval, games, rounds, concurrency, force_concurrency,
                                   recover, noswap, reverse, report_penta, affinity, check_mate_pvs, show_latency, log)

}  // namespace fastchess::config
