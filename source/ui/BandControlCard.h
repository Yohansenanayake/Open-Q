// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "../BandParameters.h"
#include "CompactComboBox.h"
#include "IconButton.h"
#include "BinaryData.h"

namespace openq
{
    class BandControlCard final : public juce::Component
    {
    public:
        static constexpr int preferredWidth = 350, preferredHeight = 166;
        explicit BandControlCard (juce::AudioProcessorValueTreeState& parameters);
        void setBand (size_t band);
        void dismissPopups();
        void paint (juce::Graphics&) override;
        void resized() override;
        void visibilityChanged() override;
        std::function<void (bool)> onDragStateChanged;
        std::function<void()> onDelete;
        std::function<void()> onClose;

    private:
        juce::AudioProcessorValueTreeState& parameters;
        juce::Colour bandColour;
        void updateControlEnablement();
        juce::Label title;
        CompactComboBox typeSelector, slopeSelector;
        IconButton powerButton { "Band power", BinaryData::bypass_svg, BinaryData::bypass_svgSize };
        IconButton deleteButton { "Delete band", BinaryData::trash_svg, BinaryData::trash_svgSize };
        IconButton closeButton { "Close band controls", BinaryData::close_svg, BinaryData::close_svgSize };
        std::array<juce::Slider, 3> controls;
        std::array<juce::Label, 3> labels;
        std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 3> attachments;
        std::unique_ptr<juce::ParameterAttachment> statusAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> typeAttachment, slopeAttachment;
    };
}
