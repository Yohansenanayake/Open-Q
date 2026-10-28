// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <array>
#include <juce_graphics/juce_graphics.h>

namespace openq::palette
{
    // Surface and control roles: tune the theme here, rather than in individual painters.
    inline const juce::Colour background { 0xff111214 };
    inline const juce::Colour headerBackground { 0xff242429 };
    inline const juce::Colour brandingTop { 0xff1e1e23 };
    inline const juce::Colour brandingBottom { 0xff2b2b32 };
    inline const juce::Colour bottomBarBackground { 0xff29292d };
    inline const juce::Colour bottomBarTop { 0xff343439 };
    inline const juce::Colour cardBackground { 0xff202126 };
    inline const juce::Colour controlBackground { 0xff2b2c32 };
    inline const juce::Colour controlBorder { 0xff505159 };
    inline const juce::Colour controlHover { 0xff383940 };
    inline const juce::Colour knobTrack { 0xff131417 };
    inline const juce::Colour selection { 0xff48415f };
    inline constexpr float globalBypassBlend = 0.55f;
    inline juce::Colour dynamicColour (juce::Colour colour, bool globallyBypassed) noexcept
    {
        return globallyBypassed ? colour.interpolatedWith (background, globalBypassBlend).withAlpha (colour.getFloatAlpha()) : colour;
    }
    inline constexpr float bypassBandAccentBlend = 0.6f;
    inline const juce::Colour meterLow { 0xff78ae91 };
    inline const juce::Colour meterWarning { 0xffe4bc60 };
    inline const juce::Colour meterOver { 0xfff26868 };
    inline const juce::Colour accent { 0xffa69bff };
    inline const juce::Colour text { 0xffe7e7eb };
    inline const juce::Colour secondaryText { 0xffaaaab3 };
    inline const juce::Colour inactiveIcon { 0xff82828d };
    inline const juce::Colour grid { 0xff25262b };
    inline const juce::Colour zeroLine { 0xff4b4c55 };
    inline const juce::Colour analyzerPre { 0xff9899a3 };
    inline const juce::Colour analyzerPost { 0xffb6aecf };

    // Interleave hues so neighbouring slots are easy to tell apart on the dark graph.
    inline const std::array bandColours {
        juce::Colour { 0xffa899ff }, juce::Colour { 0xffffb86b }, juce::Colour { 0xff6edbd4 }, juce::Colour { 0xff83dc91 },
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
