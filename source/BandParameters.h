// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace openq
{
    inline constexpr size_t bandCount = 8;

    inline juce::String bandParameterId (size_t band, const char* name)
    {
        return "band." + juce::String (band) + "." + name;
    }

    struct BandSettings
    {
        double frequency = 1000.0, gain = 0.0, q = 1.0;
        bool enabled = false;
        bool operator== (const BandSettings&) const = default;
    };

    struct BandParameterPointers
    {
        std::atomic<float>* frequency = nullptr;
        std::atomic<float>* gain = nullptr;
        std::atomic<float>* q = nullptr;
        std::atomic<float>* enabled = nullptr;

        BandSettings load() const
        {
            return { frequency->load (std::memory_order_relaxed), gain->load (std::memory_order_relaxed), q->load (std::memory_order_relaxed), enabled->load (std::memory_order_relaxed) > 0.5f };
        }
    };

    inline BandParameterPointers getBandParameterPointers (juce::AudioProcessorValueTreeState& parameters, size_t band)
    {
        return { parameters.getRawParameterValue (bandParameterId (band, "frequency")),
            parameters.getRawParameterValue (bandParameterId (band, "gain")),
            parameters.getRawParameterValue (bandParameterId (band, "q")),
            parameters.getRawParameterValue (bandParameterId (band, "enabled")) };
    }
}
