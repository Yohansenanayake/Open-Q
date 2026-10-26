// SPDX-License-Identifier: AGPL-3.0-only
#include "ResponseWorker.h"
#include <complex>
#include <numbers>

namespace openq
{
    namespace
    {
        struct TargetParameters
        {
            double frequency = 1000.0, gain = 0.0, quality = 1.0, sampleRate = 0.0;
            bool operator== (const TargetParameters&) const = default;
        };
    }

    double responseMagnitudeDb (const DigitalResponse& response, double frequencyHz)
    {
        if (!std::isfinite (response.sampleRate) || response.sampleRate < 1000.0
            || !std::isfinite (frequencyHz) || frequencyHz < 0.0 || frequencyHz > response.sampleRate * 0.5)
            return 0.0;
        const auto z = std::polar (1.0, -2.0 * std::numbers::pi * frequencyHz / response.sampleRate);
        const auto& c = response.coefficients;
        const auto h = (c.b0 + c.b1 * z + c.b2 * z * z) / (1.0 + c.a1 * z + c.a2 * z * z);
        const auto magnitude = std::abs (h);
        return std::isfinite (magnitude) ? 20.0 * std::log10 (std::max (magnitude, 1e-12)) : 0.0;
    }

    ResponseWorker::ResponseWorker (juce::AudioProcessorValueTreeState& parameters, const std::atomic<double>& rate)
        : juce::Thread ("Open-Q response"),
          frequency (*parameters.getRawParameterValue ("bell.frequency")),
          gain (*parameters.getRawParameterValue ("bell.gain")),
          quality (*parameters.getRawParameterValue ("bell.q")),
          sampleRate (rate)
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
        TargetParameters previous;
        while (!threadShouldExit())
        {
            wait (-1);
            if (threadShouldExit())
                break;
            const auto geometryChanged = geometry.pull();
            const TargetParameters target { frequency.load (std::memory_order_relaxed), gain.load (std::memory_order_relaxed), quality.load (std::memory_order_relaxed), sampleRate.load (std::memory_order_relaxed) };
            const auto responseChanged = target != previous;
            if (!first && !geometryChanged && !responseChanged)
                continue;
            first = false;
            previous = target;
            auto& frame = frames.getWriter();
            frame.geometry = geometry.getReader();
            frame.response = { makeBellCoefficients (target.sampleRate, target.frequency, target.gain, target.quality), target.sampleRate };
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
                    const auto pointFrequency = 20.0 * std::pow (maxFrequency / 20.0, proportion);
                    const auto db = std::clamp (responseMagnitudeDb (frame.response, std::min (pointFrequency, maxFrequency)), -24.0, 24.0);
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
