// Copyright (C) 2026 - zsliu98
// This file is part of ZLEqualizer
//
// ZLEqualizer is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// ZLEqualizer is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with ZLEqualizer. If not, see <https://www.gnu.org/licenses/>.
// Open-Q adaptation: Yohan Senanayake, 2026-10-28; pinned origin in THIRD_PARTY_NOTICES.md.

#include "rounded_text_editor.hpp"

namespace openq::presets
{
    RoundedTextEditor::RoundedTextEditor (PresetStyle& base) : base_ (base)
    {
        lookAndFeelChanged();
    }

    void RoundedTextEditor::lookAndFeelChanged()
    {
        setColour (backgroundColourId, palette::background);
        setColour (textColourId, base_.getTextColour());
        setColour (highlightColourId, base_.getTextColour().withAlpha (.22f));
        setColour (highlightedTextColourId, base_.getTextColour());
        setColour (outlineColourId, palette::inactiveIcon.withAlpha (0.65f));
        setColour (focusedOutlineColourId, palette::inactiveIcon.withAlpha (0.65f));
        setBorder (juce::BorderSize<int> (0));
        juce::TextEditor::lookAndFeelChanged();
    }
}
