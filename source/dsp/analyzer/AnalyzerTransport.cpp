// SPDX-License-Identifier: AGPL-3.0-only
#include "AnalyzerTransport.h"
#include <cmath>

namespace openq
{
    AnalyzerTransport::AnalyzerTransport() : packets (std::make_unique<std::array<AnalyzerPacket, packetSlots>>()) {}

    void AnalyzerTransport::prepare (double rate)
    {
        sampleRate = rate;
        ++sequence; // Forces history reset even when a host re-prepares at the same rate.
    }

    void AnalyzerTransport::setActive (bool active)
    {
        const auto previous = captureSession.load (std::memory_order_relaxed);
        if (((previous & 1u) != 0) != active)
            captureSession.store (previous + 1u, std::memory_order_release);
    }

    void AnalyzerTransport::beginBlock (const juce::AudioBuffer<float>& buffer, int channels)
    {
        pendingCount = 0;
        const auto firstSample = sequence;
        sequence += static_cast<std::uint64_t> (buffer.getNumSamples());
        const auto token = session();
        const auto options = getSettings();
        if ((token & 1u) == 0 || (!options.pre && !options.post) || channels <= 0
            || !std::isfinite (sampleRate) || sampleRate < bandLimits::minimumSampleRateHz || sampleRate > maximumAnalyzerSampleRate)
            return;
        const auto required = buffer.getNumSamples() / AnalyzerPacket::capacity + (buffer.getNumSamples() % AnalyzerPacket::capacity != 0 ? 1 : 0);
        int start1, size1, start2, size2;
        fifo.prepareToWrite (required, start1, size1, start2, size2);
        pendingStart = start1;
        pendingCount = size1 + size2;
        for (int index = 0; index < pendingCount; ++index)
        {
            auto& packet = (*packets)[static_cast<size_t> ((pendingStart + index) % packetSlots)];
            const auto offset = index * AnalyzerPacket::capacity;
            packet.samples = std::min (AnalyzerPacket::capacity, buffer.getNumSamples() - offset);
            packet.channels = std::min ({ channels, buffer.getNumChannels(), 2 });
            packet.sampleRate = sampleRate;
            packet.session = token;
            packet.sequence = firstSample + static_cast<std::uint64_t> (offset);
            for (int channel = 0; channel < packet.channels; ++channel)
                std::copy_n (buffer.getReadPointer (channel, offset), packet.samples, packet.pre[static_cast<size_t> (channel)].begin());
        }
    }

    void AnalyzerTransport::endBlock (const juce::AudioBuffer<float>& buffer)
    {
        for (int index = 0; index < pendingCount; ++index)
        {
            auto& packet = (*packets)[static_cast<size_t> ((pendingStart + index) % packetSlots)];
            for (int channel = 0; channel < packet.channels; ++channel)
                std::copy_n (buffer.getReadPointer (channel, index * AnalyzerPacket::capacity), packet.samples, packet.post[static_cast<size_t> (channel)].begin());
        }
        fifo.finishedWrite (pendingCount);
        pendingCount = 0;
    }

    bool AnalyzerTransport::read (AnalyzerPacket& packet)
    {
        int start1, size1, start2, size2;
        fifo.prepareToRead (1, start1, size1, start2, size2);
        if (size1 + size2 == 0)
            return false;
        packet = (*packets)[static_cast<size_t> (start1)];
        fifo.finishedRead (1);
        return true;
    }
}
