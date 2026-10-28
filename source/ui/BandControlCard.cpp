// SPDX-License-Identifier: AGPL-3.0-only
#include "BandControlCard.h"
#include "BinaryData.h"
#include "Palette.h"
#include "PanelDrawing.h"
#include "ControlDrawing.h"
#include "GainTimingTrace.h"

namespace openq
{
    BandControlCard::OpaqueInterior::OpaqueInterior()
    {
        setOpaque (true);
        setInterceptsMouseClicks (false, false);
    }

    void BandControlCard::OpaqueInterior::paint (juce::Graphics& g)
    {
        g.fillAll (palette::cardBackground);
    }

    BandControlCard::BandControlCard (juce::AudioProcessorValueTreeState& state) : parameters (state), bandColour (palette::accent)
    {
        setComponentID ("band.card");
        setTitle ("Selected band controls");
        // Stop control-only damage from rasterizing the spectrum behind the rounded card.
        addAndMakeVisible (interior);
        title.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (title);
        typeSelector.setComponentID ("band.type");
        typeSelector.addItemList ({ "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut" }, 1);
        typeSelector.setTooltip ("Filter response type");
        addAndMakeVisible (typeSelector);
        typeSelector.onChange = [this] { updateControlEnablement(); };
        slopeSelector.setComponentID ("band.slope");
        for (size_t i = 0; i < cutSlopeNames.size(); ++i)
            slopeSelector.addItem (cutSlopeNames[i], static_cast<int> (i + 1));
        slopeSelector.setTooltip ("Cut slope. Bell and shelf responses retain their existing design.");
        slopeSelector.onChange = [this] { updateControlEnablement(); };
        addAndMakeVisible (slopeSelector);
        powerButton.setComponentID ("band.power");
        powerButton.setClickingTogglesState (true);
        powerButton.onClick = [this] {
            if (statusAttachment != nullptr && powerButton.isEnabled())
                statusAttachment->setValueAsCompleteGesture (static_cast<float> (powerButton.getToggleState() ? BandStatus::On : BandStatus::Bypass));
        };
        addAndMakeVisible (powerButton);
        deleteButton.setComponentID ("band.delete");
        deleteButton.setTitle ("Delete selected band");
        deleteButton.setTooltip ("Delete this band (set Off)");
        deleteButton.onClick = [this] { if (onDelete) onDelete(); };
        addAndMakeVisible (deleteButton);
        closeButton.setComponentID ("band.close");
        closeButton.setTitle ("Close band controls");
        closeButton.setTooltip ("Hide this card without changing the band or its audio");
        closeButton.onClick = [this] { if (onClose) onClose(); };
        addAndMakeVisible (closeButton);
        const std::array<juce::String, 3> names { "Frequency", "Gain", "Q" };
        for (size_t i = 0; i < controls.size(); ++i)
        {
            auto& control = controls[i];
            control.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            control.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, controlStyle::height);
            control.setTitle (names[i]);
            control.setComponentID ("band." + names[i].toLowerCase());
            control.onDragStart = [this] { if (onDragStateChanged) onDragStateChanged (true); };
            control.onDragEnd = [this] { if (onDragStateChanged) onDragStateChanged (false); };
            control.setDoubleClickReturnValue (true, i == 0 ? bandLimits::defaultFrequencyHz : i == 1 ? bandLimits::defaultGainDb
                                                                                                      : bandLimits::defaultQ);
            addAndMakeVisible (control);
            labels[i].setText (names[i], juce::dontSendNotification);
            labels[i].setJustificationType (juce::Justification::centred);
            addAndMakeVisible (labels[i]);
        }
        controls[0].setTextValueSuffix (" Hz");
        controls[1].setTextValueSuffix (" dB");
        controls[0].setTooltip ("Bell center, shelf midpoint or cut cutoff frequency; limited below Nyquist.");
        controls[1].setTooltip ("Boost or cut. Zero dB leaves the signal unchanged.");
        controls[2].setTooltip ("Bell bandwidth or shelf resonance. High Q can produce large resonant peaks.");
    }

    void BandControlCard::setBand (size_t band)
    {
        GainTimingTrace::instance().record ("card_bind_begin", static_cast<int> (band));
        if (GainTimingTrace::instance().isRecording())
            GainTimingTrace::instance().record ("card_bind_message_thread", static_cast<int> (band), juce::MessageManager::getInstance()->isThisTheMessageThread() ? 1.0 : 0.0);
        typeSelector.closePopup();
        slopeSelector.closePopup();
        for (auto& control : controls)
            control.hideTextBox (false);
        statusAttachment.reset();
        typeAttachment.reset();
        slopeAttachment.reset();
        for (auto& attachment : attachments)
            attachment.reset();
        setVisible (band < bandCount);
        if (band >= bandCount)
        {
            GainTimingTrace::instance().record ("card_bind_hidden", static_cast<int> (band));
            return;
        }
        const auto colour = palette::bandColour (band);
        powerButton.setAccentColour (colour);
        title.setText ("Band " + juce::String (band + 1), juce::dontSendNotification);
        typeSelector.setTitle ("Band " + juce::String (band + 1) + " filter type");
        typeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (parameters, bandParameterId (band, "type"), typeSelector);
        slopeSelector.setTitle ("Band " + juce::String (band + 1) + " cut slope");
        slopeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (parameters, bandParameterId (band, "slope"), slopeSelector);
        powerButton.setTitle ("Band " + juce::String (band + 1) + " power");
        const std::array<const char*, 3> names { "frequency", "gain", "q" };
        for (size_t i = 0; i < controls.size(); ++i)
        {
            attachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (parameters, bandParameterId (band, names[i]), controls[i]);
            controls[i].textFromValueFunction = nullptr;
            controls[i].setNumDecimalPlacesToDisplay (i == 0 ? 1 : 2);
        }
        statusAttachment = std::make_unique<juce::ParameterAttachment> (*parameters.getParameter (bandParameterId (band, "status")), [this, colour, band] (float value) {
            const auto bandStatus = static_cast<BandStatus> (juce::roundToInt (value));
            bandColour = bandStatus == BandStatus::On ? colour : colour.interpolatedWith (palette::cardBackground, palette::bypassBandAccentBlend);
            for (auto& control : controls)
            {
                control.setColour (juce::Slider::rotarySliderFillColourId, bandColour);
                control.setColour (juce::Slider::thumbColourId, bandColour);
            }
            repaint();
            powerButton.setToggleState (bandStatus == BandStatus::On, juce::dontSendNotification);
            powerButton.setEnabled (bandStatus != BandStatus::Off);
            powerButton.setTooltip (bandStatus == BandStatus::On ? "Bypass this band" : bandStatus == BandStatus::Bypass ? "Enable this band" : "Band deleted. Double-click empty graph space to create a band.");
            GainTimingTrace::instance().record ("card_colours_ready", static_cast<int> (band), static_cast<double> (bandColour.getARGB()));
            GainTimingTrace::instance().record ("card_status_ready", static_cast<int> (band), value); }, nullptr);
        statusAttachment->sendInitialUpdate();
        updateControlEnablement();
        if (GainTimingTrace::instance().isRecording())
            for (size_t i = 0; i < controls.size(); ++i)
                GainTimingTrace::instance().record (i == 0 ? "card_frequency_bound" : i == 1 ? "card_gain_bound" : "card_q_bound", static_cast<int> (band), controls[i].getValue());
        GainTimingTrace::instance().record ("card_bind_end", static_cast<int> (band));
    }

    void BandControlCard::updateControlEnablement()
    {
        const auto type = static_cast<FilterType> (typeSelector.getSelectedItemIndex());
        const bool cut = isCut (type);
        const bool qEnabled = !cut || slopeSelector.getSelectedItemIndex() != static_cast<int> (CutSlope::Db6);
        slopeSelector.setEnabled (cut);
        controls[1].setEnabled (!cut);
        labels[1].setAlpha (cut ? 0.5f : 1.0f);
        controls[2].setEnabled (qEnabled);
        labels[2].setAlpha (qEnabled ? 1.0f : 0.5f);
        controls[2].setDoubleClickReturnValue (true, cut ? bandLimits::defaultCutQ : bandLimits::defaultQ);
        controls[1].setTooltip (cut ? "Gain is retained but does not affect cut filters." : "Boost or cut. Zero dB leaves the signal unchanged.");
        controls[2].setTooltip (qEnabled ? "Bandwidth or resonance. High Q can produce large resonant peaks." : "6 dB/oct cuts have no adjustable Q. The stored value is retained.");
    }

    void BandControlCard::paint (juce::Graphics& g)
    {
        const auto scale = static_cast<float> (getWidth()) / static_cast<float> (preferredWidth);
        drawPanelBackground (g, getLocalBounds().toFloat(), scale, bandColour);
    }

    void BandControlCard::visibilityChanged()
    {
        if (!isVisible())
        {
            typeSelector.closePopup();
            slopeSelector.closePopup();
        }
    }

    void BandControlCard::dismissPopups()
    {
        typeSelector.closePopup();
        slopeSelector.closePopup();
    }

    void BandControlCard::resized()
    {
        const auto scale = static_cast<float> (getWidth()) / static_cast<float> (preferredWidth);
        interior.setBounds (getLocalBounds().reduced (static_cast<int> (std::ceil (panelCornerRadius * scale))));
        const auto scaled = [scale] (int value) { return juce::roundToInt (static_cast<float> (value) * scale); };
        auto area = getLocalBounds().reduced (scaled (10));
        const int buttonSize = scaled (controlStyle::height), spacing = scaled (6);
        title.setFont (juce::FontOptions (15.0f * scale));
        auto header = area.removeFromTop (buttonSize);
        closeButton.setBounds (header.removeFromRight (buttonSize));
        header.removeFromRight (spacing);
        deleteButton.setBounds (header.removeFromRight (buttonSize));
        header.removeFromRight (spacing);
        powerButton.setBounds (header.removeFromLeft (buttonSize));
        header.removeFromLeft (spacing);
        title.setBounds (header);
        area.removeFromTop (spacing);
        const int selectorColumnWidth = scaled (78);
        auto selectors = area.removeFromLeft (selectorColumnWidth);
        selectors.removeFromTop (scaled (18));
        typeSelector.setBounds (selectors.removeFromTop (buttonSize));
        selectors.removeFromTop (spacing);
        slopeSelector.setBounds (selectors.removeFromTop (buttonSize));
        area.removeFromLeft (spacing);
        const auto width = area.getWidth() / static_cast<int> (controls.size());
        for (size_t i = 0; i < controls.size(); ++i)
        {
            auto column = area.removeFromLeft (width).reduced (scaled (3), 0);
            labels[i].setFont (juce::FontOptions (15.0f * scale));
            labels[i].setBounds (column.removeFromTop (scaled (18)));
            controls[i].setTextBoxStyle (juce::Slider::TextBoxBelow, false, scaled (74), scaled (controlStyle::height));
            controls[i].setMouseDragSensitivity (scaled (250));
            controls[i].setBounds (column);
        }
    }
}
