// SPDX-License-Identifier: AGPL-3.0-only
#include "IconButton.h"
#include "Palette.h"
namespace openq
{
    IconButton::IconButton (const juce::String& name, const void* svg, int bytes)
        : DrawableButton (name, juce::DrawableButton::ImageFitted)
    {
        auto image = juce::Drawable::createFromImageData (svg, static_cast<size_t> (bytes));
        jassert (image != nullptr);
        if (image == nullptr)
            return;
        auto hover = image->createCopy();
        auto active = image->createCopy();
        image->replaceColour (juce::Colours::black, palette::inactiveIcon);
        hover->replaceColour (juce::Colours::black, palette::text);
        active->replaceColour (juce::Colours::black, palette::accent);
        setImages (image.get(), hover.get(), hover.get(), nullptr, active.get(), hover.get(), active.get());
    }
}
