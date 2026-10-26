// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "BandStatus.h"
#include "BellCoefficients.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace openq
{
    class BellFilter
    {
    public:
        void prepare (double sampleRate, double frequencyHz, double gainDb, double q, BandStatus status = BandStatus::On);
        void setParameters (double frequencyHz, double gainDb, double q, BandStatus status = BandStatus::On);
        void process (juce::AudioBuffer<float>& buffer, int numChannels);
        void reset();

    private:
        struct State
        {
            double s1 = 0.0, s2 = 0.0;
        };

        double sampleRate = 0.0;
        std::array<State, 2> states {};
        BiquadCoefficients coefficients;
        BandStatus status = BandStatus::On;
        bool needsCoefficientRefresh = false;
        juce::SmoothedValue<double, juce::ValueSmoothingTypes::Multiplicative> frequency { 1000.0 }, quality { 1.0 };
        juce::SmoothedValue<double> gain { 0.0 };
        juce::SmoothedValue<double> wet { 0.0 };
    };
}
