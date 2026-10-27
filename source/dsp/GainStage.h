// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "../PluginParameters.h"
#include <cmath>
#include <juce_audio_basics/juce_audio_basics.h>
#include <limits>
namespace openq
{
    class GainStage
    {
    public:
        void prepare (double rate, float db)
        {
            gain.reset (std::isfinite (rate) && rate > 0.0 ? rate : bandLimits::minimumSampleRateHz, gainLimits::smoothingSeconds);
            targetDb = sanitize (db);
            gain.setCurrentAndTargetValue (linear (targetDb));
        }
        void setGainDb (float db) noexcept
        {
            db = sanitize (db);
            if (!juce::exactlyEqual (targetDb, db))
            {
                targetDb = db;
                gain.setTargetValue (linear (db));
            }
        }
        std::array<float, 2> process (juce::AudioBuffer<float>& buffer, int channels) noexcept
        {
            std::array<float, 2> peaks {};
            channels = std::clamp (channels, 0, std::min (2, buffer.getNumChannels()));
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            {
                const auto multiplier = gain.getNextValue();
                for (int channel = 0; channel < channels; ++channel)
                {
                    const auto scaled = static_cast<double> (buffer.getSample (channel, sample)) * multiplier;
                    const auto output = std::isfinite (scaled) && std::abs (scaled) <= std::numeric_limits<float>::max() ? static_cast<float> (scaled) : 0.0f;
                    buffer.setSample (channel, sample, output);
                    peaks[static_cast<size_t> (channel)] = std::max (peaks[static_cast<size_t> (channel)], std::abs (output));
                }
            }
            return peaks;
        }

    private:
        static float sanitize (float db) noexcept
        {
            return std::isfinite (db) ? std::clamp (db, gainLimits::minimumDb, gainLimits::maximumDb) : gainLimits::defaultDb;
        }
        static double linear (float db) noexcept
        {
            return juce::Decibels::decibelsToGain (static_cast<double> (db));
        }
        float targetDb = gainLimits::defaultDb;
        juce::SmoothedValue<double, juce::ValueSmoothingTypes::Linear> gain { 1.0 };
    };
}
