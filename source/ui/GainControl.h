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
        void paintOverChildren (juce::Graphics&) override;
        bool hasPendingUserFeedback() const { return pendingUserFeedback; }
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        void messageThreadTick (double seconds);
        void mouseDoubleClick (const juce::MouseEvent&) override;
        bool keyPressed (const juce::KeyPress&) override;
        void visibilityChanged() override;

    private:
        void updateText();
        void commitText();
        void setValueWithUserGesture (double);
        static constexpr double maximumMessageGapSeconds = 0.2;
        static constexpr double wheelStepDb = 0.5, fineWheelStepDb = 0.1, smoothWheelUnitsPerStep = 0.1;
        juce::int64 lastWheelEventMilliseconds = -1;
        double lastMessageTick = 0.0;
        int timingControl = 0;
        bool dragging = false, discardDrag = false;
        bool changingValueFromUser = false, pendingUserFeedback = false;
        juce::Label value;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
}
