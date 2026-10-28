// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "../Palette.h"
namespace openq::presets
{
    struct PresetStyle
    {
        float scale = 1.0f;
        float getFontSize() const { return 10.0f * scale; }
        juce::Colour getTextColour() const { return palette::text; }
    };
}
