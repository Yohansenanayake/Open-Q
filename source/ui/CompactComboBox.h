// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace openq
{
    // ZLE-style compact presentation and editor-parented menus, using JUCE's normal input handling.
    class CompactComboBox final : public juce::ComboBox
    {
    public:
        CompactComboBox();
        ~CompactComboBox() override;
        void showPopup() override;
        void closePopup();

    private:
        struct LookAndFeel final : juce::LookAndFeel_V4
        {
            LookAndFeel();
            juce::Font getComboBoxFont (juce::ComboBox&) override;
            void drawComboBox (juce::Graphics&, int width, int height, bool buttonDown, int, int, int, int, juce::ComboBox&) override;
            void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
            juce::PopupMenu::Options getOptionsForComboBoxPopupMenu (juce::ComboBox&, juce::Label&) override;
            void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
            void preparePopupMenuWindow (juce::Component&) override;
            void releasePopup();
            juce::Component::SafePointer<juce::Component> popup;
        };
        LookAndFeel lookAndFeel;
        void enablementChanged() override;
    };
}
