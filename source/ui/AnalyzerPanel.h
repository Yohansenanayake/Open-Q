// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "../dsp/analyzer/AnalyzerTransport.h"
#include "CompactComboBox.h"

namespace openq
{
    class AnalyzerPanel final : public juce::Component
    {
    public:
        explicit AnalyzerPanel (AnalyzerTransport&);
        void refresh();
        void dismissPopups();
        void paint (juce::Graphics&) override;
        void resized() override;
        void visibilityChanged() override;
        bool keyPressed (const juce::KeyPress&) override;
        std::function<void()> onClose;
        static constexpr int preferredWidth = 320, preferredHeight = 220;

    private:
        AnalyzerTransport& transport;
        std::array<juce::Label, 4> labels;
        CompactComboBox quality, smoothing, amount, decay;
        juce::TextButton close { "Close" };
        AnalyzerSmoothing shownMode = AnalyzerSmoothing::Erb;
    };
}
