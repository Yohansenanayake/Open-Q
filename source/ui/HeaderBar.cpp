// SPDX-License-Identifier: AGPL-3.0-only
#include "HeaderBar.h"
#include "BinaryData.h"
#include "Palette.h"
namespace openq
{
    HeaderBar::Branding::Branding()
        : logo (juce::Drawable::createFromImageData (BinaryData::OpenQ_Logo_White_svg, BinaryData::OpenQ_Logo_White_svgSize))
    {
        jassert (logo != nullptr);
        setComponentID ("header.branding");
        setTitle ("Open-Q");
        setInterceptsMouseClicks (false, false);
        // Cache only this small static plaque, never the transparent header or graph.
        setBufferedToImage (true);
    }

    void HeaderBar::Branding::paint (juce::Graphics& g)
    {
        const auto scale = static_cast<float> (getWidth()) / width;
        g.addTransform (juce::AffineTransform::scale (scale));
        juce::Path plaque;
        plaque.startNewSubPath (0.0f, 0.0f);
        plaque.cubicTo (leftShoulder * 0.5f, 0.0f, leftShoulder * 0.5f, height, leftShoulder, height);
        plaque.lineTo (width - rightShoulder, height);
        plaque.cubicTo (width - rightShoulder * 0.5f, height, width - rightShoulder * 0.5f, 0.0f, width, 0.0f);
        plaque.closeSubPath();
        g.setGradientFill (juce::ColourGradient (palette::brandingTop, 0.0f, 0.0f, palette::brandingBottom, 0.0f, height, false));
        g.fillPath (plaque);
        if (logo != nullptr)
            logo->drawWithin (g, { logoInset, logoVerticalPadding, width - logoInset - rightShoulder, height - 2.0f * logoVerticalPadding }, juce::RectanglePlacement::centred, 1.0f);
    }

    HeaderBar::HeaderBar (AnalyzerTransport& source, juce::AudioProcessorValueTreeState& parameters)
        : transport (source), bypass ("Total Bypass", BinaryData::bypass_svg, BinaryData::bypass_svgSize)
    {
        setComponentID ("editor.header");
        setLookAndFeel (&lookAndFeel);
        setInterceptsMouseClicks (false, true);
        addAndMakeVisible (branding);
        for (auto* button : { &pre, &post, &fill })
            addAndMakeVisible (*button);
        // addAndMakeVisible (inspect);
        presets.setComponentID ("preset.open");
        presets.setTooltip ("Default (last loaded or saved preset). Click to browse presets.");
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
        // inspect.onClick = [this] { if (onInspect) onInspect(); };
    }
    HeaderBar::~HeaderBar() { setLookAndFeel (nullptr); }
    void HeaderBar::setPresetName (const juce::String& name)
    {
        if (presets.getButtonText() == name)
            return;
        presets.setButtonText (name);
        presets.setTitle ("Presets: " + name);
        presets.setTooltip (name + " (last loaded or saved preset). Click to browse presets.");
    }
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
        g.setGradientFill (juce::ColourGradient (palette::headerBackground, 0.0f, 0.0f, palette::background.withAlpha (0.0f), 0.0f, static_cast<float> (getHeight()), false));
        g.fillRect (getLocalBounds());
    }
    void HeaderBar::resized()
    {
        const auto scaled = [this] (int v) { return juce::roundToInt (static_cast<float> (v) * lookAndFeel.scale); };
        branding.setBounds (0, 0, scaled (Branding::width), scaled (Branding::height));
        auto area = getLocalBounds().reduced (scaled (horizontalPadding), scaled (6));
        bypass.setBounds (area.removeFromRight (scaled (28)).withSizeKeepingCentre (scaled (26), scaled (26)));
        area.removeFromRight (scaled (8));
        // inspect.setBounds (area.removeFromRight (scaled (130)).reduced (0, scaled (6)));
        // area.removeFromRight (scaled (8));
        post.setBounds (area.removeFromRight (scaled (54)));
        pre.setBounds (area.removeFromRight (scaled (48)));
        fill.setBounds (area.removeFromRight (scaled (48)));
        area.setLeft (branding.getRight());
        area.reduce (scaled (8), 0);
        const auto nameWidth = std::min (scaled (180), area.getWidth());
        const auto nameX = std::clamp ((getWidth() - nameWidth) / 2, area.getX(), area.getRight() - nameWidth);
        presets.setBounds (nameX, area.getCentreY() - scaled (13), nameWidth, scaled (26));
    }
}
