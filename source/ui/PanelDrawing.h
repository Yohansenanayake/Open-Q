// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "Palette.h"
namespace openq
{
    inline void drawPanelBackground (juce::Graphics& g, juce::Rectangle<float> bounds, float scale, juce::Colour border = palette::accent)
    {
        bounds = bounds.reduced (0.5f * scale);
        g.setColour (palette::cardBackground);
        g.fillRoundedRectangle (bounds, 8.0f * scale);
        g.setColour (border.withAlpha (0.65f));
        g.drawRoundedRectangle (bounds, 8.0f * scale, scale);
    }
}
