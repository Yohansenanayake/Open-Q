// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "../dsp/analyzer/AnalyzerEngine.h"
#include "ResponseWorker.h"

namespace openq
{
    namespace analyzerDisplay
    {
        inline constexpr float minimumDb = -96.0f, maximumDb = 0.0f;
        inline constexpr float tiltDbPerOctave = 4.5f;
        inline constexpr float fallDbPerSecond = 24.0f;
        inline constexpr double staleAfterSeconds = 0.25;
        inline constexpr int refreshMilliseconds = 33;
    }

    struct AnalyzerFrame
    {
        std::array<juce::Path, 2> fills, outlines;
        ResponseGeometry geometry;
        AnalyzerSettings settings;
        double sampleRate = 0.0;
        unsigned session = 0;
        bool valid = false, stale = false;
    };

    class AnalyzerWorker : private juce::Thread
    {
    public:
        explicit AnalyzerWorker (AnalyzerTransport& transport);
        ~AnalyzerWorker() override;
        bool start();
        void stop();
        void setGeometry (ResponseGeometry value);
        bool pullFrame() { return frames.pull(); }
        const AnalyzerFrame& getFrame() const { return frames.getReader(); }

    private:
        void run() override;
        AnalyzerTransport& transport;
        TripleBuffer<ResponseGeometry> geometry;
        TripleBuffer<AnalyzerFrame> frames;
    };
}
