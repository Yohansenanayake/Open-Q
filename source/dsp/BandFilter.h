// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "BandLimits.h"
#include "BandStatus.h"
#include "FilterCoefficients.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace openq
{
    class BandFilter
    {
    public:
        void prepare (double sampleRate, double frequencyHz, double gainDb, double q, BandStatus status = BandStatus::On, FilterType type = FilterType::Bell);
        void setParameters (double frequencyHz, double gainDb, double q, BandStatus status = BandStatus::On, FilterType type = FilterType::Bell);
        void process (juce::AudioBuffer<float>& buffer, int numChannels);
        void reset();

    private:
        void applyPendingType();
        struct State
        {
            double s1 = 0.0, s2 = 0.0;
        };

        double sampleRate = 0.0;
        std::array<State, 2> states {};
        BiquadCoefficients coefficients;
        BandStatus status = BandStatus::On;
        FilterType type = FilterType::Bell, targetType = FilterType::Bell;
        bool needsCoefficientRefresh = false;
        juce::SmoothedValue<double, juce::ValueSmoothingTypes::Multiplicative> frequency { bandLimits::defaultFrequencyHz }, quality { bandLimits::defaultQ };
        juce::SmoothedValue<double> gain { bandLimits::defaultGainDb };
        juce::SmoothedValue<double> wet { 0.0 };
    };
}
