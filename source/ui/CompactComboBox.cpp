// SPDX-License-Identifier: AGPL-3.0-only
// Pattern reference: ZLEqualizer 3ce598ee065b3d3847121e76e196feda7ed8f56d,
// source/gui/combobox/compact_combobox/; Open-Q implementation by Yohan Senanayake.
#include "CompactComboBox.h"
#include "Palette.h"

namespace openq
{
    namespace
    {
        constexpr int popupMinimumWidth = 140, popupItemHeight = 26;
        constexpr float selectorFontSize = 12.0f;
    }

    CompactComboBox::LookAndFeel::LookAndFeel()
    {
        setColour (juce::ComboBox::textColourId, palette::text);
        setColour (juce::PopupMenu::backgroundColourId, palette::cardBackground);
        setColour (juce::PopupMenu::textColourId, palette::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, palette::selection);
        setColour (juce::PopupMenu::highlightedTextColourId, palette::text);
    }

    juce::Font CompactComboBox::LookAndFeel::getComboBoxFont (juce::ComboBox& box)
    {
        return juce::Font (juce::FontOptions (selectorFontSize * static_cast<float> (box.getHeight()) / 26.0f));
    }

    juce::Font CompactComboBox::LookAndFeel::getPopupMenuFont()
    {
        return juce::Font (juce::FontOptions (15.0f * scale));
    }

    void CompactComboBox::LookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool buttonDown, int, int, int, int, juce::ComboBox& box)
    {
        const auto s = static_cast<float> (height) / 26.0f;
        const auto bounds = juce::Rectangle<float> (static_cast<float> (width), static_cast<float> (height)).reduced (0.5f * s);
        const bool highlighted = box.isEnabled() && (buttonDown || box.isPopupActive() || box.isMouseOver (true) || box.hasKeyboardFocus (true));
        g.setColour (highlighted ? palette::controlHover : palette::background);
        g.fillRoundedRectangle (bounds, 4.0f * s);
        g.setColour (highlighted ? palette::accent : palette::controlBorder);
        g.drawRoundedRectangle (bounds, 4.0f * s, s);
        const auto centreY = static_cast<float> (height) * 0.5f;
        const auto arrowX = static_cast<float> (width) - 10.0f * s;
        juce::Path arrow;
        arrow.startNewSubPath (arrowX - 2.5f * s, centreY - 1.5f * s);
        arrow.lineTo (arrowX, centreY + 1.5f * s);
        arrow.lineTo (arrowX + 2.5f * s, centreY - 1.5f * s);
        g.strokePath (arrow, juce::PathStrokeType (1.2f * s));
    }

    void CompactComboBox::LookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
    {
        const auto s = static_cast<float> (box.getHeight()) / 26.0f;
        label.setBounds (juce::Rectangle<float> (3.0f * s, s, std::max (0.0f, static_cast<float> (box.getWidth()) - 16.0f * s), std::max (0.0f, static_cast<float> (box.getHeight()) - 2.0f * s)).toNearestInt());
        label.setFont (getComboBoxFont (box));
        label.setJustificationType (juce::Justification::centredLeft);
    }

    juce::PopupMenu::Options CompactComboBox::LookAndFeel::getOptionsForComboBoxPopupMenu (juce::ComboBox& box, juce::Label& label)
    {
        scale = static_cast<float> (box.getHeight()) / 26.0f;
        auto* editor = box.findParentComponentOfClass<juce::AudioProcessorEditor>();
        // Even an orphan must never fall back to JUCE's separate desktop-window path.
        return juce::LookAndFeel_V4::getOptionsForComboBoxPopupMenu (box, label)
            .withParentComponent (editor != nullptr ? static_cast<juce::Component*> (editor) : &box)
            .withPreferredPopupDirection (juce::PopupMenu::Options::PopupDirection::upwards)
            .withMinimumWidth (std::max (juce::roundToInt (popupMinimumWidth * scale), box.getWidth()))
            .withStandardItemHeight (juce::roundToInt (popupItemHeight * scale));
    }

    void CompactComboBox::LookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
    {
        g.fillAll (palette::cardBackground);
        g.setColour (palette::controlBorder);
        g.drawRect (0, 0, width, height);
    }

    void CompactComboBox::LookAndFeel::preparePopupMenuWindow (juce::Component& window)
    {
        popup = &window;
        window.setOpaque (true);
    }

    void CompactComboBox::LookAndFeel::releasePopup()
    {
        // Dismissal callbacks may outlive the selector's LookAndFeel during editor teardown.
        if (popup != nullptr)
            popup->setLookAndFeel (nullptr);
        popup = nullptr;
    }

    CompactComboBox::CompactComboBox()
    {
        setLookAndFeel (&lookAndFeel);
        setJustificationType (juce::Justification::centredLeft);
        setScrollWheelEnabled (false);
        setBufferedToImage (true);
    }

    CompactComboBox::~CompactComboBox()
    {
        closePopup();
        setLookAndFeel (nullptr);
    }

    void CompactComboBox::enablementChanged()
    {
        const bool enabled = isEnabled();
        setRepaintsOnMouseActivity (enabled);
        setMouseClickGrabsKeyboardFocus (enabled);
        if (!enabled)
            closePopup();
        repaint();
    }

    void CompactComboBox::closePopup()
    {
        hidePopup();
        lookAndFeel.releasePopup();
    }

    void CompactComboBox::showPopup()
    {
        if (findParentComponentOfClass<juce::AudioProcessorEditor>() == nullptr || !isEnabled()
            || juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr)
        {
            closePopup();
            return;
        }
        for (auto* ancestor = static_cast<juce::Component*> (this); ancestor != nullptr; ancestor = ancestor->getParentComponent())
            if (!ancestor->isVisible())
            {
                closePopup();
                return;
            }
        if (lookAndFeel.popup == nullptr)
            juce::ComboBox::showPopup();
    }
}
