// Copyright (C) 2026 - zsliu98
// Adapted for Open-Q by Yohan Senanayake, 26–27 October 2026.
// Extracted bell and second-order shelf designs from ZLEqualizer commit 3ce598ee065b3d3847121e76e196feda7ed8f56d.
// Changes: named normalized coefficients, response-type dispatch, input validation and an identity path.
//
// ZLEqualizer is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// ZLEqualizer is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with ZLEqualizer. If not, see <https://www.gnu.org/licenses/>.

#include "FilterCoefficients.h"
#include <algorithm>
#include <cmath>

namespace openq
{
    namespace
    {
        constexpr double kPi = 3.14159265358979323846;
        constexpr double kSigma = 2.00143;

        constexpr double kPiSq = kPi * kPi;
        constexpr double kTwoPiSq = 2.0 * kPiSq;

        constexpr double kSigmaSq = kSigma * kSigma;
        constexpr double kSigma4 = kSigmaSq * kSigmaSq;

        constexpr double kSplineWA = 0.4996419767299294;
        constexpr double kSplineWB = 0.5919981009;
        constexpr double kSplineInvDelta = 10.8276522968691715;
        constexpr double kSplineA = 1.4079737347730696;
        constexpr double kSplineB = -2.5538880428234330;
        constexpr double kSplineC = -0.0064417990487326;
        constexpr double kSplineD = 4.0057345308722958;

        struct PhiPair
        {
            double p1, p2, d;
        };

        inline double get_wrapping_w2 (const double w0)
        {
            if (w0 <= kSplineWA)
            {
                const auto pi_fc_fs = kPi * w0;
                const auto tan_val = std::tan (pi_fc_fs);
                return kSigmaSq + kPiSq / (tan_val * tan_val);
            }
            else if (w0 >= kSplineWB)
            {
                return 1.0 / (w0 * w0);
            }
            else
            {
                const double t = (w0 - kSplineWA) * kSplineInvDelta;
                return t * (t * (kSplineA * t + kSplineB) + kSplineC) + kSplineD;
            }
        }

        inline PhiPair get_phi_sq (const double w2, const double zeta2)
        {
            const auto z_term = 2.0 * zeta2 - 1.0;
            const auto kappa = w2 * z_term + kSigmaSq;
            const auto v = std::sqrt (std::abs (w2 * w2 + 2.0 * kSigmaSq * w2 * z_term + kSigma4));

            const auto sq_term = std::sqrt (std::abs (2.0 * (v + kappa)));
            const auto pi_sq_term = kPi * sq_term;

            const auto d = kPiSq + pi_sq_term + v;
            const auto d_inv = 1.0 / d;
            return { (kTwoPiSq - 2.0 * v) * d_inv, (kPiSq - pi_sq_term + v) * d_inv, d };
        }
    }

    BiquadCoefficients makeFilterCoefficients (FilterType type, double sampleRate, double frequencyHz, double gainDb, double q)
    {
        if (!std::isfinite (sampleRate) || sampleRate < 1000.0
            || !std::isfinite (frequencyHz) || !std::isfinite (gainDb) || !std::isfinite (q))
            return {};

        frequencyHz = std::clamp (frequencyHz, 20.0, std::min (20000.0, sampleRate * 0.49));
        gainDb = std::clamp (gainDb, -24.0, 24.0);
        q = std::clamp (q, 0.1, 10.0);
        if (std::fpclassify (gainDb) == FP_ZERO)
            return {};

        const auto w2 = get_wrapping_w2 (frequencyHz / sampleRate);
        const auto zeta2 = 1.0 / (4.0 * q * q);
        PhiPair alpha {}, beta {};
        double factor = 1.0;
        if (type == FilterType::Bell)
        {
            const auto g_linear = std::exp2 (gainDb * 0.16609640474436813);
            alpha = get_phi_sq (w2, zeta2 * g_linear);
            beta = get_phi_sq (w2, zeta2 / g_linear);
            factor = alpha.d / beta.d;
        }
        else if (type == FilterType::LowShelf || type == FilterType::HighShelf)
        {
            const auto g_half = std::exp2 (gainDb * (0.16609640474436813 * 0.5));
            const bool low = type == FilterType::LowShelf;
            alpha = get_phi_sq (low ? w2 / g_half : w2 * g_half, zeta2);
            beta = get_phi_sq (low ? w2 * g_half : w2 / g_half, zeta2);
            factor = alpha.d / beta.d;
            if (low)
                factor = (g_half * g_half) * (alpha.d / beta.d);
        }
        else
            return {};
        const BiquadCoefficients result { factor, factor * alpha.p1, factor * alpha.p2, beta.p1, beta.p2 };
        if (!std::isfinite (result.b0) || !std::isfinite (result.b1) || !std::isfinite (result.b2)
            || !std::isfinite (result.a1) || !std::isfinite (result.a2)
            || std::abs (result.a2) >= 1.0 || 1.0 + result.a1 + result.a2 <= 0.0 || 1.0 - result.a1 + result.a2 <= 0.0)
            return {};
        return result;
    }
}
