// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <atomic>
#include <cstdint>
#include <juce_core/juce_core.h>

namespace openq
{
    struct ProcessingSnapshot
    {
        double hostRate = 0.0;
        int oversamplingMode = 0;
        double internalRate() const { return hostRate * (1 << oversamplingMode); }
        bool operator== (const ProcessingSnapshot& other) const
        {
            return juce::exactlyEqual (hostRate, other.hostRate) && oversamplingMode == other.oversamplingMode;
        }
    };

    // Preparation and audio publish serially; readers keep their last snapshot when busy.
    class ProcessingConfiguration
    {
    public:
        void publish (ProcessingSnapshot value)
        {
            sequence.fetch_add (1);
            hostRate.store (value.hostRate);
            mode.store (value.oversamplingMode);
            sequence.fetch_add (1);
        }
        bool tryRead (ProcessingSnapshot& result) const
        {
            const auto before = sequence.load();
            if ((before & 1u) != 0)
                return false;
            const ProcessingSnapshot value { hostRate.load(), mode.load() };
            if (before != sequence.load())
                return false;
            result = value;
            return true;
        }

    private:
        friend struct ProcessingConfigurationTestAccess;
        static_assert (std::atomic<double>::is_always_lock_free && std::atomic<std::uint64_t>::is_always_lock_free && std::atomic<int>::is_always_lock_free);
        std::atomic<std::uint64_t> sequence { 0 };
        std::atomic<double> hostRate { 0.0 };
        std::atomic<int> mode { 0 };
    };
}
