// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "CompactComboBox.h"
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
        bool hasPendingGainFeedback() const { return inputGain.hasPendingUserFeedback() || outputGain.hasPendingUserFeedback(); }
        void paint (juce::Graphics&) override;
        void resized() override;
        juce::TextButton analyzer { "Analyzer" };
        CompactComboBox oversampling;
        void refreshOversampling (int requested, int active, bool pending);
        static constexpr int preferredHeight = 36;

    private:
        GainControl inputGain, outputGain;
        juce::Label inputLabel, outputLabel;
        juce::Label oversamplingLabel;
        int displayedOversampling = -1, displayedActive = -1;
        bool displayedPending = false;
        UiLookAndFeel lookAndFeel;
    };
}
