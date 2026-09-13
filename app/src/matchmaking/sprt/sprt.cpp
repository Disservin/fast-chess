#include <matchmaking/sprt/sprt.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>

#include <core/logger/logger.hpp>
#include <matchmaking/stats.hpp>
#include <types/exception.hpp>

namespace fastchess {

SPRT::SPRT(double alpha, double beta, double elo0, double elo1, std::string model, bool enabled) {
    enabled_ = enabled;

    if (enabled_) {
        lower_ = std::log(beta / (1 - alpha));
        upper_ = std::log((1 - beta) / alpha);

        elo0_ = elo0;
        elo1_ = elo1;

        model_ = model;

        LOG_TRACE("Initialized valid SPRT configuration.");
    }
}

double SPRT::leloToScore(double lelo) noexcept { return 1 / (1 + std::pow(10, (-lelo / 400))); }

double SPRT::bayeseloToScore(double bayeselo, double drawelo) noexcept {
    double pwin  = 1.0 / (1.0 + std::pow(10.0, (-bayeselo + drawelo) / 400.0));
    double ploss = 1.0 / (1.0 + std::pow(10.0, (bayeselo + drawelo) / 400.0));
    double pdraw = 1.0 - pwin - ploss;

    return pwin + 0.5 * pdraw;
}

double SPRT::neloToScoreWDL(double nelo, double variance) noexcept {
    return nelo * std::sqrt(variance) / (800.0 / std::log(10)) + 0.5;
}

double SPRT::neloToScorePenta(double nelo, double variance) noexcept {
    return nelo * std::sqrt(2.0 * variance) / (800.0 / std::log(10)) + 0.5;
}

void SPRT::isValid(double alpha, double beta, double elo0, double elo1, std::string model, bool& report_penta) {
    if (elo0 >= elo1) {
        throw fastchess_exception("Error; SPRT: elo0 must be less than elo1!");
    } else if (alpha <= 0 || alpha >= 1) {
        throw fastchess_exception("Error; SPRT: alpha must be a decimal number between 0 and 1!");
    } else if (beta <= 0 || beta >= 1) {
        throw fastchess_exception("Error; SPRT: beta must be a decimal number between 0 and 1!");
    } else if (alpha + beta >= 1) {
        throw fastchess_exception("Error; SPRT: sum of alpha and beta must be less than 1!");
    } else if (model != "normalized" && model != "bayesian" && model != "logistic") {
        throw fastchess_exception("Error; SPRT: invalid SPRT model!");
    }

    if (model == "bayesian" && report_penta) {
        Logger::print<Logger::Level::WARN>(
            "Warning; Bayesian SPRT model not available with pentanomial statistics. Disabling "
            "pentanomial reports...");
        report_penta = false;
    }
}

static double regularize(int value) {
    if (value == 0) return 1e-3;
    return value;
}

double SPRT::getLLR(const Stats& stats, bool penta) const noexcept {
    if (penta)
        return getLLR(stats.penta_WW, stats.penta_WD, stats.penta_WL, stats.penta_DD, stats.penta_LD, stats.penta_LL);

    return getLLR(stats.wins, stats.draws, stats.losses);
}

double SPRT::getFraction(const double llr) const noexcept {
    if (llr >= 0) {
        return llr / upper_;
    } else {
        return -llr / lower_;
    }
}

double SPRT::getLLR(int win, int draw, int loss) const noexcept {
    if (!enabled_) return 0.0;

    double L     = regularize(loss);
    double D     = regularize(draw);
    double W     = regularize(win);
    double total = L + D + W;
    std::array<double, 3> probs{L / total, D / total, W / total};

    if (model_ == "normalized") {
        double t0 = elo0_ / (800.0 / std::log(10));
        double t1 = elo1_ / (800.0 / std::log(10));
        return getLLR_normalized(total, {0.0, 0.5, 1.0}, probs, t0, t1);
    } else if (model_ == "bayesian") {
        if (win == 0 || loss == 0) return 0.0;
        double L       = probs[0];
        double W       = probs[2];
        double drawelo = 200 * std::log10((1 - L) / L * (1 - W) / W);
        double score0  = bayeseloToScore(elo0_, drawelo);
        double score1  = bayeseloToScore(elo1_, drawelo);
        return getLLR_logistic(total, {0.0, 0.5, 1.0}, probs, score0, score1);
    } else {
        double score0 = leloToScore(elo0_);
        double score1 = leloToScore(elo1_);
        return getLLR_logistic(total, {0.0, 0.5, 1.0}, probs, score0, score1);
    }
}

double SPRT::getLLR(int penta_WW, int penta_WD, int penta_WL, int penta_DD, int penta_LD, int penta_LL) const noexcept {
    if (!enabled_) return 0.0;

    double LL    = regularize(penta_LL);
    double LD    = regularize(penta_LD);
    double WL_DD = regularize(penta_DD + penta_WL);
    double WD    = regularize(penta_WD);
    double WW    = regularize(penta_WW);
    double total = WW + WD + WL_DD + LD + LL;
    std::array<double, 5> probs{
        LL / total, LD / total, WL_DD / total, WD / total, WW / total,
    };

    if (model_ == "normalized") {
        double t0 = std::sqrt(2.0) * elo0_ / (800.0 / std::log(10));
        double t1 = std::sqrt(2.0) * elo1_ / (800.0 / std::log(10));
        return getLLR_normalized(total, {0.0, 0.25, 0.5, 0.75, 1.0}, probs, t0, t1);
    } else {
        double score0 = leloToScore(elo0_);
        double score1 = leloToScore(elo1_);
        return getLLR_logistic(total, {0.0, 0.25, 0.5, 0.75, 1.0}, probs, score0, score1);
    }
}

template <size_t N>
static double mean(std::array<double, N> x, std::array<double, N> p) {
    double result = 0.0;
    for (size_t i = 0; i < N; i++) result += x[i] * p[i];
    return result;
}

template <size_t N>
static std::tuple<double, double> mean_and_variance(std::array<double, N> x, std::array<double, N> p) {
    double mu  = mean(x, p);
    double var = 0.0;
    for (size_t i = 0; i < N; i++) var += p[i] * (x[i] - mu) * (x[i] - mu);
    return std::make_tuple(mu, var);
}

// I. F. D. Oliveira and R. H. C. Takahashi. 2020. An Enhancement of the Bisection Method Average Performance
// Preserving Minmax Optimality. ACM Trans. Math. Softw. 47, 1, Article 5 (March 2021).
// https://doi.org/10.1145/3423597
template <typename F>
static double itp(F f, double a, double b, double f_a, double f_b, double k_1, double k_2, double n_0, double epsilon) {
    if (f_a > 0) {
        std::swap(a, b);
        std::swap(f_a, f_b);
    }
    assert(f_a < 0 && 0 < f_b);

    double n_half = std::ceil(std::log2(std::abs(b - a) / (2.0 * epsilon)));
    double n_max  = n_half + n_0;
    for (size_t i = 0; std::abs(b - a) > 2.0 * epsilon; i++) {
        double x_half = (a + b) / 2.0;
        double r      = epsilon * std::pow(2.0, n_max - i) - (b - a) / 2.0;
        double delta  = k_1 * std::pow(b - a, k_2);

        double x_f = (f_b * a - f_a * b) / (f_b - f_a);

        double sigma = (x_half - x_f) / std::abs(x_half - x_f);
        double x_t   = delta <= std::abs(x_half - x_f) ? x_f + sigma * delta : x_half;

        double x_itp = std::abs(x_t - x_half) <= r ? x_t : x_half - sigma * r;

        double f_itp = f(x_itp);
        if (std::fpclassify(f_itp) == FP_ZERO) {
            a = x_itp;
            b = x_itp;
        } else if (std::signbit(f_itp)) {
            a   = x_itp;
            f_a = f_itp;
        } else {
            b   = x_itp;
            f_b = f_itp;
        }
    }

    return (a + b) / 2.0;
}

template <size_t N>
double SPRT::getLLR_logistic(double total, std::array<double, N> scores, std::array<double, N> probs, double s0,
                             double s1) const noexcept {
    // Compute the maximum likelihood estimate for a discrete probability distribution with an expectation of s,
    // given an empirical distribution. See Proposition 1.1 of [1] for details.
    //
    // [1]: Michel Van den Bergh, Comparing the approximations for the generalized log likelihood ratio of a
    // multinomial distribution, https://www.cantate.be/Fishtest/comparing_approximations.pdf
    const auto mle = [&](double s) -> std::array<double, N> {
        const double theta_epsilon = 1e-3;

        // Solve equation 1.3 in [1] for theta.
        double min_theta = -1.0 / (scores[N - 1] - s);
        double max_theta = -1.0 / (scores[0] - s);
        double theta     = itp(
            [&](double x) -> double {
                double result = 0.0;
                for (size_t i = 0; i < N; i++) {
                    double a_i    = scores[i];
                    double phat_i = probs[i];
                    result += phat_i * (a_i - s) / (1.0 + x * (a_i - s));
                }
                return result;
            },
            min_theta, max_theta, INFINITY, -INFINITY, 0.1, 2.0, 0.99, theta_epsilon);

        // ML distribution is given by equation 1.2 in [1].
        std::array<double, N> p;
        for (size_t i = 0; i < N; i++) {
            double a_i    = scores[i];
            double phat_i = probs[i];
            p[i]          = phat_i / (1 + theta * (a_i - s));
        }
        return p;
    };

    std::array<double, N> p0 = mle(s0);
    std::array<double, N> p1 = mle(s1);
    std::array<double, N> lpr;
    for (size_t i = 0; i < N; i++) lpr[i] = std::log(p1[i]) - std::log(p0[i]);
    return total * mean(lpr, probs);
}

template <size_t N>
double SPRT::getLLR_normalized(double total, std::array<double, N> scores, std::array<double, N> probs, double t0,
                               double t1) const noexcept {
    // Compute the maximum likelihood estimate for a discrete probability distribution that has t = (mu - mu_ref) /
    // sigma, given an empirical distribution. See Section 4.1 of [1] for details.
    //
    // [1]: Michel Van den Bergh, Comments on normalized Elo,
    // https://www.cantate.be/Fishtest/normalized_elo_practical.pdf
    const auto mle = [&](double mu_ref, double t_star) -> std::optional<std::array<double, N>> {
        const double theta_epsilon = 1e-7;
        const double mle_epsilon   = 1e-4;

        // This is an iterative method, so we need to start with an initial value. As suggested in [1], we start with a
        // uniform distribution.
        std::array<double, N> p;

        const auto calc_phi = [&]() -> std::tuple<std::array<double, N>, double, double> {
            // Calculate phi.
            auto [mu, var] = mean_and_variance(scores, p);
            std::array<double, N> phi;
            for (size_t i = 0; i < N; i++) {
                double a_i   = scores[i];
                double sigma = std::sqrt(var);
                phi[i] = a_i - mu_ref - 0.5 * t_star * sigma * (1.0 + ((a_i - mu) / sigma) * ((a_i - mu) / sigma));
            }

            // We need to find a subset of the possible solutions for theta,
            // so we need to calculate our constraints for theta.
            double u = *std::min_element(phi.begin(), phi.end());
            double v = *std::max_element(phi.begin(), phi.end());

            return {phi, u, v};
        };

        const auto is_valid_initial_value = [&]() -> bool {
            auto [phi, u, v] = calc_phi();
            return u * v < 0;
        };

        std::fill(p.begin(), p.end(), 1.0 / N);
        if (!is_valid_initial_value()) {
            p = probs;
        }
        if (!is_valid_initial_value()) {
            return std::nullopt;  // Give up.
        }

        for (int iterations = 0; iterations < 10; iterations++) {
            std::array<double, N> phi;
            double u;
            double v;
            std::tie(phi, u, v) = calc_phi();

            double min_theta = -1.0 / v;
            double max_theta = -1.0 / u;

            // Solve equation 4.9 in [1] for theta.
            double theta = itp(
                [&](double x) -> double {
                    double result = 0.0;
                    for (size_t i = 0; i < N; i++) {
                        double phat_i = probs[i];
                        result += phat_i * phi[i] / (1.0 + x * phi[i]);
                    }
                    return result;
                },
                min_theta, max_theta, INFINITY, -INFINITY, 0.1, 2.0, 0.99, theta_epsilon);

            double max_diff = 0.0;
            // Calculate new estimate
            for (size_t i = 0; i < N; i++) {
                double phat_i = probs[i];
                double newp_i = phat_i / (1.0 + theta * phi[i]);
                max_diff      = std::max(max_diff, std::abs(newp_i - p[i]));
                p[i]          = newp_i;
            }

            // Good enough?
            if (max_diff < mle_epsilon) break;
        }

        return p;
    };

    auto p0 = mle(0.5, t0);
    auto p1 = mle(0.5, t1);

    if (!p0 || !p1) return 0.0;

    std::array<double, N> lpr;
    for (size_t i = 0; i < N; i++) lpr[i] = std::log((*p1)[i]) - std::log((*p0)[i]);
    return total * mean(lpr, probs);
}

SPRTResult SPRT::getResult(double llr) const noexcept {
    if (!enabled_) return SPRT_CONTINUE;

    if (llr >= upper_)
        return SPRT_H1;
    else if (llr <= lower_)
        return SPRT_H0;
    return SPRT_CONTINUE;
}

std::string SPRT::getBounds() const noexcept { return fmt::format("({:.2f}, {:.2f})", lower_, upper_); }

std::string SPRT::getElo() const noexcept { return fmt::format("[{:.2f}, {:.2f}]", elo0_, elo1_); }

double SPRT::getLowerBound() const noexcept { return lower_; }

double SPRT::getUpperBound() const noexcept { return upper_; }

bool SPRT::isEnabled() const noexcept { return enabled_; }

}  // namespace fastchess
