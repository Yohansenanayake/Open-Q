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
#include "PresetStyle.h"
#include <juce_gui_basics/juce_gui_basics.h>
namespace openq::presets
{
    class WarningOverlay final : public juce::Component
    {
    public:
        explicit WarningOverlay (PresetStyle& style);
        void paint (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress&) override;
        void show (const juce::String&, const juce::String&, bool, std::function<void()>);
        void showMessage (const juce::String&);
        void hide();

    private:
        PresetStyle& style_;
        juce::Label message_;
        juce::TextButton cancel_ { "Cancel" }, confirm_;
        std::function<void()> action_;
    };
}
