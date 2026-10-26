// SPDX-License-Identifier: AGPL-3.0-only
#include "BandControlCard.h"

namespace openq
{
    BandControlCard::BandControlCard (juce::AudioProcessorValueTreeState& state) : parameters (state)
    {
        setComponentID ("band.card");
        setTitle ("Selected band controls");
        title.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (title);
        status.setComponentID ("band.status");
        status.addItemList ({ "Off", "Bypass", "On" }, 1);
        status.setTooltip ("Off: unused slot. Bypass: keep the filter warm without applying EQ. On: apply EQ. Settings are retained in all states.");
        addAndMakeVisible (status);
        deleteButton.setComponentID ("band.delete");
        deleteButton.setTitle ("Delete selected band");
        deleteButton.setTooltip ("Delete this band (set Off). Click empty graph space to hide the card without deleting.");
        deleteButton.onClick = [this] { if (onDelete) onDelete(); };
        addAndMakeVisible (deleteButton);
        const std::array<juce::String, 3> names { "Frequency", "Gain", "Q" };
        for (size_t i = 0; i < controls.size(); ++i)
        {
            auto& control = controls[i];
            control.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            control.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 98, 22);
            control.setTitle (names[i]);
            control.setComponentID ("band." + names[i].toLowerCase());
            control.onDragStart = [this] { if (onDragStateChanged) onDragStateChanged (true); };
            control.onDragEnd = [this] { if (onDragStateChanged) onDragStateChanged (false); };
            control.setDoubleClickReturnValue (true, i == 0 ? 1000.0 : i == 1 ? 0.0
                                                                              : 1.0);
            addAndMakeVisible (control);
            labels[i].setText (names[i], juce::dontSendNotification);
            labels[i].setJustificationType (juce::Justification::centred);
            addAndMakeVisible (labels[i]);
        }
        controls[0].setTextValueSuffix (" Hz");
        controls[1].setTextValueSuffix (" dB");
        controls[0].setTooltip ("Bell center frequency; the DSP limits it below Nyquist.");
        controls[1].setTooltip ("Boost or cut. Zero dB leaves the signal unchanged.");
        controls[2].setTooltip ("Bandwidth: lower Q is wider; higher Q is narrower.");
    }

    void BandControlCard::setBand (size_t band)
    {
        for (auto& control : controls)
            control.hideTextBox (false);
        statusAttachment.reset();
        for (auto& attachment : attachments)
            attachment.reset();
        setVisible (band < bandCount);
        if (band >= bandCount)
            return;
        title.setText ("Band " + juce::String (band + 1) + "  /  Bell", juce::dontSendNotification);
        status.setTitle ("Band " + juce::String (band + 1) + " status");
        const std::array<const char*, 3> names { "frequency", "gain", "q" };
        for (size_t i = 0; i < controls.size(); ++i)
        {
            attachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (parameters, bandParameterId (band, names[i]), controls[i]);
            controls[i].textFromValueFunction = nullptr;
            controls[i].setNumDecimalPlacesToDisplay (i == 0 ? 1 : 2);
        }
        statusAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (parameters, bandParameterId (band, "status"), status);
    }

    void BandControlCard::paint (juce::Graphics& g)
    {
        const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (juce::Colour (0xff222b35));
        g.fillRoundedRectangle (bounds, 8.0f);
        g.setColour (juce::Colour (0xff6edbd4).withAlpha (0.65f));
        g.drawRoundedRectangle (bounds, 8.0f, 1.0f);
    }

    void BandControlCard::resized()
    {
        auto area = getLocalBounds().reduced (10);
        auto header = area.removeFromTop (26);
        deleteButton.setBounds (header.removeFromRight (26));
        header.removeFromRight (6);
        status.setBounds (header.removeFromRight (100));
        title.setBounds (header);
        area.removeFromTop (6);
        const auto width = area.getWidth() / 3;
        for (size_t i = 0; i < controls.size(); ++i)
        {
            auto column = area.removeFromLeft (width).reduced (3, 0);
            labels[i].setBounds (column.removeFromTop (18));
            controls[i].setBounds (column);
        }
    }
}
