// Copyright (C) 2026 - zsliu98
// This file is part of ZLEqualizer
//
// ZLEqualizer is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// ZLEqualizer is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with ZLEqualizer. If not, see <https://www.gnu.org/licenses/>.
// Open-Q adaptation: Yohan Senanayake, 2026-10-28; pinned origin in THIRD_PARTY_NOTICES.md.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "virtualized_list.hpp"

namespace openq::presets::preset_list_layout
{
    inline int contentInset (const float font_size)
    {
        return juce::roundToInt (font_size * VirtualizedList::kDefaultContentInsetScale);
    }

    inline int rowTextInset (const float font_size)
    {
        return juce::roundToInt (font_size * .96f);
    }

    inline int columnTextInset (const float font_size)
    {
        return contentInset (font_size) + rowTextInset (font_size);
    }
}
