// Copyright (C) 2026 - zsliu98
// This file is part of ZLEqualizer
//
// ZLEqualizer is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// ZLEqualizer is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with ZLEqualizer. If not, see <https://www.gnu.org/licenses/>.
// Open-Q adaptation: Yohan Senanayake, 2026-10-28; pinned origin in THIRD_PARTY_NOTICES.md.

#include "warning_overlay.hpp"
namespace openq::presets
{
    WarningOverlay::WarningOverlay (PresetStyle& style) : style_ (style)
    {
        setOpaque (true);
        setWantsKeyboardFocus (true);
        setComponentID ("preset.warning");
        cancel_.setComponentID ("preset.cancel");
        confirm_.setComponentID ("preset.confirm");
        for (juce::Component* child : std::array<juce::Component*, 3> { &message_, &cancel_, &confirm_ })
            addAndMakeVisible (child);
        message_.setColour (juce::Label::textColourId, palette::text);
        cancel_.onClick = [this] { hide(); };
        confirm_.onClick = [this] { auto action = std::move(action_); hide(); if (action) action(); };
    }
    void WarningOverlay::paint (juce::Graphics& g) { g.fillAll (palette::cardBackground); }
    void WarningOverlay::resized()
    {
        auto area = getLocalBounds().reduced (juce::roundToInt (20.0f * style_.scale));
        auto buttons = area.removeFromBottom (juce::roundToInt (32.0f * style_.scale));
        confirm_.setBounds (buttons.removeFromRight (juce::roundToInt (110.0f * style_.scale)));
        buttons.removeFromRight (juce::roundToInt (8.0f * style_.scale));
        cancel_.setBounds (buttons.removeFromRight (juce::roundToInt (110.0f * style_.scale)));
        message_.setBounds (area);
        message_.setFont (juce::FontOptions (16.0f * style_.scale));
    }
    bool WarningOverlay::keyPressed (const juce::KeyPress& key)
    {
        if (key == juce::KeyPress::escapeKey)
        {
            hide();
            return true;
        }
        if (key == juce::KeyPress::returnKey)
        {
            confirm_.onClick();
            return true;
        }
        return false;
    }
    void WarningOverlay::show (const juce::String& message, const juce::String& action, bool destructive, std::function<void()> callback)
    {
        action_ = std::move (callback);
        message_.setText (message, juce::dontSendNotification);
        confirm_.setButtonText (action);
        confirm_.setColour (juce::TextButton::textColourOffId, destructive ? palette::meterOver : palette::text);
        cancel_.setVisible (static_cast<bool> (action_));
        setVisible (true);
        toFront (false);
        if (getPeer() != nullptr)
            grabKeyboardFocus();
    }
    void WarningOverlay::showMessage (const juce::String& message) { show (message, "OK", false, {}); }
    void WarningOverlay::hide()
    {
        action_ = nullptr;
        if (getPeer() != nullptr && getParentComponent() != nullptr && getParentComponent()->isShowing() && isVisible())
            getParentComponent()->grabKeyboardFocus();
        setVisible (false);
    }
}
