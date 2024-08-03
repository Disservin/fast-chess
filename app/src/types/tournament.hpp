#pragma once

#include <string>

#include <util/helper.hpp>
#include <util/rand.hpp>

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

namespace fastchess::config {

struct Tournament {
    Opening opening = {};
    Pgn pgn         = {};
    Epd epd         = {};

    Sprt sprt = {};

    std::string config_name;

    DrawAdjudication draw         = {};
    ResignAdjudication resign     = {};
    MaxMovesAdjudication maxmoves = {};

    VariantType variant = VariantType::STANDARD;

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
    int games            = 2;
    int rounds           = 2;
    bool report_penta    = true;
#endif

    uint64_t seed = util::random::random_uint64();

    int scoreinterval = 1;

    int concurrency = 1;
    int overhead    = 0;
    uint32_t wait   = 0;

    bool noswap   = false;
    bool recover  = false;
    bool affinity = false;

    Log log = {};
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_ORDERED_JSON(Tournament, resign, draw, maxmoves, opening, pgn, epd, sprt,
                                                config_name, output, seed, variant, ratinginterval, scoreinterval, wait,
                                                autosaveinterval, games, rounds, concurrency, overhead, recover, noswap,
                                                report_penta, affinity, log)

}  // namespace fastchess::config
