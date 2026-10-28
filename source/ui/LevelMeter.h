// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
namespace openq
{
    class LevelMeter final : public juce::Component
    {
    public:
        explicit LevelMeter (const juce::String& name);
        ~LevelMeter() override;
        void resized() override;
        void update (const std::array<float, 2>& peaks, int channels, double elapsedSeconds);
        void reset();
        void setGloballyBypassed (bool);
        void mouseDown (const juce::MouseEvent&) override;
        static constexpr int preferredWidth = 52;

    private:
        class Artwork;
        class Display;
        struct Channel
        {
            float peak = silenceDb, tick = silenceDb, maximum = silenceDb;
            double heldSeconds = 0.0;
        };
        static constexpr float silenceDb = -200.0f, minimumDb = -60.0f, maximumDb = 6.0f;
        static constexpr float releaseDbPerSecond = 24.0f, tickReleaseDbPerSecond = 3.0f;
        static constexpr double holdSeconds = 2.0, readoutSeconds = 0.1;
        float yOf (float db) const;
        std::array<int, 4> positions() const;
        std::unique_ptr<Artwork> artwork;
        std::unique_ptr<Display> display;
        std::array<Channel, 2> levels;
        std::array<juce::Rectangle<float>, 2> bars;
        juce::Rectangle<int> meterArea, readoutArea;
        juce::ColourGradient gradient, bypassGradient;
        bool globallyBypassed = false;
        juce::String name, readout { "-inf" };
        int channelCount = 2;
        double readoutElapsed = 0.0;
        float scale = 1.0f;
    };
}
