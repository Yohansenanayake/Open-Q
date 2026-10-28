// SPDX-License-Identifier: AGPL-3.0-only
#include "UiLookAndFeel.h"
#include "Palette.h"
#include "ControlDrawing.h"
#include "RotaryStyle.h"
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
        label->setJustificationType (juce::Justification::centred);
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
        const auto bounds = juce::Rectangle<float> (static_cast<float> (x), static_cast<float> (y), static_cast<float> (width), static_cast<float> (height));
        const auto radius = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f - rotaryStyle::padding * scale;
        const auto trackWidth = rotaryStyle::trackWidth * scale;
        const auto faceRadius = radius - trackWidth - rotaryStyle::faceGap * scale;
        if (faceRadius <= 0.0f)
            return;
        const auto centre = bounds.getCentre();
        const auto trackRadius = radius - trackWidth * 0.5f;
        const auto angle = start + juce::jlimit (0.0f, 1.0f, position) * (end - start);
        const auto enabled = slider.isEnabled();
        const auto opacity = enabled ? 1.0f : controlStyle::disabledOpacity;
        const auto highlighted = enabled && (slider.isMouseOver (false) || slider.isMouseButtonDown() || slider.hasKeyboardFocus (true));
        const juce::PathStrokeType stroke (trackWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, trackRadius, trackRadius, 0.0f, start, end, true);
        g.setColour (slider.findColour (juce::Slider::rotarySliderOutlineColourId).withMultipliedAlpha (opacity));
        g.strokePath (track, stroke);
        if (enabled && position > 0.0f)
        {
            juce::Path value;
            value.addCentredArc (centre.x, centre.y, trackRadius, trackRadius, 0.0f, start, angle, true);
            g.setColour (slider.findColour (juce::Slider::rotarySliderFillColourId));
            g.strokePath (value, stroke);
        }
        const auto face = juce::Rectangle<float> (faceRadius * 2.0f, faceRadius * 2.0f).withCentre (centre);
        g.setColour (palette::knobFace.withMultipliedAlpha (opacity));
        g.fillEllipse (face);
        g.setColour ((highlighted ? palette::knobRimHover : palette::knobRim).withMultipliedAlpha (opacity));
        g.drawEllipse (face, rotaryStyle::rimWidth * scale);
        const juce::Point<float> direction { std::sin (angle), -std::cos (angle) };
        g.setColour (palette::knobIndicator.withMultipliedAlpha (opacity));
        g.drawLine ({ centre + direction * (faceRadius * rotaryStyle::indicatorInnerRadiusRatio), centre + direction * (faceRadius * rotaryStyle::indicatorOuterRadiusRatio) }, rotaryStyle::indicatorWidth * scale);
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
