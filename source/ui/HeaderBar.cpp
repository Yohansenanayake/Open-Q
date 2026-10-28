// SPDX-License-Identifier: AGPL-3.0-only
#include "HeaderBar.h"
#include "Palette.h"
#include "BinaryData.h"
namespace openq
{
    HeaderBar::HeaderBar (AnalyzerTransport& source, juce::AudioProcessorValueTreeState& parameters)
        : transport (source), bypass ("Total Bypass", BinaryData::bypass_svg, BinaryData::bypass_svgSize)
    {
        setComponentID ("editor.header");
        setLookAndFeel (&lookAndFeel);
        setInterceptsMouseClicks (false, true);
        for (auto* button : { &pre, &post, &fill })
            addAndMakeVisible (*button);
        addAndMakeVisible (inspect);
        presets.setComponentID ("preset.open");
        presets.setClickingTogglesState (true);
        presets.onClick = [this] { if (onPresets) onPresets(); };
        addAndMakeVisible (presets);
        bypass.setComponentID (bypassParameterId);
        bypass.setClickingTogglesState (true);
        bypass.setTooltip ("Total bypass, including Input and Output gain. Lit means bypassed; the EQ keeps processing warm.");
        addAndMakeVisible (bypass);
        bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (parameters, bypassParameterId, bypass);
        pre.setComponentID ("analyzer.pre");
        post.setComponentID ("analyzer.post");
        fill.setComponentID ("analyzer.fill");
        pre.setColour (juce::ToggleButton::tickColourId, palette::analyzerPre);
        post.setColour (juce::ToggleButton::tickColourId, palette::analyzerPost);
        pre.setTooltip ("Show the input spectrum (averaged channel power).");
        post.setTooltip ("Show the output spectrum (averaged channel power).");
        fill.setTooltip ("Fill the spectra; turn off to show outlines only.");
        pre.onClick = [this] {
            auto settings = transport.getSettings();
            settings.pre = pre.getToggleState();
            transport.setSettings (settings);
        };
        post.onClick = [this] {
            auto settings = transport.getSettings();
            settings.post = post.getToggleState();
            transport.setSettings (settings);
        };
        fill.onClick = [this] {
            transport.setFillEnabled (fill.getToggleState());
            if (onFillChanged)
                onFillChanged();
        };
        inspect.onClick = [this] { if (onInspect) onInspect(); };
    }
    HeaderBar::~HeaderBar() { setLookAndFeel (nullptr); }
    void HeaderBar::refresh()
    {
        const auto settings = transport.getSettings();
        pre.setToggleState (settings.pre, juce::dontSendNotification);
        post.setToggleState (settings.post, juce::dontSendNotification);
        fill.setToggleState (transport.getFillEnabled(), juce::dontSendNotification);
    }
    void HeaderBar::setUiScale (float scale)
    {
        if (juce::exactlyEqual (lookAndFeel.scale, scale))
            return;
        lookAndFeel.scale = scale;
        sendLookAndFeelChange();
        resized();
        repaint();
    }
    void HeaderBar::paint (juce::Graphics& g)
    {
        g.setGradientFill (juce::ColourGradient (palette::background, 0.0f, 0.0f, palette::background.withAlpha (0.0f), 0.0f, static_cast<float> (getHeight()), false));
        g.fillRect (getLocalBounds());
        g.setColour (palette::text);
        g.setFont (24.0f * lookAndFeel.scale);
        g.drawText ("Open-Q", getLocalBounds().reduced (juce::roundToInt (12.0f * lookAndFeel.scale), 0).withWidth (juce::roundToInt (120.0f * lookAndFeel.scale)), juce::Justification::centredLeft);
    }
    void HeaderBar::resized()
    {
        const auto scaled = [this] (int v) { return juce::roundToInt (static_cast<float> (v) * lookAndFeel.scale); };
        auto area = getLocalBounds().reduced (scaled (12), scaled (6));
        bypass.setBounds (area.removeFromRight (scaled (28)).withSizeKeepingCentre (scaled (26), scaled (26)));
        area.removeFromRight (scaled (8));
        inspect.setBounds (area.removeFromRight (scaled (130)).reduced (0, scaled (6)));
        area.removeFromRight (scaled (8));
        post.setBounds (area.removeFromRight (scaled (66)));
        pre.setBounds (area.removeFromRight (scaled (60)));
        fill.setBounds (area.removeFromRight (scaled (60)));
        area.removeFromLeft (scaled (120));
        presets.setBounds (area.removeFromLeft (scaled (110)).reduced (0, scaled (6)));
    }
}
