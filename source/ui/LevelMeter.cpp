// SPDX-License-Identifier: AGPL-3.0-only
// Rendering/ballistics informed by Grisey 5e315049a981472fca31a84efd23778e1ff2d253;
// separate static artwork informed by ZLEqualizer 3ce598ee065b3d3847121e76e196feda7ed8f56d.
// Open-Q implementation; no reference source copied.
#include "LevelMeter.h"
#include "Palette.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
namespace openq
{
    class LevelMeter::Artwork final : public juce::Component
    {
    public:
        explicit Artwork (LevelMeter& owner) : meter (owner)
        {
            setOpaque (true);
            setBufferedToImage (true);
            setInterceptsMouseClicks (false, false);
        }
        void paint (juce::Graphics& g) override
        {
            g.fillAll (palette::background);
            g.setFont (juce::FontOptions (11.0f * meter.scale));
            g.setColour (palette::secondaryText);
            g.drawText (meter.name, getLocalBounds().withHeight (juce::roundToInt (15.0f * meter.scale)), juce::Justification::centred);
            for (int channel = 0; channel < meter.channelCount; ++channel)
            {
                const auto bar = meter.bars[static_cast<size_t> (channel)];
                g.setColour (palette::grid);
                g.fillRect (bar);
                g.setColour (palette::secondaryText);
                g.drawText (meter.channelCount == 1 ? "M" : channel == 0 ? "L"
                                                                         : "R",
                    bar.withY (bar.getBottom() + 3.0f * meter.scale).withHeight (12.0f * meter.scale),
                    juce::Justification::centred);
            }
            g.setFont (juce::FontOptions (9.0f * meter.scale));
            for (int db = 0; db >= static_cast<int> (minimumDb); db -= 12)
            {
                const auto y = meter.yOf (static_cast<float> (db));
                g.setColour (db == 0 ? palette::meterOver : palette::secondaryText);
                g.drawText (juce::String (db), juce::Rectangle<float> (29.0f * meter.scale, y - 6.0f * meter.scale, 23.0f * meter.scale, 12.0f * meter.scale), juce::Justification::centredLeft);
            }
        }

    private:
        LevelMeter& meter;
    };
    class LevelMeter::Display final : public juce::Component
    {
    public:
        explicit Display (LevelMeter& owner) : meter (owner) { setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics& g) override
        {
            for (int channel = 0; channel < meter.channelCount; ++channel)
            {
                const auto index = static_cast<size_t> (channel);
                const auto bar = meter.bars[index];
                if (meter.levels[index].peak > minimumDb)
                {
                    g.setGradientFill (meter.gradient);
                    g.fillRect (bar.withTop (meter.yOf (meter.levels[index].peak)));
                }
                if (meter.levels[index].tick > minimumDb)
                {
                    g.setColour (meter.levels[index].tick > 0.0f ? palette::meterOver : palette::text);
                    g.fillRect (bar.withY (meter.yOf (meter.levels[index].tick)).withHeight (2.0f * meter.scale));
                }
            }
            const auto maximum = std::max (meter.levels[0].maximum, meter.levels[1].maximum);
            g.setColour (maximum > 0.0f ? palette::meterOver : palette::text);
            g.setFont (juce::FontOptions (10.0f * meter.scale));
            g.drawText (meter.readout, meter.readoutArea, juce::Justification::centred);
        }

