// SPDX-License-Identifier: AGPL-3.0-only
#include "BottomBar.h"
#include "Palette.h"
namespace openq
{
    BottomBar::BottomBar (juce::AudioProcessorValueTreeState& parameters)
        : inputGain (parameters, gainParameterIds[0]), outputGain (parameters, gainParameterIds[1])
    {
        setComponentID ("editor.bottom");
        setLookAndFeel (&lookAndFeel);
        analyzer.setComponentID ("analyzer.settings");
        analyzer.setClickingTogglesState (true);
        addAndMakeVisible (analyzer);
        addAndMakeVisible (inputGain);
        addAndMakeVisible (outputGain);
        inputLabel.setText ("Input", juce::dontSendNotification);
        outputLabel.setText ("Output", juce::dontSendNotification);
        addAndMakeVisible (inputLabel);
        addAndMakeVisible (outputLabel);
        setOpaque (true);
    }
    BottomBar::~BottomBar() { setLookAndFeel (nullptr); }
    void BottomBar::paint (juce::Graphics& g)
    {
        g.setGradientFill (juce::ColourGradient (palette::bottomBarTop, 0.0f, 0.0f, palette::bottomBarBackground, 0.0f, static_cast<float> (getHeight()), false));
        g.fillAll();
    }
    void BottomBar::setUiScale (float scale)
    {
        if (juce::exactlyEqual (lookAndFeel.scale, scale))
            return;
        lookAndFeel.scale = scale;
        sendLookAndFeelChange();
        resized();
        repaint();
    }
    void BottomBar::messageThreadTick (double seconds)
    {
        inputGain.messageThreadTick (seconds);
        outputGain.messageThreadTick (seconds);
    }
    void BottomBar::resized()
    {
        const auto scaled = [this] (float value) { return juce::roundToInt (value * lookAndFeel.scale); };
        auto area = getLocalBounds().reduced (scaled (8.0f), scaled (5.0f));
        inputLabel.setFont (juce::FontOptions (14.0f * lookAndFeel.scale));
        outputLabel.setFont (juce::FontOptions (14.0f * lookAndFeel.scale));
        inputLabel.setBounds (area.removeFromLeft (scaled (44.0f)));
        inputGain.setBounds (area.removeFromLeft (scaled (90.0f)));
        area.removeFromLeft (scaled (12.0f));
        analyzer.setBounds (area.removeFromLeft (scaled (100.0f)));
        area.removeFromRight (scaled (12.0f));
        outputGain.setBounds (area.removeFromRight (scaled (90.0f)));
        outputLabel.setBounds (area.removeFromRight (scaled (54.0f)));
    }
}
