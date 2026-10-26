// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "../BandParameters.h"

namespace openq
{
    class BandControlCard final : public juce::Component
    {
    public:
        explicit BandControlCard (juce::AudioProcessorValueTreeState& parameters);
        void setBand (size_t band);
        void paint (juce::Graphics&) override;
        void resized() override;
        std::function<void (bool)> onDragStateChanged;
        std::function<void()> onDelete;

    private:
        juce::AudioProcessorValueTreeState& parameters;
        juce::Label title;
        juce::ComboBox status;
        juce::TextButton deleteButton { "X" };
        std::array<juce::Slider, 3> controls;
        std::array<juce::Label, 3> labels;
        std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 3> attachments;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> statusAttachment;
    };
}
