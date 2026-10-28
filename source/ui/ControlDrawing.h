// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "Palette.h"

namespace openq
{
    namespace controlStyle
    {
        inline constexpr int height = 26;
        inline constexpr float cornerRadius = 4.0f, borderWidth = 1.0f, textPadding = 6.0f;
        inline constexpr float disabledOpacity = 0.45f;
    }

    inline void drawControlBackground (juce::Graphics& g, juce::Rectangle<float> bounds, float scale,
        bool enabled, bool hovered, bool focused, bool active = false)
    {
        const auto opacity = enabled ? 1.0f : controlStyle::disabledOpacity;
        const auto fill = enabled && active ? palette::selection : enabled && hovered ? palette::controlHover : palette::controlBackground;
        const auto border = enabled && (focused || hovered) ? palette::controlHoverBorder : palette::controlBorder;
        bounds = bounds.reduced (controlStyle::borderWidth * scale * 0.5f);
        g.setColour (fill.withMultipliedAlpha (opacity));
        g.fillRoundedRectangle (bounds, controlStyle::cornerRadius * scale);
        g.setColour (border.withMultipliedAlpha (opacity));
        g.drawRoundedRectangle (bounds, controlStyle::cornerRadius * scale, controlStyle::borderWidth * scale);
    }
}