    private:
        LevelMeter& meter;
    };
    LevelMeter::LevelMeter (const juce::String& label) : name (label)
    {
        setOpaque (true);
        setTitle (label + " sample peak meter");
        artwork = std::make_unique<Artwork> (*this);
        display = std::make_unique<Display> (*this);
        addAndMakeVisible (*artwork);
        addAndMakeVisible (*display);
    }
    LevelMeter::~LevelMeter() = default;
    float LevelMeter::yOf (float db) const
    {
        return static_cast<float> (meterArea.getBottom()) - (std::clamp (db, minimumDb, maximumDb) - minimumDb) / (maximumDb - minimumDb) * static_cast<float> (meterArea.getHeight());
    }
    std::array<int, 4> LevelMeter::positions() const
    {
        return { juce::roundToInt (yOf (levels[0].peak)), juce::roundToInt (yOf (levels[0].tick)), juce::roundToInt (yOf (levels[1].peak)), juce::roundToInt (yOf (levels[1].tick)) };
    }
    void LevelMeter::resized()
    {
        scale = static_cast<float> (getWidth()) / preferredWidth;
        readoutArea = { 0, juce::roundToInt (15.0f * scale), getWidth(), juce::roundToInt (15.0f * scale) };
        meterArea = getLocalBounds().withTrimmedTop (juce::roundToInt (35.0f * scale)).withTrimmedBottom (juce::roundToInt (20.0f * scale));
        bars[0] = { 6.0f * scale, static_cast<float> (meterArea.getY()), 8.0f * scale, static_cast<float> (meterArea.getHeight()) };
        bars[1] = bars[0].translated (12.0f * scale, 0.0f);
        if (channelCount == 1)
            bars[0] = bars[0].translated (6.0f * scale, 0.0f);
        gradient = juce::ColourGradient (palette::meterLow, 0.0f, static_cast<float> (meterArea.getBottom()), palette::meterOver, 0.0f, static_cast<float> (meterArea.getY()), false);
        gradient.addColour ((-6.0f - minimumDb) / (maximumDb - minimumDb), palette::meterWarning);
        gradient.addColour ((0.0f - minimumDb) / (maximumDb - minimumDb), palette::meterOver);
        artwork->setBounds (getLocalBounds());
        artwork->repaint();
        display->setBounds (getLocalBounds());
        display->repaint();
    }
    void LevelMeter::reset()
    {
        levels = {};
        readout = "-inf";
        readoutElapsed = 0.0;
        display->repaint();
    }
    void LevelMeter::mouseDown (const juce::MouseEvent&)
    {
        for (auto& channel : levels)
        {
            channel.maximum = channel.tick = silenceDb;
            channel.heldSeconds = 0.0;
        }
        readout = "-inf";
        display->repaint();
    }
    void LevelMeter::update (const std::array<float, 2>& peaks, int count, double seconds)
    {
        count = std::clamp (count, 1, 2);
        if (channelCount != count)
        {
            channelCount = count;
            reset();
            resized();
        }
        const auto before = positions();
        seconds = std::isfinite (seconds) ? std::max (0.0, seconds) : 0.0;
        for (size_t channel = 0; channel < levels.size(); ++channel)
        {
            auto& shown = levels[channel];
            const auto db = channel < static_cast<size_t> (count) && std::isfinite (peaks[channel]) ? juce::Decibels::gainToDecibels (std::max (0.0f, peaks[channel]), silenceDb) : silenceDb;
            shown.peak = std::max (db, shown.peak - releaseDbPerSecond * static_cast<float> (seconds));
            shown.maximum = std::max (shown.maximum, db);
            if (db >= shown.tick)
            {
                shown.tick = db;
                shown.heldSeconds = 0.0;
            }
            else
            {
                const auto previous = shown.heldSeconds;
                shown.heldSeconds += seconds;
                const auto decayTime = std::max (0.0, shown.heldSeconds - holdSeconds) - std::max (0.0, previous - holdSeconds);
                shown.tick = std::max (silenceDb, shown.tick - tickReleaseDbPerSecond * static_cast<float> (decayTime));
            }
        }
        readoutElapsed += seconds;
        if (readoutElapsed >= readoutSeconds)
        {
            readoutElapsed = 0.0;
            const auto maximum = std::max (levels[0].maximum, levels[1].maximum);
            const auto text = maximum <= minimumDb - 40.0f ? juce::String ("-inf") : juce::String (maximum, 1);
            if (text != readout)
            {
                readout = text;
                display->repaint (readoutArea);
            }
        }
        if (before != positions())
            display->repaint (meterArea.expanded (0, juce::roundToInt (2.0f * scale)));
    }
}
