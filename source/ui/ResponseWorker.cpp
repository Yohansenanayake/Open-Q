// SPDX-License-Identifier: AGPL-3.0-only
#include "ResponseWorker.h"
#include "ResponseMapping.h"
#include <complex>
#include <numbers>

namespace openq
{
    namespace
    {
        constexpr double minimumResponseMagnitude = 1e-12; // -240 dB floor avoids log10(0).

        struct TargetParameters
        {
            std::array<BandSettings, bandCount> bands;
            ProcessingSnapshot configuration;
            bool operator== (const TargetParameters& other) const
            {
                return bands == other.bands && configuration == other.configuration;
            }
        };
    }

    double responseMagnitudeDb (const DigitalResponse& response, double frequencyHz)
    {
        if (!std::isfinite (response.sampleRate) || response.sampleRate < bandLimits::minimumSampleRateHz
            || !std::isfinite (frequencyHz) || frequencyHz < 0.0 || frequencyHz > response.sampleRate * 0.5)
            return 0.0;
        const auto z = std::polar (1.0, -2.0 * std::numbers::pi * frequencyHz / response.sampleRate);
        const auto& c = response.coefficients;
        const auto h = (c.b0 + c.b1 * z + c.b2 * z * z) / (1.0 + c.a1 * z + c.a2 * z * z);
        const auto magnitude = std::abs (h);
        return std::isfinite (magnitude) ? 20.0 * std::log10 (std::max (magnitude, minimumResponseMagnitude)) : 0.0;
    }

    double responseMagnitudeDb (const BandResponse& response, double frequencyHz)
    {
        double db = 0.0;
        for (size_t i = 0; i < response.coefficients.count; ++i)
            db += responseMagnitudeDb (DigitalResponse { response.coefficients.sections[i], response.sampleRate }, frequencyHz);
        return db;
    }

    double responseMagnitudeDb (const BankResponse& response, double frequencyHz)
    {
        double db = 0.0;
        for (const auto& band : response.bands)
            db += responseMagnitudeDb (band, frequencyHz);
        return db;
    }

    ResponseWorker::ResponseWorker (juce::AudioProcessorValueTreeState& parameters, const ProcessingConfiguration& rate)
        : juce::Thread ("Open-Q response"),
          sampleRate (rate)
    {
        for (size_t i = 0; i < bandCount; ++i)
            bandParameters[i] = getBandParameterPointers (parameters, i);
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
            TargetParameters target;
            if (!sampleRate.tryRead (target.configuration))
                continue;
            const auto geometryChanged = geometry.pull();
            const auto hostRate = target.configuration.hostRate;
            const auto internalRate = target.configuration.internalRate();
            for (size_t i = 0; i < bandCount; ++i)
                target.bands[i] = bandParameters[i].load();
            const auto responseChanged = target != previous;
            if (!first && !geometryChanged && !responseChanged)
                continue;
            first = false;
            previous = target;
            auto& frame = frames.getWriter();
            frame.geometry = geometry.getReader();
            frame.response.sampleRate = internalRate;
            frame.hostSampleRate = hostRate;
            frame.settings = target.bands;
            std::array<BandResponse, bandCount> individualResponses;
            for (size_t i = 0; i < bandCount; ++i)
            {
                const auto& band = target.bands[i];
                individualResponses[i] = { makeBandCoefficients (band.type, internalRate, std::min (std::isfinite (band.frequency) ? band.frequency : bandLimits::defaultFrequencyHz, bandLimits::effectiveMaximumFrequency (hostRate)), band.gain, band.q, band.slope), internalRate };
                frame.response.bands[i] = band.status == BandStatus::On ? individualResponses[i] : BandResponse { {}, internalRate };
                frame.bandCurves[i].clear();
            }
            frame.curve.clear();
            frame.valid = frame.geometry.width > 0 && frame.geometry.height > 0
                          && std::isfinite (frame.response.sampleRate) && frame.response.sampleRate >= bandLimits::minimumSampleRateHz;
            if (frame.valid)
            {
                constexpr int points = 400;
                const auto maxFrequency = responseMapping::maximumFrequency (hostRate);
                for (int i = 0; i < points; ++i)
                {
                    if (threadShouldExit())
                        return;
                    const auto proportion = static_cast<double> (i) / static_cast<double> (points - 1);
                    const auto pointFrequency = responseMapping::proportionToFrequency (proportion, maxFrequency);
                    const auto x = static_cast<float> (proportion * frame.geometry.width);
                    double sumDb = 0.0;
                    for (size_t band = 0; band < bandCount; ++band)
                    {
                        if (target.bands[band].status == BandStatus::Off)
                            continue;
                        const auto bandDb = responseMagnitudeDb (individualResponses[band], std::min (pointFrequency, maxFrequency));
                        if (target.bands[band].status == BandStatus::On)
                            sumDb += bandDb;
                        const auto bandY = static_cast<float> (responseMapping::dbToProportion (std::clamp (bandDb, static_cast<double> (responseMapping::minimumDb), static_cast<double> (responseMapping::maximumDb))) * frame.geometry.height);
                        if (i == 0)
                            frame.bandCurves[band].startNewSubPath (x, bandY);
                        else
                            frame.bandCurves[band].lineTo (x, bandY);
                    }
                    const auto db = std::clamp (sumDb, static_cast<double> (responseMapping::minimumDb), static_cast<double> (responseMapping::maximumDb));
                    const auto y = static_cast<float> (responseMapping::dbToProportion (db) * frame.geometry.height);
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
