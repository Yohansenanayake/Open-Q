// SPDX-License-Identifier: AGPL-3.0-only
#include "ResponseGraph.h"

namespace openq
{
    ResponseGraph::ResponseGraph (juce::AudioProcessorValueTreeState& parameters, const std::atomic<double>& sampleRate) : worker (parameters, sampleRate)
    {
        setTitle ("Frequency response");
        setDescription ("Combined digital EQ response of all enabled bands, including when playback is stopped.");
        setOpaque (true);
        startTimer (250);
    }

    ResponseGraph::~ResponseGraph()
    {
        stopTimer();
        vblank.reset();
        worker.stop();
    }

    juce::Rectangle<int> ResponseGraph::plotBounds() const
    {
        return getLocalBounds().withTrimmedLeft (52).withTrimmedRight (24).withTrimmedTop (36).withTrimmedBottom (32);
    }

    void ResponseGraph::resized()
    {
        const auto bounds = plotBounds();
        worker.setGeometry ({ std::max (0, bounds.getWidth()), std::max (0, bounds.getHeight()) });
    }

    void ResponseGraph::visibilityChanged() { updateVisibility(); }
    void ResponseGraph::parentHierarchyChanged() { updateVisibility(); }
    void ResponseGraph::timerCallback() { updateVisibility(); }

    void ResponseGraph::updateVisibility()
    {
        if (isShowing() && vblank == nullptr)
        {
            if (worker.start())
            {
                vblank = std::make_unique<juce::VBlankAttachment> (this, [this] (double) { repaint(); });
                worker.requestUpdate();
                repaint();
            }
        }
        else if (!isShowing() && vblank != nullptr)
        {
            vblank.reset();
            worker.stop();
        }
    }

    void ResponseGraph::paintOverChildren (juce::Graphics&)
    {
        worker.requestUpdate();
    }

    void ResponseGraph::paint (juce::Graphics& g)
    {
        worker.pullFrame();
        const auto& frame = worker.getFrame();
        const auto plot = plotBounds();
        g.fillAll (juce::Colour (0xff161b23));
        g.setColour (juce::Colour (0xffb4bfce));
        g.setFont (13.0f);
        g.drawText ("Frequency response", 52, 6, 200, 24, juce::Justification::centredLeft);
        g.drawText ("dB", 2, 6, 42, 24, juce::Justification::centredRight);
        if (frame.valid)
            g.drawText (juce::String (frame.response.sampleRate / 1000.0, 1) + " kHz", getWidth() - 110, 6, 90, 24, juce::Justification::centredRight);
        if (plot.isEmpty())
            return;

        for (int db = -24; db <= 24; db += 6)
        {
            const auto y = plot.getY() + (24 - db) * plot.getHeight() / 48;
            g.setColour (juce::Colour (db == 0 ? 0xff566375 : 0xff2b3441));
            g.drawHorizontalLine (y, static_cast<float> (plot.getX()), static_cast<float> (plot.getRight()));
            g.setColour (juce::Colour (0xffb4bfce));
            g.drawText (juce::String (db), 2, y - 9, 42, 18, juce::Justification::centredRight);
        }
        const auto maximum = frame.valid ? std::min (20000.0, frame.response.sampleRate * 0.5) : 20000.0;
        for (const auto frequency : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
        {
            if (frequency > maximum)
                continue;
            const auto proportion = std::log (frequency / 20.0) / std::log (maximum / 20.0);
            const auto x = static_cast<int> (std::round (plot.getX() + proportion * plot.getWidth()));
            g.setColour (juce::Colour (0xff2b3441));
            g.drawVerticalLine (x, static_cast<float> (plot.getY()), static_cast<float> (plot.getBottom()));
            g.setColour (juce::Colour (0xffb4bfce));
            const auto label = frequency >= 1000.0 ? juce::String (frequency / 1000.0, 0) + "k" : juce::String (frequency, 0);
            g.drawText (label, x - 22, plot.getBottom() + 5, 44, 20, juce::Justification::centred);
        }
        if (!frame.valid)
        {
            g.setColour (juce::Colour (0xffb4bfce));
            g.drawText ("Waiting for audio preparation", plot, juce::Justification::centred);
        }
        else if (frame.geometry == ResponseGeometry { plot.getWidth(), plot.getHeight() })
        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (plot.expanded (1));
            g.setColour (juce::Colour (0xff6edbd4));
            g.strokePath (frame.curve, juce::PathStrokeType (2.0f), juce::AffineTransform::translation (static_cast<float> (plot.getX()), static_cast<float> (plot.getY())));
        }
    }
}
