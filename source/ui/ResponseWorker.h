// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "../dsp/BellCoefficients.h"
#include "../utility/TripleBuffer.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace openq
{
    struct DigitalResponse
    {
        BiquadCoefficients coefficients;
        double sampleRate = 0.0;
    };

    double responseMagnitudeDb (const DigitalResponse& response, double frequencyHz);

    struct ResponseGeometry
    {
        int width = 0, height = 0;
        bool operator== (const ResponseGeometry&) const = default;
    };

    struct ResponseFrame
    {
        juce::Path curve;
        DigitalResponse response;
        ResponseGeometry geometry;
        bool valid = false;
    };

    class ResponseWorker : private juce::Thread
    {
    public:
        ResponseWorker (juce::AudioProcessorValueTreeState& parameters, const std::atomic<double>& sampleRate);
        ~ResponseWorker() override;
        bool start();
        void stop();
        void requestUpdate();
        void setGeometry (ResponseGeometry geometry);
        bool pullFrame();
        const ResponseFrame& getFrame() const;

    private:
        void run() override;
        const std::atomic<float>& frequency;
        const std::atomic<float>& gain;
        const std::atomic<float>& quality;
        const std::atomic<double>& sampleRate;
        TripleBuffer<ResponseGeometry> geometry;
        TripleBuffer<ResponseFrame> frames;
    };
}
