// SPDX-License-Identifier: AGPL-3.0-only
#include "IconButton.h"
#include "Palette.h"
namespace openq
{
    IconButton::IconButton (const juce::String& name, const void* svg, int bytes)
        : DrawableButton (name, juce::DrawableButton::ImageFitted)
    {
        image = juce::Drawable::createFromImageData (svg, static_cast<size_t> (bytes));
        jassert (image != nullptr);
        if (image == nullptr)
            return;
        hover = image->createCopy();
        active = image->createCopy();
        image->replaceColour (juce::Colours::black, palette::inactiveIcon);
        hover->replaceColour (juce::Colours::black, palette::text);
        accentColour = juce::Colours::black;
        setAccentColour (palette::accent);
    }

    void IconButton::setAccentColour (juce::Colour colour)
    {
        if (active == nullptr || colour == accentColour)
            return;
        active->replaceColour (accentColour, colour);
        accentColour = colour;
        setImages (image.get(), hover.get(), hover.get(), nullptr, active.get(), hover.get(), active.get());
    }
}
