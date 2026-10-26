// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

namespace openq
{
    enum class FilterType {
        Bell = 0,
        LowShelf = 1,
        HighShelf = 2,
        LowCut = 3,
        HighCut = 4
    };

    constexpr bool isCut (FilterType type)
    {
        return type == FilterType::LowCut || type == FilterType::HighCut;
    }
}
