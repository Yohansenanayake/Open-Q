// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "../dsp/BandLimits.h"
#include "../dsp/FilterType.h"
#include <cmath>

namespace openq::responseMapping
{
    // Display limits are independent of the band's allowed gain range.
    inline constexpr int minimumDb = -24;
    inline constexpr int maximumDb = 24;
    inline constexpr int spanDb = maximumDb - minimumDb;

    inline double maximumFrequency (double sampleRate)
    {
        return std::min (bandLimits::maximumFrequencyHz, sampleRate * 0.5);
    }

    inline double frequencyToProportion (double frequency, double maximum)
    {
        return std::log (frequency / bandLimits::minimumFrequencyHz) / std::log (maximum / bandLimits::minimumFrequencyHz);
    }

    inline double proportionToFrequency (double proportion, double maximum)
    {
        return bandLimits::minimumFrequencyHz * std::pow (maximum / bandLimits::minimumFrequencyHz, proportion);
    }

    inline double dbToProportion (double db)
    {
        return (maximumDb - db) / spanDb;
    }

    template <typename T>
    T proportionToDb (T proportion)
    {
        return (T (1) - proportion) * T (spanDb) + T (minimumDb);
    }

    inline double nodeGainScale (FilterType type)
    {
        return type == FilterType::Bell ? 1.0 : 0.5;
    }
}
