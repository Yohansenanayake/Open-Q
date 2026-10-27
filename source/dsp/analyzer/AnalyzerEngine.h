// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "AnalyzerTransport.h"

namespace openq
{
    // Worker-owned analysis; never called by processBlock or the message thread.
    class AnalyzerEngine
    {
    public:
        AnalyzerEngine();
        ~AnalyzerEngine();
        void push (const AnalyzerPacket& packet);
        void analyze (AnalyzerSettings settings);
        void updateDisplay (double elapsedSeconds, bool stale, float minimumDb, float tiltDbPerOctave);
        float displayDbAt (size_t trace, double frequency) const;
        float powerAt (size_t trace, double frequency) const;
        double getSampleRate() const;
        std::uint64_t getRevision() const;
        size_t getMiddleFFTSize() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}
