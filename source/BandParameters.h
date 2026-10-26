// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "dsp/BandLimits.h"
#include "dsp/BandStatus.h"
#include "dsp/CutSlope.h"
#include "dsp/FilterType.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace openq
{
    inline constexpr size_t bandCount = 24;
    inline constexpr std::array<const char*, 6> bandParameterNames { "frequency", "gain", "q", "status", "type", "slope" };

    inline juce::String bandParameterId (size_t band, const char* name)
    {
        return "band." + juce::String (band) + "." + name;
    }

    struct BandSettings
    {
        double frequency = bandLimits::defaultFrequencyHz, gain = bandLimits::defaultGainDb, q = bandLimits::defaultQ;
        BandStatus status = BandStatus::Off;
        FilterType type = FilterType::Bell;
        CutSlope slope = defaultCutSlope;
        bool operator== (const BandSettings&) const = default;
    };

    struct BandParameterPointers
    {
        std::atomic<float>* frequency = nullptr;
        std::atomic<float>* gain = nullptr;
        std::atomic<float>* q = nullptr;
        std::atomic<float>* status = nullptr;
        std::atomic<float>* type = nullptr;
        std::atomic<float>* slope = nullptr;

        BandSettings load() const
        {
            return { frequency->load (std::memory_order_relaxed), gain->load (std::memory_order_relaxed), q->load (std::memory_order_relaxed), static_cast<BandStatus> (juce::roundToInt (status->load (std::memory_order_relaxed))), static_cast<FilterType> (juce::roundToInt (type->load (std::memory_order_relaxed))), static_cast<CutSlope> (juce::roundToInt (slope->load (std::memory_order_relaxed))) };
        }
    };

    inline BandParameterPointers getBandParameterPointers (juce::AudioProcessorValueTreeState& parameters, size_t band)
    {
        return { parameters.getRawParameterValue (bandParameterId (band, "frequency")),
            parameters.getRawParameterValue (bandParameterId (band, "gain")),
            parameters.getRawParameterValue (bandParameterId (band, "q")),
            parameters.getRawParameterValue (bandParameterId (band, "status")),
            parameters.getRawParameterValue (bandParameterId (band, "type")),
            parameters.getRawParameterValue (bandParameterId (band, "slope")) };
    }
}
