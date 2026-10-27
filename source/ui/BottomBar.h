// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "UiLookAndFeel.h"
namespace openq
{
    class BottomBar final : public juce::Component
    {
    public:
        BottomBar();
        ~BottomBar() override;
        void setUiScale (float);
        void paint (juce::Graphics&) override;
        bool hitTest (int x, int y) override;
        void resized() override;
        juce::TextButton analyzer { "Analyzer" };
        static constexpr int preferredHeight = 44;

    private:
        juce::Path backgroundShape() const;
        UiLookAndFeel lookAndFeel;
    };
}
