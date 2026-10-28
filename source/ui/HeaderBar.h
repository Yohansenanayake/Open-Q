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
        // std::function<void()> onInspect;
        std::function<void()> onFillChanged, onPresets;
        PresetNameButton presets;
        static constexpr int preferredHeight = 52;

    private:
        UiLookAndFeel lookAndFeel;
        struct Branding final : juce::Component
        {
            Branding();
            void paint (juce::Graphics&) override;
            void lookAndFeelChanged() override { repaint(); }
            static constexpr int width = 188, height = 48;
            static constexpr float leftShoulder = 42.0f, rightShoulder = 24.0f;
            static constexpr float logoInset = 30.0f, logoVerticalPadding = 2.0f;
            std::unique_ptr<juce::Drawable> logo;
        } branding;
        static constexpr int horizontalPadding = 12;
        AnalyzerTransport& transport;
        juce::ToggleButton pre { "Pre" }, post { "Post" }, fill { "Fill" };
        // juce::TextButton inspect { "Inspect the UI" };
        IconButton bypass;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
    };
}
