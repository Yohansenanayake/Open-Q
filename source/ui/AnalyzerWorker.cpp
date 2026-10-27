// SPDX-License-Identifier: AGPL-3.0-only
#include "AnalyzerWorker.h"
#include "ResponseMapping.h"
#include <chrono>

namespace openq
{
    AnalyzerWorker::AnalyzerWorker (AnalyzerTransport& source) : Thread ("Open-Q analyzer"), transport (source) {}
    AnalyzerWorker::~AnalyzerWorker() { stop(); }

    bool AnalyzerWorker::start()
    {
        if (isThreadRunning())
            return true;
        if (!startThread (juce::Thread::Priority::low))
            return false;
        transport.setActive (true);
        return true;
    }

    void AnalyzerWorker::stop()
    {
        transport.setActive (false);
        signalThreadShouldExit();
        notify();
        stopThread (-1);
    }

    void AnalyzerWorker::setGeometry (ResponseGeometry value)
    {
        geometry.getWriter() = value;
        geometry.publish();
        notify();
    }

    void AnalyzerWorker::run()
    {
        juce::ScopedNoDenormals noDenormals;
        using Clock = std::chrono::steady_clock;
        AnalyzerEngine engine;
        AnalyzerPacket packet;
        std::array<std::vector<float>, 2> levels;
        auto lastTick = Clock::now(), lastAudio = lastTick;
        unsigned previousSettings = transport.getSettings().encode();
        std::uint64_t revision = 0;
        while (!threadShouldExit())
        {
            geometry.pull();
            const auto bounds = geometry.getReader();
            const auto settings = transport.getSettings();
            const auto token = transport.session();
            bool fresh = false;
            // Bounded drain: a continuously running offline producer must not starve stop().
            for (int count = 0; count < AnalyzerTransport::packetSlots && transport.read (packet); ++count)
                if (packet.session == token && (token & 1u) != 0)
                {
                    engine.push (packet);
                    fresh = true;
                }
            const auto now = Clock::now();
            const auto elapsed = std::chrono::duration<float> (now - lastTick).count();
            lastTick = now;
            if (fresh)
                lastAudio = now;
            const bool stale = std::chrono::duration<double> (now - lastAudio).count() > analyzerDisplay::staleAfterSeconds;
            if (fresh || previousSettings != settings.encode())
                engine.analyze (settings);
            const auto width = std::clamp (bounds.width, 0, 8192);
            const bool reset = revision != engine.getRevision() || ((previousSettings ^ settings.encode()) & 4u) != 0;
            previousSettings = settings.encode();
            revision = engine.getRevision();
            for (auto& trace : levels)
                if (trace.size() != static_cast<size_t> (width + 1) || reset)
                    trace.assign (static_cast<size_t> (width + 1), analyzerDisplay::minimumDb);
            auto& frame = frames.getWriter();
            frame.geometry = bounds;
            frame.sampleRate = engine.getSampleRate();
            frame.settings = settings;
            frame.session = token;
            frame.valid = frame.sampleRate > 0.0 && width > 0 && bounds.height > 0;
            frame.stale = stale;
            for (auto& path : frame.fills)
                path.clear();
            for (auto& path : frame.outlines)
                path.clear();
            if (frame.valid)
            {
                const auto maximum = responseMapping::maximumFrequency (frame.sampleRate);
                for (size_t trace = 0; trace < levels.size(); ++trace)
                {
                    if (!(trace == 0 ? settings.pre : settings.post))
                    {
                        std::fill (levels[trace].begin(), levels[trace].end(), analyzerDisplay::minimumDb);
                        continue;
                    }
                    auto& fill = frame.fills[trace];
                    auto& outline = frame.outlines[trace];
                    fill.startNewSubPath (0.0f, static_cast<float> (bounds.height));
                    for (int x = 0; x <= width; ++x)
                    {
                        const auto frequency = responseMapping::proportionToFrequency (static_cast<double> (x) / width, maximum);
                        const auto power = engine.powerAt (trace, frequency);
                        const auto db = 10.0f * std::log10 (std::max (power, 1.0e-20f))
                                        + analyzerDisplay::tiltDbPerOctave * static_cast<float> (std::log2 (frequency / 1000.0));
                        auto& level = levels[trace][static_cast<size_t> (x)];
                        const auto falling = std::max (analyzerDisplay::minimumDb, level - elapsed * analyzerDisplay::fallDbPerSecond);
                        level = std::clamp (stale ? falling : std::max (db, falling), analyzerDisplay::minimumDb, analyzerDisplay::maximumDb);
                        const auto proportion = (analyzerDisplay::maximumDb - level) / (analyzerDisplay::maximumDb - analyzerDisplay::minimumDb);
                        const auto drawX = static_cast<float> (x) * static_cast<float> (bounds.width) / static_cast<float> (width);
                        const auto y = proportion * static_cast<float> (bounds.height);
                        fill.lineTo (drawX, y);
                        if (x == 0)
                            outline.startNewSubPath (drawX, y);
                        else
                            outline.lineTo (drawX, y);
                    }
                    fill.lineTo (static_cast<float> (bounds.width), static_cast<float> (bounds.height));
                    fill.closeSubPath();
                }
            }
            frames.publish();
            wait (analyzerDisplay::refreshMilliseconds);
        }
    }
}
