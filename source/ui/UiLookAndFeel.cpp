// SPDX-License-Identifier: AGPL-3.0-only
#include "UiLookAndFeel.h"
#include "Palette.h"
namespace openq
{
    UiLookAndFeel::UiLookAndFeel()
        : juce::LookAndFeel_V4 (ColourScheme {
              palette::background, palette::controlBackground, palette::cardBackground,
              palette::controlBorder, palette::text, palette::accent,
              palette::text, palette::selection, palette::text })
    {
        setColour (juce::Slider::rotarySliderFillColourId, palette::accent);
        setColour (juce::Slider::rotarySliderOutlineColourId, palette::knobTrack);
        setColour (juce::Slider::textBoxBackgroundColourId, palette::background);
        setColour (juce::Slider::textBoxHighlightColourId, palette::selection);
        setColour (juce::TextEditor::highlightColourId, palette::selection);
        setColour (juce::TooltipWindow::backgroundColourId, palette::cardBackground);
    }
    juce::Font UiLookAndFeel::getTextButtonFont (juce::TextButton&, int)
    {
        return juce::Font (juce::FontOptions (16.0f * scale));
    }
    juce::Font UiLookAndFeel::getComboBoxFont (juce::ComboBox&)
    {
        return juce::Font (juce::FontOptions (15.0f * scale));
    }
    juce::Font UiLookAndFeel::getPopupMenuFont()
    {
        return juce::Font (juce::FontOptions (15.0f * scale));
    }
    juce::Label* UiLookAndFeel::createSliderTextBox (juce::Slider& slider)
    {
        auto* label = juce::LookAndFeel_V4::createSliderTextBox (slider);
        label->setFont (juce::FontOptions (14.0f * scale));
        return label;
    }
    void UiLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float position, float start, float end, juce::Slider& slider)
    {
        juce::Graphics::ScopedSaveState save (g);
        g.addTransform (juce::AffineTransform::scale (scale));
        // Scale JUCE's capped stroke/padding metrics along with the knob bounds.
        juce::LookAndFeel_V4::drawRotarySlider (g, juce::roundToInt (x / scale), juce::roundToInt (y / scale), juce::roundToInt (width / scale), juce::roundToInt (height / scale), position, start, end, slider);
    }
    void UiLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool down)
    {
        const auto tickSize = 18.0f * scale;
        const auto y = (static_cast<float> (button.getHeight()) - tickSize) * 0.5f;
        drawTickBox (g, button, 2.0f * scale, y, tickSize, tickSize, button.getToggleState(), button.isEnabled(), highlighted, down);
        g.setColour (button.findColour (juce::ToggleButton::textColourId).withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f));
        g.setFont (15.0f * scale);
        g.drawText (button.getButtonText(), button.getLocalBounds().withTrimmedLeft (juce::roundToInt (24.0f * scale)), juce::Justification::centredLeft);
    }
}
