// SPDX-License-Identifier: AGPL-3.0-only
#include "ResponseWorker.h"
#include <complex>
#include <numbers>

namespace openq
{
    double responseMagnitudeDb (const AppliedResponse& response, double frequencyHz)
    {
        if (!std::isfinite (response.sampleRate) || response.sampleRate < 1000.0
            || !std::isfinite (frequencyHz) || frequencyHz < 0.0 || frequencyHz > response.sampleRate * 0.5
            || !std::isfinite (response.wet))
            return 0.0;
        if (response.wet <= 0.0)
            return 0.0;
        const auto z = std::polar (1.0, -2.0 * std::numbers::pi * frequencyHz / response.sampleRate);
        const auto& c = response.coefficients;
        const auto h = (c.b0 + c.b1 * z + c.b2 * z * z) / (1.0 + c.a1 * z + c.a2 * z * z);
        const auto effective = 1.0 + std::clamp (response.wet, 0.0, 1.0) * (h - 1.0);
        const auto magnitude = std::abs (effective);
        return std::isfinite (magnitude) ? 20.0 * std::log10 (std::max (magnitude, 1e-12)) : 0.0;
    }

    ResponseWorker::ResponseWorker (TripleBuffer<AppliedResponse>& input)
        : juce::Thread ("Open-Q response"), snapshots (input)
    {
    }

    ResponseWorker::~ResponseWorker() { stop(); }
    bool ResponseWorker::start() { return startThread (juce::Thread::Priority::low); }
    void ResponseWorker::stop() { stopThread (-1); }
    void ResponseWorker::requestUpdate() { notify(); }
    bool ResponseWorker::pullFrame() { return frames.pull(); }
    const ResponseFrame& ResponseWorker::getFrame() const { return frames.getReader(); }

    void ResponseWorker::setGeometry (ResponseGeometry next)
    {
        geometry.getWriter() = next;
        geometry.publish();
        requestUpdate();
    }

    void ResponseWorker::run()
    {
        bool first = true;
        while (!threadShouldExit())
        {
            wait (-1);
            if (threadShouldExit())
                break;
            const auto geometryChanged = geometry.pull();
            const auto responseChanged = snapshots.pull();
            if (!first && !geometryChanged && !responseChanged)
                continue;
            first = false;
            auto& frame = frames.getWriter();
            frame.geometry = geometry.getReader();
            frame.response = snapshots.getReader();
            frame.curve.clear();
            frame.valid = frame.geometry.width > 0 && frame.geometry.height > 0
                          && std::isfinite (frame.response.sampleRate) && frame.response.sampleRate >= 1000.0;
            if (frame.valid)
            {
                constexpr int points = 400;
                const auto maxFrequency = std::min (20000.0, frame.response.sampleRate * 0.5);
                for (int i = 0; i < points; ++i)
                {
                    if (threadShouldExit())
                        return;
                    const auto proportion = static_cast<double> (i) / static_cast<double> (points - 1);
                    const auto frequency = 20.0 * std::pow (maxFrequency / 20.0, proportion);
                    const auto db = std::clamp (responseMagnitudeDb (frame.response, std::min (frequency, maxFrequency)), -24.0, 24.0);
                    const auto x = static_cast<float> (proportion * frame.geometry.width);
                    const auto y = static_cast<float> ((24.0 - db) / 48.0 * frame.geometry.height);
                    if (i == 0)
                        frame.curve.startNewSubPath (x, y);
                    else
                        frame.curve.lineTo (x, y);
                }
            }
            frames.publish();
        }
    }
}
