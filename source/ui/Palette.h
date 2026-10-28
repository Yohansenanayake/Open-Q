// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <array>
#include <juce_graphics/juce_graphics.h>

namespace openq::palette
{
    inline const juce::Colour background { 0xff161b23 };
    inline const juce::Colour bottomBarBackground { 0xff353638 };
    inline const juce::Colour cardBackground { 0xff222b35 };
    inline constexpr float globalBypassBlend = 0.55f;
    inline juce::Colour dynamicColour (juce::Colour colour, bool globallyBypassed) noexcept
    {
        return globallyBypassed ? colour.interpolatedWith (background, globalBypassBlend).withAlpha (colour.getFloatAlpha()) : colour;
    }
    inline constexpr float bypassBandAccentBlend = 0.6f;
    inline const juce::Colour meterLow { 0xff4da5ba };
    inline const juce::Colour meterWarning { 0xffe4bc60 };
    inline const juce::Colour meterOver { 0xfff26868 };
    inline const juce::Colour accent { 0xff6edbd4 };
    inline const juce::Colour text { 0xffe0e7ef };
    inline const juce::Colour secondaryText { 0xffb4bfce };
    inline const juce::Colour inactiveIcon { 0xff8996a5 };
    inline const juce::Colour grid { 0xff2b3441 };
    inline const juce::Colour zeroLine { 0xff566375 };
    inline const juce::Colour analyzerPre { 0xffb4bfce };
    inline const juce::Colour analyzerPost { 0xffc693df };

    // Interleave hues so neighbouring slots are easy to tell apart on the dark graph.
    inline const std::array bandColours {
        juce::Colour { 0xff6edbd4 }, juce::Colour { 0xffffb86b }, juce::Colour { 0xffa899ff }, juce::Colour { 0xff83dc91 },
        juce::Colour { 0xffff8199 }, juce::Colour { 0xff78baff }, juce::Colour { 0xffe5d66f }, juce::Colour { 0xffe997e0 },
        juce::Colour { 0xff55c6ab }, juce::Colour { 0xffff9675 }, juce::Colour { 0xff91a1ed }, juce::Colour { 0xffb4d875 },
        juce::Colour { 0xffe67eaf }, juce::Colour { 0xff6dcfe8 }, juce::Colour { 0xffdca65d }, juce::Colour { 0xffbd8fe3 },
        juce::Colour { 0xff68c88b }, juce::Colour { 0xffed887f }, juce::Colour { 0xff8bc9e0 }, juce::Colour { 0xffcedf93 },
        juce::Colour { 0xffd4a0d1 }, juce::Colour { 0xff8dd4bf }, juce::Colour { 0xffebbe94 }, juce::Colour { 0xffb7b8ed }
    };

    inline juce::Colour bandColour (size_t slot) noexcept
    {
        return bandColours[slot % bandColours.size()];
    }
}
