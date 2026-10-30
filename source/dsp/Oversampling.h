// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_dsp/juce_dsp.h>

namespace openq
{
    inline constexpr int oversamplingModeCount = 3;
    inline constexpr int processingChunkSamples = 256;

    class Oversampling
    {
    public:
        Oversampling()
        {
            for (auto& converter : converters)
                converter.initProcessing (processingChunkSamples);
            for (int mode = 1; mode < oversamplingModeCount; ++mode)
                latencies[static_cast<size_t> (mode)] = juce::roundToInt (converters[static_cast<size_t> (mode - 1)].getLatencyInSamples());
            dryHistory.resize (static_cast<size_t> (*std::max_element (latencies.begin(), latencies.end()) + 1));
        }

        int latency (int mode) const { return latencies[static_cast<size_t> (mode)]; }
        void reset()
        {
            for (auto& converter : converters)
                converter.reset();
            std::fill (dryHistory.begin(), dryHistory.end(), std::array<float, 2> {});
            dryPosition = 0;
        }

        void captureDry (const juce::AudioBuffer<float>& input, int channels, int mode, std::array<std::array<float, processingChunkSamples>, 2>& dry)
        {
            if (mode == 0)
            {
                for (int channel = 0; channel < channels; ++channel)
                    std::copy_n (input.getReadPointer (channel), input.getNumSamples(), dry[static_cast<size_t> (channel)].begin());
                return;
            }
            const auto delay = static_cast<size_t> (latency (mode));
            for (int sample = 0; sample < input.getNumSamples(); ++sample)
            {
                for (int channel = 0; channel < channels; ++channel)
                    dryHistory[dryPosition][static_cast<size_t> (channel)] = input.getSample (channel, sample);
                const auto read = (dryPosition + dryHistory.size() - delay) % dryHistory.size();
                for (int channel = 0; channel < channels; ++channel)
                    dry[static_cast<size_t> (channel)][static_cast<size_t> (sample)] = dryHistory[read][static_cast<size_t> (channel)];
                dryPosition = (dryPosition + 1) % dryHistory.size();
            }
        }

        juce::dsp::AudioBlock<float> up (const juce::dsp::AudioBlock<const float>& input, int mode)
        {
            if (input.getNumChannels() == 1)
            {
                const std::array<const float*, 2> pointers { input.getChannelPointer (0), silence.data() };
                return converters[static_cast<size_t> (mode - 1)].processSamplesUp ({ pointers.data(), 2, input.getNumSamples() });
            }
            return converters[static_cast<size_t> (mode - 1)].processSamplesUp (input);
        }
        void down (juce::dsp::AudioBlock<float>& output, int mode)
        {
            if (output.getNumChannels() == 1)
            {
                // JUCE's integer-delay stage requires the prepared two-channel layout.
                std::array<float*, 2> pointers { output.getChannelPointer (0), discardedOutput.data() };
                juce::dsp::AudioBlock<float> stereo (pointers.data(), 2, output.getNumSamples());
                converters[static_cast<size_t> (mode - 1)].processSamplesDown (stereo);
                return;
            }
            converters[static_cast<size_t> (mode - 1)].processSamplesDown (output);
        }

    private:
        using Converter = juce::dsp::Oversampling<float>;
        std::array<Converter, 2> converters {
            Converter { 2, 1, Converter::filterHalfBandPolyphaseIIR, true, true },
            Converter { 2, 2, Converter::filterHalfBandPolyphaseIIR, true, true }
        };
        std::array<int, oversamplingModeCount> latencies {};
        std::vector<std::array<float, 2>> dryHistory;
        std::array<float, processingChunkSamples> silence {}, discardedOutput {};
        size_t dryPosition = 0;
    };
}
