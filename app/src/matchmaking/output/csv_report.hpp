#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <core/filesystem/file_writer.hpp>
#include <matchmaking/elo/elo_pentanomial.hpp>
#include <matchmaking/elo/elo_wdl.hpp>
#include <matchmaking/sprt/sprt.hpp>
#include <matchmaking/stats.hpp>

#define FMT_HEADER_ONLY
#include <fmt/include/fmt/core.h>

namespace fastchess {

// Appends one row per interval so that llr and elo can be plotted against the
// number of games. Read only: nothing here feeds back into the match scheduling
// or into the SPRT decision.
class CsvReport {
   public:
    CsvReport(const std::string& file, bool append, const std::string& separator, bool report_penta)
        : needs_header_(!append || isEmpty(file)),
          writer_(file, append),
          separator_(separator),
          report_penta_(report_penta) {
        if (needs_header_) writer_.write(header());
    }

    void update(const SPRT& sprt, const Stats& stats) {
        const auto elo = createElo(stats);

        std::vector<std::string> row;

        row.emplace_back(fmt::format("{}", ++iteration_));
        row.emplace_back(fmt::format("{}", stats.sum()));
        row.emplace_back(fmt::format("{}", stats.totalPairs()));

        if (sprt.isEnabled()) {
            const auto llr = sprt.getLLR(stats, report_penta_);

            row.emplace_back(num(llr, 4));
            row.emplace_back(num(sprt.getLowerBound(), 4));
            row.emplace_back(num(sprt.getUpperBound(), 4));
        } else {
            row.insert(row.end(), 3, "");
        }

        row.emplace_back(num(elo->diff(), 2));
        row.emplace_back(num(elo->error(), 2));
        row.emplace_back(num(elo->diff() - elo->error(), 2));
        row.emplace_back(num(elo->diff() + elo->error(), 2));
        row.emplace_back(num(elo->nEloDiff(), 2));
        row.emplace_back(num(elo->nEloError(), 2));
        row.emplace_back(num(stats.pointsRatio(), 4));

        row.emplace_back(fmt::format("{}", stats.wins));
        row.emplace_back(fmt::format("{}", stats.losses));
        row.emplace_back(fmt::format("{}", stats.draws));

        row.emplace_back(fmt::format("{}", stats.penta_LL));
        row.emplace_back(fmt::format("{}", stats.penta_LD));
        row.emplace_back(fmt::format("{}", stats.penta_WL + stats.penta_DD));
        row.emplace_back(fmt::format("{}", stats.penta_WD));
        row.emplace_back(fmt::format("{}", stats.penta_WW));

        writer_.write(join(row));
    }

   private:
    static bool isEmpty(const std::string& file) {
        std::ifstream in(file, std::ios::binary);
        return !in.good() || in.peek() == std::ifstream::traits_type::eof();
    }

    // empty cell instead of nan/inf, spreadsheets read those as text
    static std::string num(double value, int precision) {
        if (!std::isfinite(value)) return "";
        return fmt::format("{:.{}f}", value, precision);
    }

    std::string join(const std::vector<std::string>& fields) const {
        std::string line;

        for (std::size_t i = 0; i < fields.size(); i++) {
            if (i) line += separator_;
            line += fields[i];
        }

        return line + "\n";
    }

    std::string header() const {
        return join({"iteration", "games", "pairs", "llr", "llr_lower", "llr_upper", "elo", "elo_err", "elo_lower",
                     "elo_upper", "nelo", "nelo_err", "score", "wins", "losses", "draws", "LL", "LD", "WLDD", "WD",
                     "WW"});
    }

    std::unique_ptr<elo::EloBase> createElo(const Stats& stats) const {
        if (report_penta_) return std::make_unique<elo::EloPentanomial>(stats);
        return std::make_unique<elo::EloWDL>(stats);
    }

    const bool needs_header_;
    util::FileWriter writer_;

    const std::string separator_;
    const bool report_penta_;

    std::uint64_t iteration_ = 0;
};

}  // namespace fastchess
