// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>

namespace openq::bandLimits
{
    inline constexpr double minimumFrequencyHz = 20.0;
    inline constexpr double maximumFrequencyHz = 20000.0;
    inline constexpr double defaultFrequencyHz = 1000.0;
    inline constexpr double frequencySkewCentreHz = 1000.0;
    inline constexpr double minimumGainDb = -24.0;
    inline constexpr double maximumGainDb = 24.0;
    inline constexpr double defaultGainDb = 0.0;
    inline constexpr double minimumQ = 0.1;
    inline constexpr double maximumQ = 10.0;
    inline constexpr double defaultQ = 1.0;
    inline constexpr double qSkewCentre = 1.0;
    inline constexpr double minimumSampleRateHz = 1000.0;
    // Leave a margin below Nyquist for coefficient design.
    inline constexpr double maximumFrequencySampleRateRatio = 0.49;

    inline double effectiveMaximumFrequency (double sampleRate)
    {
        return std::min (maximumFrequencyHz, sampleRate * maximumFrequencySampleRateRatio);
    }
}
