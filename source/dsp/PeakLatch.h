// SPDX-License-Identifier: GPL-3.0-only
// Adapted from Grisey 5e315049a981472fca31a84efd23778e1ff2d253, Source/Engine/AtomicMax.h.
// Open-Q adds finite checks and a single-attempt reset-only publication path; see THIRD_PARTY_NOTICES.md.
#pragma once
#include <atomic>
#include <cmath>
namespace openq
{
    // One audio producer publishes finite, nonnegative peaks. The consumer only exchanges zero.
    class PeakLatch
    {
    public:
        static_assert (std::atomic<float>::is_always_lock_free);
        void publish (float peak) noexcept
        {
            if (!std::isfinite (peak) || peak < 0.0f)
                return;
            auto previous = value.load (std::memory_order_relaxed);
            if (peak > previous && !value.compare_exchange_strong (previous, peak, std::memory_order_relaxed))
                // Only the consumer can have changed it, to zero; no producer retry is needed.
                value.store (peak, std::memory_order_relaxed);
        }
        float read() noexcept { return value.exchange (0.0f, std::memory_order_relaxed); }

    private:
        std::atomic<float> value { 0.0f };
    };
}
