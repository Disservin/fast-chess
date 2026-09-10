#include <matchmaking/elo/elo_pentanomial.hpp>

#include <cmath>

#include <fmt/include/fmt/core.h>

namespace fastchess::elo {

EloPentanomial::EloPentanomial(const Stats& stats) {
    pairs_             = stats.totalPairs();
    score_             = calcScore(stats);
    variance_          = calcVariance(stats);
    variance_per_pair_ = variance_ / pairs_;
    scoreUpperBound_   = score_ + CI95ZSCORE * std::sqrt(variance_per_pair_);
    scoreLowerBound_   = score_ - CI95ZSCORE * std::sqrt(variance_per_pair_);
    diff_              = scoreToEloDiff(score_);
    error_             = (scoreToEloDiff(scoreUpperBound_) - scoreToEloDiff(scoreLowerBound_)) / 2.0;
    nelodiff_          = scoreToNeloDiff(score_, variance_);
    neloerror_ = (scoreToNeloDiff(scoreUpperBound_, variance_) - scoreToNeloDiff(scoreLowerBound_, variance_)) / 2.0;
}

double EloPentanomial::scoreToNeloDiff(double score, double variance) noexcept {
    return (score - 0.5) / std::sqrt(2 * variance) * (800 / std::log(10));
}

double EloPentanomial::calcScore(const Stats& stats) const noexcept {
    const double WW = double(stats.penta_WW) / pairs_;
    const double WD = double(stats.penta_WD) / pairs_;
    const double WL = double(stats.penta_WL) / pairs_;
    const double DD = double(stats.penta_DD) / pairs_;
    const double LD = double(stats.penta_LD) / pairs_;

    return WW + 0.75 * WD + 0.5 * (WL + DD) + 0.25 * LD;
}

double EloPentanomial::calcVariance(const Stats& stats) const noexcept {
    const double WW       = double(stats.penta_WW) / pairs_;
    const double WD       = double(stats.penta_WD) / pairs_;
    const double WL       = double(stats.penta_WL) / pairs_;
    const double DD       = double(stats.penta_DD) / pairs_;
    const double LD       = double(stats.penta_LD) / pairs_;
    const double LL       = double(stats.penta_LL) / pairs_;
    const double WW_dev   = WW * std::pow((1 - score_), 2);
    const double WD_dev   = WD * std::pow((0.75 - score_), 2);
    const double WLDD_dev = (WL + DD) * std::pow((0.5 - score_), 2);
    const double LD_dev   = LD * std::pow((0.25 - score_), 2);
    const double LL_dev   = LL * std::pow((0 - score_), 2);

    return WW_dev + WD_dev + WLDD_dev + LD_dev + LL_dev;
}

std::string EloPentanomial::nElo() const noexcept { return fmt::format("{:.2f} +/- {:.2f}", nelodiff_, neloerror_); }

std::string EloPentanomial::los() const noexcept {
    const double los = (1 - std::erf(-(score_ - 0.5) / std::sqrt(2.0 * variance_per_pair_))) / 2.0;

    return fmt::format("{:.2f} %", los * 100.0);
}

double EloPentanomial::getScore() const noexcept { return score_; }

}  // namespace fastchess::elo
