// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "../PluginParameters.h"
namespace openq
{
    class GainControl final : public juce::Slider
    {
    public:
        GainControl (juce::AudioProcessorValueTreeState&, const char* id);
        ~GainControl() override;
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void messageThreadTick (double seconds);
        void mouseDoubleClick (const juce::MouseEvent&) override;
        bool keyPressed (const juce::KeyPress&) override;
        void visibilityChanged() override;

    private:
        void updateText();
        void commitText();
        static constexpr double maximumMessageGapSeconds = 0.2;
        double lastMessageTick = 0.0;
        bool dragging = false, discardDrag = false;
        juce::Label value;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
}
