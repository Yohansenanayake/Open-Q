// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "PeakLatch.h"
#include <array>
#include <cstdint>
namespace openq
{
    class LevelMeterTransport
    {
    public:
        struct Reading
        {
            std::array<std::array<float, 2>, 2> peaks {};
            int channels = 2;
            std::uint64_t generation = 0;
        };
        // prepare and publish belong to the audio/processing owner, never run concurrently.
        void prepare (int count) noexcept
        {
            for (auto& stage : peaks)
                for (auto& channel : stage)
                    (void) channel.read();
            channels.store (count, std::memory_order_relaxed);
            generation.fetch_add (1, std::memory_order_release);
        }
        void publish (const std::array<float, 2>& input, const std::array<float, 2>& output) noexcept
        {
            for (size_t channel = 0; channel < input.size(); ++channel)
            {
                peaks[0][channel].publish (input[channel]);
                peaks[1][channel].publish (output[channel]);
            }
        }
        // Exactly one UI consumer polls and distributes both stages' readings.
        Reading read() noexcept
        {
            Reading result;
            result.generation = generation.load (std::memory_order_acquire);
            result.channels = channels.load (std::memory_order_relaxed);
            for (size_t stage = 0; stage < peaks.size(); ++stage)
                for (size_t channel = 0; channel < peaks[stage].size(); ++channel)
                    result.peaks[stage][channel] = peaks[stage][channel].read();
            return result;
        }

    private:
        std::array<std::array<PeakLatch, 2>, 2> peaks;
        std::atomic<int> channels { 2 };
        std::atomic<std::uint64_t> generation { 0 };
    };
}
