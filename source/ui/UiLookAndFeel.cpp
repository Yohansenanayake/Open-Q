// SPDX-License-Identifier: AGPL-3.0-only
#include "UiLookAndFeel.h"
#include "Palette.h"
#include "ControlDrawing.h"
namespace openq
{
    namespace
    {
        bool isSliderValue (juce::Component& component)
        {
            const auto* slider = dynamic_cast<juce::Slider*> (component.getParentComponent());
            return slider != nullptr && slider->getTextBoxPosition() != juce::Slider::NoTextBox;
        }
        bool isSliderValueEditor (juce::TextEditor& editor)
        {
            const auto* label = dynamic_cast<juce::Label*> (editor.getParentComponent());
            return label != nullptr && isSliderValue (*editor.getParentComponent());
        }
    }
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
        label->setJustificationType (juce::Justification::centredRight);
        label->setBorderSize (juce::BorderSize<int> (0, juce::roundToInt (controlStyle::textPadding * scale), 0, juce::roundToInt (controlStyle::textPadding * scale)));
        label->setRepaintsOnMouseActivity (true);
        label->onEditorShow = [label] {
            if (auto* editor = label->getCurrentTextEditor())
                editor->setOpaque (false);
        };
        return label;
    }
    void UiLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&, bool highlighted, bool down)
    {
        drawControlBackground (g, button.getLocalBounds().toFloat(), scale, button.isEnabled(), highlighted || down,
            button.hasKeyboardFocus (true), button.getToggleState() || down);
    }
    void UiLookAndFeel::drawDrawableButton (juce::Graphics& g, juce::DrawableButton& button, bool highlighted, bool down)
    {
        juce::LookAndFeel_V4::drawDrawableButton (g, button, highlighted, down);
        if (button.isEnabled() && (highlighted || down || button.hasKeyboardFocus (true)))
        {
            g.setColour (palette::controlHoverBorder);
            g.drawRoundedRectangle (button.getLocalBounds().toFloat().reduced (controlStyle::borderWidth * scale * 0.5f),
                controlStyle::cornerRadius * scale, controlStyle::borderWidth * scale);
        }
    }
    void UiLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
    {
        if (!isSliderValue (label))
        {
            juce::LookAndFeel_V4::drawLabel (g, label);
            return;
        }
        drawControlBackground (g, label.getLocalBounds().toFloat(), scale, label.isEnabled(), label.isMouseOver (true), label.hasKeyboardFocus (true));
        if (!label.isBeingEdited())
        {
            g.setColour (label.findColour (juce::Label::textColourId).withMultipliedAlpha (label.isEnabled() ? 1.0f : controlStyle::disabledOpacity));
            g.setFont (getLabelFont (label));
            g.drawFittedText (label.getText(), label.getBorderSize().subtractedFrom (label.getLocalBounds()), label.getJustificationType(), 1, label.getMinimumHorizontalScale());
        }
    }
    void UiLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
    {
        if (isSliderValueEditor (editor))
            drawControlBackground (g, editor.getLocalBounds().toFloat(), scale, editor.isEnabled(), false, editor.hasKeyboardFocus (true));
        else
            juce::LookAndFeel_V4::fillTextEditorBackground (g, width, height, editor);
    }
    void UiLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
    {
        if (!isSliderValueEditor (editor))
            juce::LookAndFeel_V4::drawTextEditorOutline (g, width, height, editor);
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
