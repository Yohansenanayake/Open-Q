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
        oversampling.setComponentID ("processing.oversampling");
        oversampling.setTitle ("Oversampling");
        oversampling.addItem (juce::String::fromUTF8 ("1×"), 1);
        oversampling.addItem (juce::String::fromUTF8 ("2×"), 2);
        oversampling.addItem (juce::String::fromUTF8 ("4×"), 3);
        addAndMakeVisible (oversampling);
        oversamplingLabel.setText ("Oversampling", juce::dontSendNotification);
        addAndMakeVisible (oversamplingLabel);
        addAndMakeVisible (inputGain);
        addAndMakeVisible (outputGain);
        inputLabel.setText ("Input", juce::dontSendNotification);
        outputLabel.setText ("Output", juce::dontSendNotification);
        addAndMakeVisible (inputLabel);
        addAndMakeVisible (outputLabel);
        setOpaque (true);
    }
    BottomBar::~BottomBar()
    {
        oversampling.closePopup();
        setLookAndFeel (nullptr);
    }
    void BottomBar::refreshOversampling (int requested, int active, bool pending)
    {
        if (requested != displayedOversampling)
        {
            displayedOversampling = requested;
            oversampling.setSelectedId (requested + 1, juce::dontSendNotification);
        }
        if (active == displayedActive && pending == displayedPending)
            return;
        displayedActive = active;
        displayedPending = pending;
        oversamplingLabel.setText (pending ? "OS pending" : "Oversampling", juce::dontSendNotification);
        oversampling.setTooltip (juce::String::fromUTF8 ("Processing quality; 1× has zero latency. Changes briefly fade audio and may restart host processing. Active: ")
                                 + juce::String (1 << active) + juce::String::fromUTF8 ("×. Saved in sessions; preserved on preset load."));
    }
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
        oversampling.closePopup();
        const auto scaled = [this] (float value) { return juce::roundToInt (value * lookAndFeel.scale); };
        auto area = getLocalBounds().reduced (scaled (8.0f), scaled (5.0f));
        inputLabel.setFont (juce::FontOptions (14.0f * lookAndFeel.scale));
        outputLabel.setFont (juce::FontOptions (14.0f * lookAndFeel.scale));
        inputLabel.setBounds (area.removeFromLeft (scaled (44.0f)));
        inputGain.setBounds (area.removeFromLeft (scaled (90.0f)));
        area.removeFromLeft (scaled (12.0f));
        analyzer.setBounds (area.removeFromLeft (scaled (100.0f)));
        area.removeFromLeft (scaled (12.0f));
        oversamplingLabel.setFont (juce::FontOptions (12.0f * lookAndFeel.scale));
        oversamplingLabel.setBounds (area.removeFromLeft (scaled (90.0f)));
        oversampling.setBounds (area.removeFromLeft (scaled (64.0f)));
        area.removeFromRight (scaled (12.0f));
        outputGain.setBounds (area.removeFromRight (scaled (90.0f)));
        outputLabel.setBounds (area.removeFromRight (scaled (54.0f)));
    }
}
