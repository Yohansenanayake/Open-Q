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

    GraphArtwork::GraphArtwork()
    {
        setComponentID ("response.artwork");
        setInterceptsMouseClicks (false, false);
        setBufferedToImage (true);
    }

    void GraphArtwork::setDisplay (juce::Rectangle<int> bounds, double sampleRate, bool analyzerVisible)
    {
        if (plot == bounds && juce::exactlyEqual (rate, sampleRate) && showAnalyzer == analyzerVisible)
            return;
        plot = bounds;
        rate = sampleRate;
        showAnalyzer = analyzerVisible;
        repaint();
    }

    void GraphArtwork::paint (juce::Graphics& g)
    {
        g.setColour (palette::secondaryText);
        g.setFont (13.0f);
        g.drawText ("Frequency response", 52, 6, 200, 24, juce::Justification::centredLeft);
        g.drawText ("dB", 2, 6, 42, 24, juce::Justification::centredRight);
        if (rate > 0.0)
            g.drawText (juce::String (rate / 1000.0, 1) + " kHz", getWidth() - 110, 6, 90, 24, juce::Justification::centredRight);
        if (plot.isEmpty())
            return;
        if (showAnalyzer)
        {
            g.setColour (palette::secondaryText);
            g.setFont (10.0f);
            // Analyzer levels have their own scale; the left scale remains EQ gain.
            for (int db = static_cast<int> (analyzerDisplay::maximumDb); db >= static_cast<int> (analyzerDisplay::minimumDb); db -= 24)
            {
                const auto proportion = (analyzerDisplay::maximumDb - static_cast<float> (db)) / (analyzerDisplay::maximumDb - analyzerDisplay::minimumDb);
                const auto y = plot.getY() + static_cast<int> (proportion * static_cast<float> (plot.getHeight()));
                g.drawText (juce::String (db), plot.getRight() + 2, y - 7, 22, 14, juce::Justification::centredLeft);
            }
            g.drawText ("dBFS", plot.getRight() - 40, plot.getY() + 3, 36, 12, juce::Justification::centred);
        }
        for (int db = responseMapping::minimumDb; db <= responseMapping::maximumDb; db += gridStepDb)
        {
            const auto y = plot.getY() + (responseMapping::maximumDb - db) * plot.getHeight() / responseMapping::spanDb;
            g.setColour (db == 0 ? palette::zeroLine : palette::grid);
            g.drawHorizontalLine (y, static_cast<float> (plot.getX()), static_cast<float> (plot.getRight()));
            g.setColour (palette::secondaryText);
            g.drawText (juce::String (db), 2, y - 9, 42, 18, juce::Justification::centredRight);
        }
        const auto maximum = rate > 0.0 ? responseMapping::maximumFrequency (rate) : bandLimits::maximumFrequencyHz;
        for (const auto frequency : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
        {
            if (frequency > maximum)
                continue;
            const auto proportion = responseMapping::frequencyToProportion (frequency, maximum);
            const auto x = static_cast<int> (std::round (plot.getX() + proportion * plot.getWidth()));
            g.setColour (palette::grid);
            g.drawVerticalLine (x, static_cast<float> (plot.getY()), static_cast<float> (plot.getBottom()));
            g.setColour (palette::secondaryText);
            const auto label = frequency >= 1000.0 ? juce::String (frequency / 1000.0, 0) + "k" : juce::String (frequency, 0);
            g.drawText (label, x - 22, plot.getBottom() + 5, 44, 20, juce::Justification::centred);
        }
    }
}
