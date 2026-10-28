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

#include <functional>
#include <vector>

#include "preset_entry.hpp"
#include "virtualized_list.hpp"

namespace openq::presets
{
    class PresetList final : public VirtualizedList
    {
    public:
        explicit PresetList (PresetStyle& base);

        void setPresets (const std::vector<PresetEntry>& presets, const juce::File& selected_file, bool show_groups);

        void selectFile (const juce::File& file, bool scroll_to_row = false);

        std::function<void (const juce::File&)> onPresetSelected;
        std::function<void (const juce::File&)> onPresetLoad;

    private:
        const std::vector<PresetEntry>* presets_ { nullptr };
        bool show_groups_ { false };

        void paintRow (juce::Graphics& g, int row, juce::Rectangle<int> bounds, bool selected, bool hovered) override;

        void rowClicked (int row) override;

        void rowDoubleClicked (int row) override;

        int findFile (const juce::File& file) const;
    };
}
