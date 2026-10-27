// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
namespace openq
{
    class IconButton final : public juce::DrawableButton
    {
    public:
        IconButton (const juce::String& name, const void* svg, int bytes);
        void setAccentColour (juce::Colour colour);

    private:
        std::unique_ptr<juce::Drawable> image, hover, active;
        juce::Colour accentColour;
    };
}
