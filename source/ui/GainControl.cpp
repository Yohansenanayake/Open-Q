// SPDX-License-Identifier: AGPL-3.0-only
// Wheel interaction follows ZLEqualizer's SnappingSlider, revision 3ce598ee065b3d3847121e76e196feda7ed8f56d.
// See THIRD_PARTY_NOTICES.md for reference attribution.
#include "GainControl.h"
#include "Palette.h"
#include <cmath>
#include <locale>
#include <sstream>
namespace openq
{
    GainControl::GainControl (juce::AudioProcessorValueTreeState& parameters, const char* id)
    {
        setComponentID (id);
        setTitle (juce::String (id).startsWith ("input") ? "Input gain" : "Output gain");
        setSliderStyle (juce::Slider::RotaryVerticalDrag);
        setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        setScrollWheelEnabled (true);
        setTextValueSuffix (" dB");
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        setRepaintsOnMouseActivity (true);
        setTooltip ("Drag up/down or scroll to adjust gain; wheel steps are 0.5 dB, or 0.1 dB with Shift. Shift also gives fine dragging. Double-click or Enter to type. Ctrl/Cmd-double-click resets to 0 dB.");
        value.setComponentID ("gain.value");
        value.setJustificationType (juce::Justification::centred);
        value.setEditable (false, false, true);
        value.setInterceptsMouseClicks (false, true);
        value.setColour (juce::Label::textColourId, palette::text);
        addAndMakeVisible (value);
        value.onTextChange = [this] { commitText(); };
        onValueChange = [this] { updateText(); };
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (parameters, id, *this);
        updateText();
    }
    GainControl::~GainControl() { value.hideEditor (true); }
    void GainControl::updateText()
    {
        value.setText (juce::String (getValue(), 1) + " dB", juce::dontSendNotification);
    }
    void GainControl::commitText()
    {
        auto text = value.getText().trim();
        if (text.endsWithIgnoreCase ("dB"))
            text = text.dropLastCharacters (2).trim();
        double db = 0.0;
        std::istringstream input (text.toStdString());
        input.imbue (std::locale::classic());
        input >> std::noskipws >> db;
        if (!input.fail() && input.eof() && std::isfinite (db))
        {
            juce::Slider::ScopedDragNotification gesture (*this);
            setValue (db, juce::sendNotificationSync);
        }
        updateText();
    }
    void GainControl::paint (juce::Graphics& g)
    {
        const auto scale = static_cast<float> (getHeight()) / 26.0f;
        const bool active = isMouseOver (true) || hasKeyboardFocus (true) || isMouseButtonDown (true);
        g.setColour (palette::background);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 3.0f * scale);
        g.setColour (active ? palette::accent : palette::inactiveIcon);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f * scale), 3.0f * scale, scale);
    }
    void GainControl::resized()
    {
        const auto scale = static_cast<float> (getHeight()) / 26.0f;
        value.setFont (juce::FontOptions (14.0f * scale));
        value.setBounds (getLocalBounds());
        setMouseDragSensitivity (std::max (1, juce::roundToInt (250.0f * scale)));
    }
    void GainControl::mouseDown (const juce::MouseEvent& event)
    {
        const auto scale = static_cast<float> (getHeight()) / 26.0f;
        setMouseDragSensitivity (std::max (1, juce::roundToInt (250.0f * scale * (event.mods.isShiftDown() ? 10.0f : 1.0f))));
        dragging = true;
        discardDrag = false;
        lastMessageTick = juce::Time::getMillisecondCounterHiRes() * 0.001;
        juce::Slider::mouseDown (event);
    }
    void GainControl::messageThreadTick (double seconds)
    {
        if (dragging && seconds - lastMessageTick > maximumMessageGapSeconds)
            discardDrag = true;
        lastMessageTick = seconds;
    }
    void GainControl::mouseDrag (const juce::MouseEvent& event)
    {
        if (!dragging)
            return;
        const auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        if (discardDrag || now - lastMessageTick > maximumMessageGapSeconds)
        {
            // Do not apply accumulated mouse displacement after a UI/debugger stall.
            juce::Slider::mouseUp (event);
            dragging = false;
            return;
        }
        juce::Slider::mouseDrag (event);
    }
    void GainControl::mouseUp (const juce::MouseEvent& event)
    {
        juce::Slider::mouseUp (event);
        dragging = false;
        discardDrag = false;
    }
    void GainControl::mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
    {
        if (!isEnabled() || !isScrollWheelEnabled() || dragging || event.mods.isAnyMouseButtonDown() || value.isBeingEdited() || wheel.isInertial
            || !std::isfinite (wheel.deltaX) || !std::isfinite (wheel.deltaY))
            return;
        const auto delta = (std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? -wheel.deltaX : wheel.deltaY) * (wheel.isReversed ? -1.0f : 1.0f);
        const auto timestamp = event.eventTime.toMilliseconds();
        if (juce::exactlyEqual (delta, 0.0f) || timestamp == lastWheelEventMilliseconds)
            return;
        lastWheelEventMilliseconds = timestamp;
        // Stepped wheel scales vary between JUCE backends; smooth input keeps its fractional magnitude.
        const auto steps = wheel.isSmooth ? static_cast<double> (delta) / smoothWheelUnitsPerStep : std::copysign (1.0, delta);
        const auto db = std::clamp (getValue() + steps * (event.mods.isShiftDown() ? fineWheelStepDb : wheelStepDb), getMinimum(), getMaximum());
        if (juce::exactlyEqual (db, getValue()))
            return;
        juce::Slider::ScopedDragNotification gesture (*this);
        setValue (db, juce::sendNotificationSync);
    }
    void GainControl::mouseDoubleClick (const juce::MouseEvent& event)
    {
        if (event.mods.isCommandDown())
        {
            juce::Slider::ScopedDragNotification gesture (*this);
            setValue (gainLimits::defaultDb, juce::sendNotificationSync);
        }
        else
            value.showEditor();
    }
    bool GainControl::keyPressed (const juce::KeyPress& key)
    {
        if (key == juce::KeyPress::returnKey)
        {
            value.showEditor();
            return true;
        }
        return juce::Slider::keyPressed (key);
    }
    void GainControl::visibilityChanged()
    {
        if (!isVisible())
        {
            discardDrag = true;
            value.hideEditor (true);
        }
    }
}
