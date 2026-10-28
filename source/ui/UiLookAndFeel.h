// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
namespace openq
{
    class UiLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        UiLookAndFeel();
        float scale = 1.0f;
        juce::Font getTextButtonFont (juce::TextButton&, int) override;
        juce::Font getComboBoxFont (juce::ComboBox&) override;
        juce::Font getPopupMenuFont() override;
        juce::Label* createSliderTextBox (juce::Slider&) override;
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
        void drawDrawableButton (juce::Graphics&, juce::DrawableButton&, bool, bool) override;
        void drawLabel (juce::Graphics&, juce::Label&) override;
        void fillTextEditorBackground (juce::Graphics&, int, int, juce::TextEditor&) override;
        void drawTextEditorOutline (juce::Graphics&, int, int, juce::TextEditor&) override;
        void drawRotarySlider (juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
        void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool, bool) override;
    };
}
