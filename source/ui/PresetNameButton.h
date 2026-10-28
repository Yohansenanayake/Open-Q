// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "Palette.h"
#include <juce_gui_basics/juce_gui_basics.h>
namespace openq
{
    class PresetNameButton final : public juce::Button
    {
    public:
        PresetNameButton() : juce::Button ("Default")
        {
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            setTitle ("Presets: Default");
        }

    private:
        void paintButton (juce::Graphics& g, bool highlighted, bool down) override
        {
            const auto scale = static_cast<float> (getHeight()) / 26.0f;
            auto area = getLocalBounds().toFloat().reduced (4.0f * scale, 0.0f);
            const auto arrow = area.removeFromRight (12.0f * scale);
            g.setColour (highlighted || down || hasKeyboardFocus (true) ? palette::text : palette::secondaryText);
            g.setFont (15.0f * scale);
            g.drawText (getButtonText(), area, juce::Justification::centred, true);
            const auto centre = arrow.getCentre();
            juce::Path chevron;
            chevron.startNewSubPath (centre.x - 3.0f * scale, centre.y - 1.5f * scale);
            chevron.lineTo (centre.x, centre.y + 1.5f * scale);
            chevron.lineTo (centre.x + 3.0f * scale, centre.y - 1.5f * scale);
            g.strokePath (chevron, juce::PathStrokeType (scale));
        }
    };
}
