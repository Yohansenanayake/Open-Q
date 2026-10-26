// SPDX-License-Identifier: AGPL-3.0-only
#include "BandControlCard.h"
#include "BinaryData.h"

namespace openq
{
    BandControlCard::BandControlCard (juce::AudioProcessorValueTreeState& state) : parameters (state)
    {
        setComponentID ("band.card");
        setTitle ("Selected band controls");
        title.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (title);
        typeSelector.setComponentID ("band.type");
        typeSelector.addItemList ({ "Bell", "Low Shelf", "High Shelf" }, 1);
        typeSelector.setTooltip ("Filter response type");
        addAndMakeVisible (typeSelector);
        auto power = juce::Drawable::createFromImageData (BinaryData::bypass_svg, BinaryData::bypass_svgSize);
        auto close = juce::Drawable::createFromImageData (BinaryData::close_svg, BinaryData::close_svgSize);
        auto trash = juce::Drawable::createFromImageData (BinaryData::trash_svg, BinaryData::trash_svgSize);
        jassert (power != nullptr && close != nullptr && trash != nullptr);
        if (power != nullptr && close != nullptr && trash != nullptr)
        {
            auto powerOn = power->createCopy();
            auto powerHover = power->createCopy();
            auto closeHover = close->createCopy();
            auto trashHover = trash->createCopy();
            power->replaceColour (juce::Colours::black, juce::Colour (0xff8996a5));
            close->replaceColour (juce::Colours::black, juce::Colour (0xff8996a5));
            trash->replaceColour (juce::Colours::black, juce::Colour (0xff8996a5));
            powerOn->replaceColour (juce::Colours::black, juce::Colour (0xff6edbd4));
            powerHover->replaceColour (juce::Colours::black, juce::Colour (0xffe0e7ef));
            closeHover->replaceColour (juce::Colours::black, juce::Colour (0xffe0e7ef));
            trashHover->replaceColour (juce::Colours::black, juce::Colour (0xffe0e7ef));
            powerButton.setImages (power.get(), powerHover.get(), powerOn.get(), nullptr, powerOn.get(), powerHover.get(), powerOn.get());
            deleteButton.setImages (trash.get(), trashHover.get(), trashHover.get());
            closeButton.setImages (close.get(), closeHover.get(), closeHover.get());
        }
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
        controls[0].setTooltip ("Bell center or shelf midpoint frequency; limited below Nyquist.");
        controls[1].setTooltip ("Boost or cut. Zero dB leaves the signal unchanged.");
        controls[2].setTooltip ("Bell bandwidth or shelf resonance. High Q can produce large resonant peaks.");
    }

    void BandControlCard::setBand (size_t band)
    {
        for (auto& control : controls)
            control.hideTextBox (false);
        statusAttachment.reset();
        typeAttachment.reset();
        for (auto& attachment : attachments)
            attachment.reset();
        setVisible (band < bandCount);
        if (band >= bandCount)
            return;
        title.setText ("Band " + juce::String (band + 1), juce::dontSendNotification);
        typeSelector.setTitle ("Band " + juce::String (band + 1) + " filter type");
        typeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (parameters, bandParameterId (band, "type"), typeSelector);
        powerButton.setTitle ("Band " + juce::String (band + 1) + " power");
        const std::array<const char*, 3> names { "frequency", "gain", "q" };
        for (size_t i = 0; i < controls.size(); ++i)
        {
            attachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (parameters, bandParameterId (band, names[i]), controls[i]);
            controls[i].textFromValueFunction = nullptr;
            controls[i].setNumDecimalPlacesToDisplay (i == 0 ? 1 : 2);
        }
        statusAttachment = std::make_unique<juce::ParameterAttachment> (*parameters.getParameter (bandParameterId (band, "status")), [this] (float value) {
            const auto bandStatus = static_cast<BandStatus> (juce::roundToInt (value));
            powerButton.setToggleState (bandStatus == BandStatus::On, juce::dontSendNotification);
            powerButton.setEnabled (bandStatus != BandStatus::Off);
            powerButton.setTooltip (bandStatus == BandStatus::On ? "Bypass this band" : bandStatus == BandStatus::Bypass ? "Enable this band" : "Band deleted. Double-click empty graph space to create a band."); }, nullptr);
        statusAttachment->sendInitialUpdate();
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
        closeButton.setBounds (header.removeFromRight (26));
        header.removeFromRight (6);
        deleteButton.setBounds (header.removeFromRight (26));
        header.removeFromRight (6);
        powerButton.setBounds (header.removeFromLeft (26));
        header.removeFromLeft (6);
        typeSelector.setBounds (header.removeFromRight (140));
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
