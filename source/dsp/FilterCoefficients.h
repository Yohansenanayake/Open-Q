// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "FilterType.h"

namespace openq
{
    struct BiquadCoefficients
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    };

    BiquadCoefficients makeFilterCoefficients (FilterType type, double sampleRate, double frequencyHz, double gainDb, double q);
}
