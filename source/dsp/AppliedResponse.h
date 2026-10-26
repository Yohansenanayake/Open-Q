// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "BellCoefficients.h"

namespace openq
{
    struct AppliedResponse
    {
        BiquadCoefficients coefficients;
        double wet = 0.0;
        double sampleRate = 0.0;
        bool operator== (const AppliedResponse&) const = default;
    };
}
