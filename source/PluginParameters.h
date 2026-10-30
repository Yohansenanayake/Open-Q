// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "BandParameters.h"
namespace openq
{
    inline constexpr int stateSchemaVersion = 8;
    inline constexpr std::array<const char*, 2> gainParameterIds { "input.gain", "output.gain" };
    inline constexpr auto bypassParameterId = "global.bypass";
    inline constexpr double bypassFadeSeconds = 0.02;
    inline constexpr size_t parameterCount = bandCount * bandParameterNames.size() + gainParameterIds.size() + 1;
    namespace gainLimits
    {
        inline constexpr float minimumDb = -30.0f, maximumDb = 30.0f, defaultDb = 0.0f;
        inline constexpr double smoothingSeconds = 0.02;
    }
}
