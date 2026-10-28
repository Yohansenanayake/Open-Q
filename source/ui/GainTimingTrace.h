// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

namespace openq
{
    // Message-thread-only diagnostics; no logging or allocation while capturing.
    class GainTimingTrace
    {
    public:
        static constexpr size_t capacity = 32768;
        static GainTimingTrace& instance()
        {
            static GainTimingTrace trace;
            return trace;
        }
        void start()
        {
            count = dropped = 0;
            origin = juce::Time::getMillisecondCounterHiRes();
            recording = true;
        }
        void stop() { recording = false; }
        bool isRecording() const { return recording; }
        void record (const char* event, int control = -1, double value = 0.0, juce::int64 sourceTime = 0)
        {
            if (!recording)
                return;
            if (count == capacity)
            {
                ++dropped;
                return;
            }
            entries[count++] = { juce::Time::getMillisecondCounterHiRes() - origin, event, control, value, sourceTime };
        }
        juce::String toCsv() const
        {
            jassert (!recording);
            juce::MemoryOutputStream csv (count * 128 + 128);
            csv << "elapsed_ms,event,control,value,source_event_ms\n";
            for (size_t i = 0; i < count; ++i)
            {
                const auto& e = entries[i];
                csv << juce::String (e.milliseconds, 6) << "," << e.event << "," << e.control << ","
                    << juce::String (e.value, 9) << "," << e.sourceTime << "\n";
            }
            csv << "# dropped_events," << static_cast<juce::int64> (dropped) << "\n";
            return csv.toUTF8();
        }
    private:
        struct Entry
        {
            double milliseconds = 0.0;
            const char* event = nullptr;
            int control = -1;
            double value = 0.0;
            juce::int64 sourceTime = 0;
        };
        std::array<Entry, capacity> entries;
        size_t count = 0, dropped = 0;
        double origin = 0.0;
        bool recording = false;
    };
}
