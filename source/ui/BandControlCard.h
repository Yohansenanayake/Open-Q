// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "../BandParameters.h"

namespace openq
{
    class BandControlCard final : public juce::Component
    {
    public:
        static constexpr int preferredWidth = 350, preferredHeight = 196;
        explicit BandControlCard (juce::AudioProcessorValueTreeState& parameters);
        void setBand (size_t band);
        void paint (juce::Graphics&) override;
        void resized() override;
        std::function<void (bool)> onDragStateChanged;
        std::function<void()> onDelete;
        std::function<void()> onClose;

    private:
        juce::AudioProcessorValueTreeState& parameters;
        void updateControlEnablement();
        juce::Label title, slopeLabel;
        juce::ComboBox typeSelector, slopeSelector;
        juce::DrawableButton powerButton { "Band power", juce::DrawableButton::ImageFitted };
        juce::DrawableButton deleteButton { "Delete band", juce::DrawableButton::ImageFitted };
        juce::DrawableButton closeButton { "Close band controls", juce::DrawableButton::ImageFitted };
        std::array<juce::Slider, 3> controls;
        std::array<juce::Label, 3> labels;
        std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 3> attachments;
        std::unique_ptr<juce::ParameterAttachment> statusAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> typeAttachment, slopeAttachment;
    };
}
