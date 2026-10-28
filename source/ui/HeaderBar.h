// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "../PluginParameters.h"
#include "../dsp/analyzer/AnalyzerTransport.h"
#include "IconButton.h"
#include "PresetNameButton.h"
#include "UiLookAndFeel.h"
namespace openq
{
    class HeaderBar final : public juce::Component
    {
    public:
        HeaderBar (AnalyzerTransport&, juce::AudioProcessorValueTreeState&);
        ~HeaderBar() override;
        void refresh();
        void setPresetName (const juce::String&);
        void setUiScale (float);
        void paint (juce::Graphics&) override;
        void resized() override;
        std::function<void()> onInspect, onFillChanged, onPresets;
        PresetNameButton presets;
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
