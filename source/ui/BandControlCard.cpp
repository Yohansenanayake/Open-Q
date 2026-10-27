// SPDX-License-Identifier: AGPL-3.0-only
#include "BandControlCard.h"
#include "BinaryData.h"
#include "Palette.h"

namespace openq
{
    BandControlCard::BandControlCard (juce::AudioProcessorValueTreeState& state) : parameters (state)
    {
        setComponentID ("band.card");
        setTitle ("Selected band controls");
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
            power->replaceColour (juce::Colours::black, palette::inactiveIcon);
            close->replaceColour (juce::Colours::black, palette::inactiveIcon);
            trash->replaceColour (juce::Colours::black, palette::inactiveIcon);
            powerOn->replaceColour (juce::Colours::black, palette::accent);
            powerHover->replaceColour (juce::Colours::black, palette::text);
            closeHover->replaceColour (juce::Colours::black, palette::text);
            trashHover->replaceColour (juce::Colours::black, palette::text);
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
            control.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 22);
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
            return;
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
        statusAttachment = std::make_unique<juce::ParameterAttachment> (*parameters.getParameter (bandParameterId (band, "status")), [this] (float value) {
            const auto bandStatus = static_cast<BandStatus> (juce::roundToInt (value));
            powerButton.setToggleState (bandStatus == BandStatus::On, juce::dontSendNotification);
            powerButton.setEnabled (bandStatus != BandStatus::Off);
            powerButton.setTooltip (bandStatus == BandStatus::On ? "Bypass this band" : bandStatus == BandStatus::Bypass ? "Enable this band" : "Band deleted. Double-click empty graph space to create a band."); }, nullptr);
        statusAttachment->sendInitialUpdate();
        updateControlEnablement();
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
        const auto bounds = getLocalBounds().toFloat().reduced (0.5f * scale);
        g.setColour (palette::cardBackground);
        g.fillRoundedRectangle (bounds, 8.0f * scale);
        g.setColour (palette::accent.withAlpha (0.65f));
        g.drawRoundedRectangle (bounds, 8.0f * scale, scale);
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
        const auto scaled = [scale] (int value) { return juce::roundToInt (static_cast<float> (value) * scale); };
        auto area = getLocalBounds().reduced (scaled (10));
        const int buttonSize = scaled (26), spacing = scaled (6);
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
            controls[i].setTextBoxStyle (juce::Slider::TextBoxBelow, false, scaled (74), scaled (22));
            controls[i].setMouseDragSensitivity (scaled (250));
            controls[i].setBounds (column);
        }
    }
}
