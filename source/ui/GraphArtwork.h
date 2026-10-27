// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace openq
{
    // Static artwork stays ordinary painting code; JUCE owns cache storage and invalidation.
    class GraphArtwork : public juce::Component
    {
    public:
        GraphArtwork();
        void setDisplay (juce::Rectangle<int> plotBounds, double sampleRate, bool analyzerVisible);
        void paint (juce::Graphics&) override;

    private:
        juce::Rectangle<int> plot;
        double rate = 0.0;
        bool showAnalyzer = false;
    };
}
