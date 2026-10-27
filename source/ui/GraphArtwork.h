// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace openq
{
    // Cache bounded label regions; draw grid lines without a full-plot transparent image.
    class GraphArtwork : public juce::Component
    {
    public:
        static constexpr int axisLabelWidth = 24, axisLabelGap = 6;
        static constexpr int leftMargin = axisLabelWidth + axisLabelGap + 2;
        GraphArtwork();
        ~GraphArtwork() override;
        void resized() override;
        void setDisplay (juce::Rectangle<int> plotBounds, double sampleRate, bool analyzerVisible, float scale = 1.0f);
        void paint (juce::Graphics&) override;

    private:
        class LabelLayer;
        void updateLabelBounds();
        std::vector<std::unique_ptr<LabelLayer>> labelLayers;
        juce::Rectangle<int> plot;
        double rate = 0.0;
        float uiScale = 1.0f;
        bool showAnalyzer = false;
    };
}
