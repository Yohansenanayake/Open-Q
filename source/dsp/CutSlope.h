// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <array>
#include <cstddef>

namespace openq
{
    enum class CutSlope { Db6 = 0,
        Db12,
        Db18,
        Db24,
        Db36,
        Db48,
        Db72,
        Db96 };
    inline constexpr std::array<size_t, 8> cutOrders { 1, 2, 3, 4, 6, 8, 12, 16 };
    inline constexpr std::array<const char*, cutOrders.size()> cutSlopeNames {
        "6 dB/oct",
        "12 dB/oct",
        "18 dB/oct",
        "24 dB/oct",
        "36 dB/oct",
        "48 dB/oct",
        "72 dB/oct",
        "96 dB/oct"
    };
    inline constexpr size_t maximumFilterSections = cutOrders.back() / 2;
    inline constexpr CutSlope defaultCutSlope = CutSlope::Db12;
}
