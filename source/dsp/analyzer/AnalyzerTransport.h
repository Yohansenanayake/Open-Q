// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "../BandLimits.h"
#include "AnalyzerSettings.h"
#include <atomic>
#include <juce_audio_basics/juce_audio_basics.h>

namespace openq
{
    inline constexpr double maximumAnalyzerSampleRate = 768000.0;
    struct AnalyzerPacket
    {
        static constexpr int capacity = 256;
        std::array<std::array<float, capacity>, 2> pre {}, post {};
        double sampleRate = 0.0;
        std::uint64_t sequence = 0;
        unsigned session = 0;
        int channels = 0, samples = 0;
    };

    // One audio producer, one editor-worker consumer. Storage never changes during preparation.
    class AnalyzerTransport
    {
    public:
        static constexpr int packetSlots = 257; // One spare slot: 65536 samples of usable capture.
        static_assert (std::atomic<unsigned>::is_always_lock_free);
        AnalyzerTransport();
        void prepare (double rate);
        void setActive (bool active);
        unsigned session() const { return captureSession.load (std::memory_order_acquire); }
        AnalyzerSettings getSettings() const { return AnalyzerSettings::decode (settings.load (std::memory_order_relaxed)); }
        bool setSettings (AnalyzerSettings value)
        {
            if (!value.isValid())
                return false;
            settings.store (value.encode(), std::memory_order_relaxed);
            return true;
        }
        bool getFillEnabled() const { return fillEnabled.load (std::memory_order_relaxed) != 0; }
        void setFillEnabled (bool enabled) { fillEnabled.store (enabled ? 1u : 0u, std::memory_order_relaxed); }
        void beginBlock (const juce::AudioBuffer<float>& buffer, int channels);
        void endBlock (const juce::AudioBuffer<float>& buffer);
        bool read (AnalyzerPacket& packet);

    private:
        std::unique_ptr<std::array<AnalyzerPacket, packetSlots>> packets;
        juce::AbstractFifo fifo { packetSlots };
        std::atomic<unsigned> captureSession { 0 }, settings { AnalyzerSettings {}.encode() };
        std::atomic<unsigned> fillEnabled { 1 };
        double sampleRate = 0.0;
        std::uint64_t sequence = 0;
        int pendingStart = 0, pendingCount = 0;
    };
}
