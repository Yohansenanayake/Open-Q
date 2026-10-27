// SPDX-License-Identifier: AGPL-3.0-only
#include "GraphArtwork.h"
#include "AnalyzerWorker.h"
#include "Palette.h"
#include "ResponseMapping.h"

namespace openq
{
    namespace
    {
        constexpr int gridStepDb = 6;
    }

    class GraphArtwork::LabelLayer final : public juce::Component
    {
    public:
        LabelLayer()
        {
            setInterceptsMouseClicks (false, false);
            setBufferedToImage (true);
        }
        void setLabel (juce::String value, juce::Rectangle<int> area, float fontSize, juce::Justification alignment, bool opaque)
        {
            text = std::move (value);
            textBounds = area;
            fontHeight = fontSize;
            justification = alignment;
            if (isOpaque() != opaque)
            {
                setBufferedToImage (false);
                setOpaque (opaque);
                setBufferedToImage (true);
            }
            setBounds (area.getIntersection (getParentComponent()->getLocalBounds()));
            setVisible (true);
            repaint();
        }
        void paint (juce::Graphics& g) override
        {
            if (isOpaque())
                g.fillAll (palette::background);
            g.setColour (palette::secondaryText);
            g.setFont (fontHeight);
            g.drawText (text, textBounds.translated (-getX(), -getY()), justification);
        }

    private:
        juce::String text;
        juce::Rectangle<int> textBounds;
        float fontHeight = 13.0f;
        juce::Justification justification { juce::Justification::centredLeft };
    };

    GraphArtwork::GraphArtwork()
    {
        setComponentID ("response.artwork");
        setInterceptsMouseClicks (false, false);
    }

    GraphArtwork::~GraphArtwork() = default;

    void GraphArtwork::resized() { updateLabelBounds(); }

    void GraphArtwork::updateLabelBounds()
    {
        size_t index = 0;
        const auto scaled = [this] (int value) { return juce::roundToInt (static_cast<float> (value) * uiScale); };
        const auto label = [&] (juce::String text, juce::Rectangle<int> area, float font, juce::Justification justification, bool opaque = true) {
            if (index == labelLayers.size())
            {
                labelLayers.push_back (std::make_unique<LabelLayer>());
                addChildComponent (*labelLayers.back());
            }
            labelLayers[index++]->setLabel (std::move (text), area, font * uiScale, justification, opaque);
        };
        const auto titleY = plot.getY() - scaled (30);
        const auto axisX = plot.getX() - scaled (axisLabelWidth + axisLabelGap);
        label ("Frequency response", { plot.getX(), titleY, scaled (200), scaled (24) }, 13.0f, juce::Justification::centredLeft);
        label ("dB", { axisX, titleY, scaled (axisLabelWidth), scaled (24) }, 13.0f, juce::Justification::centredRight);
        if (rate > 0.0)
            label (juce::String (rate / 1000.0, 1) + " kHz", { plot.getRight() - scaled (86), titleY, scaled (90), scaled (24) }, 13.0f, juce::Justification::centredRight);
        if (!plot.isEmpty())
        {
            if (showAnalyzer)
            {
                for (int db = static_cast<int> (analyzerDisplay::maximumDb); db >= static_cast<int> (analyzerDisplay::minimumDb); db -= 24)
                {
                    const auto proportion = (analyzerDisplay::maximumDb - static_cast<float> (db)) / (analyzerDisplay::maximumDb - analyzerDisplay::minimumDb);
                    const auto y = plot.getY() + static_cast<int> (proportion * static_cast<float> (plot.getHeight()));
                    label (juce::String (db), { plot.getRight() + scaled (2), y - scaled (7), scaled (22), scaled (14) }, 10.0f, juce::Justification::centredLeft);
                }
                label ("dBFS", { plot.getRight() - scaled (40), plot.getY() + scaled (3), scaled (36), scaled (12) }, 10.0f, juce::Justification::centred, false);
            }
            const auto font = showAnalyzer ? 10.0f : 13.0f;
            for (int db = responseMapping::minimumDb; db <= responseMapping::maximumDb; db += gridStepDb)
            {
                const auto y = plot.getY() + (responseMapping::maximumDb - db) * plot.getHeight() / responseMapping::spanDb;
                label (juce::String (db), { axisX, y - scaled (9), scaled (axisLabelWidth), scaled (18) }, font, juce::Justification::centredRight);
            }
            const auto maximum = rate > 0.0 ? responseMapping::maximumFrequency (rate) : bandLimits::maximumFrequencyHz;
            for (const auto frequency : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
            {
                if (frequency > maximum)
                    continue;
                const auto proportion = responseMapping::frequencyToProportion (frequency, maximum);
                const auto x = static_cast<int> (std::round (plot.getX() + proportion * plot.getWidth()));
                const auto text = frequency >= 1000.0 ? juce::String (frequency / 1000.0, 0) + "k" : juce::String (frequency, 0);
                label (text, { x - scaled (22), plot.getBottom() + scaled (5), scaled (44), scaled (20) }, font, juce::Justification::centred);
            }
        }
        for (; index < labelLayers.size(); ++index)
            labelLayers[index]->setVisible (false);
    }

    void GraphArtwork::setDisplay (juce::Rectangle<int> bounds, double sampleRate, bool analyzerVisible, float scale)
    {
        if (plot == bounds && juce::exactlyEqual (rate, sampleRate) && showAnalyzer == analyzerVisible && juce::exactlyEqual (uiScale, scale))
            return;
        uiScale = scale;
        plot = bounds;
        rate = sampleRate;
        showAnalyzer = analyzerVisible;
        updateLabelBounds();
        repaint();
    }

    void GraphArtwork::paint (juce::Graphics& g)
    {
        if (plot.isEmpty())
            return;
        g.setColour (palette::grid);
        for (int db = responseMapping::minimumDb; db <= responseMapping::maximumDb; db += gridStepDb)
        {
            if (db == 0)
                continue;
            const auto y = plot.getY() + (responseMapping::maximumDb - db) * plot.getHeight() / responseMapping::spanDb;
            g.drawHorizontalLine (y, static_cast<float> (plot.getX()), static_cast<float> (plot.getRight()));
        }
        g.setColour (palette::zeroLine);
        const auto zeroY = plot.getY() + responseMapping::maximumDb * plot.getHeight() / responseMapping::spanDb;
        g.drawHorizontalLine (zeroY, static_cast<float> (plot.getX()), static_cast<float> (plot.getRight()));
        g.setColour (palette::grid);
        const auto maximum = rate > 0.0 ? responseMapping::maximumFrequency (rate) : bandLimits::maximumFrequencyHz;
        for (const auto frequency : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
        {
            if (frequency > maximum)
                continue;
            const auto proportion = responseMapping::frequencyToProportion (frequency, maximum);
            const auto x = static_cast<int> (std::round (plot.getX() + proportion * plot.getWidth()));
            g.drawVerticalLine (x, static_cast<float> (plot.getY()), static_cast<float> (plot.getBottom()));
        }
    }

}
