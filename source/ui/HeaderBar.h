// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "../dsp/analyzer/AnalyzerTransport.h"
#include "UiLookAndFeel.h"
#include "IconButton.h"
#include "../PluginParameters.h"
namespace openq
{
    class HeaderBar final : public juce::Component
    {
    public:
        HeaderBar (AnalyzerTransport&, juce::AudioProcessorValueTreeState&);
        ~HeaderBar() override;
        void refresh();
        void setUiScale (float);
        void paint (juce::Graphics&) override;
        void resized() override;
        std::function<void()> onInspect, onFillChanged;
        static constexpr int preferredHeight = 52;

    private:
        UiLookAndFeel lookAndFeel;
        AnalyzerTransport& transport;
        juce::ToggleButton pre { "Pre" }, post { "Post" }, fill { "Fill" };
        juce::TextButton inspect { "Inspect the UI" };
        IconButton bypass;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
    };
}
