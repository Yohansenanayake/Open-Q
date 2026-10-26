// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

namespace openq
{
    struct BiquadCoefficients
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    };

    BiquadCoefficients makeBellCoefficients (double sampleRate, double frequencyHz, double gainDb, double q);
}
