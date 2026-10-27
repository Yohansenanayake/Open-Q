// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "GainControl.h"
#include "UiLookAndFeel.h"
namespace openq
{
    class BottomBar final : public juce::Component
    {
    public:
        explicit BottomBar (juce::AudioProcessorValueTreeState&);
        ~BottomBar() override;
        void setUiScale (float);
        void messageThreadTick (double seconds);
        void paint (juce::Graphics&) override;
        void resized() override;
        juce::TextButton analyzer { "Analyzer" };
        static constexpr int preferredHeight = 36;

    private:
        GainControl inputGain, outputGain;
        juce::Label inputLabel, outputLabel;
        UiLookAndFeel lookAndFeel;
    };
}
