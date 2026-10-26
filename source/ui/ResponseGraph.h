// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "ResponseWorker.h"

namespace openq
{
    class ResponseGraph : public juce::Component, private juce::Timer
    {
    public:
        ResponseGraph (juce::AudioProcessorValueTreeState& parameters, const std::atomic<double>& sampleRate);
        ~ResponseGraph() override;
        void paint (juce::Graphics& graphics) override;
        void paintOverChildren (juce::Graphics&) override;
        void resized() override;
        void visibilityChanged() override;
        void parentHierarchyChanged() override;

    private:
        void timerCallback() override;
        void updateVisibility();
        juce::Rectangle<int> plotBounds() const;
        ResponseWorker worker;
        std::unique_ptr<juce::VBlankAttachment> vblank;
    };
}
