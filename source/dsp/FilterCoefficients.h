// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "CutSlope.h"
#include "FilterType.h"

namespace openq
{
    struct BiquadCoefficients
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    };

    struct BandCoefficients
    {
        std::array<BiquadCoefficients, maximumFilterSections> sections {};
        size_t count = 1;
    };

    // Single-section bell/shelf designs retain their pinned behavior.
    BiquadCoefficients makeFilterCoefficients (FilterType type, double sampleRate, double frequencyHz, double gainDb, double q);
    BandCoefficients makeBandCoefficients (FilterType type, double sampleRate, double frequencyHz, double gainDb, double q, CutSlope slope = defaultCutSlope);
}
