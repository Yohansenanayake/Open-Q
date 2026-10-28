// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <cmath>
#include <optional>

namespace openq
{
    class GraphRefreshLimiter
    {
    public:
        static constexpr double targetFramesPerSecond = 30.0;

        bool tick (double timestampSeconds, bool controlFeedbackPending = false)
        {
            if (!std::isfinite (timestampSeconds))
                return false;
            constexpr double roundingToleranceSeconds = 1.0e-9;
            const auto intervalSeconds = interval();
            if (lastTimestamp && std::abs (timestampSeconds - *lastTimestamp) <= roundingToleranceSeconds)
                return false;
            if (lastTimestamp && timestampSeconds < *lastTimestamp)
            {
                nextFrame.reset();
                yieldedForFeedback = false;
            }
            lastTimestamp = timestampSeconds;
            if (nextFrame && timestampSeconds + roundingToleranceSeconds < *nextFrame)
                return false;
            // Yield once for pending control pixels, but admit the following frame even under continuous input.
            if (controlFeedbackPending && !yieldedForFeedback)
            {
                yieldedForFeedback = true;
                return false;
            }
            const auto scheduledFrom = yieldedForFeedback ? timestampSeconds : nextFrame.value_or (timestampSeconds);
            yieldedForFeedback = false;
            const auto nextDeadline = scheduledFrom + intervalSeconds;
            // Skip missed frames rather than scheduling a burst after a UI stall.
            nextFrame = nextDeadline > timestampSeconds + roundingToleranceSeconds ? nextDeadline : timestampSeconds + intervalSeconds;
            return true;
        }

        void recordPaintDuration (double seconds, double completedAtSeconds)
        {
            if (!std::isfinite (seconds) || seconds < 0.0 || !std::isfinite (completedAtSeconds))
                return;
            // Reserve time for input; recover gradually after an expensive full redraw.
            constexpr double estimateDecay = 0.9, maximumSampleSeconds = 0.1;
            paintSeconds = std::max (std::min (seconds, maximumSampleSeconds), paintSeconds * estimateDecay);
            // Queueing delay and the recovery estimate cap must not shorten the input window after a paint.
            nextFrame = std::max (nextFrame.value_or (completedAtSeconds), completedAtSeconds + std::max (seconds, paintSeconds));
        }

        void reset()
        {
            paintSeconds = 0.0;
            lastTimestamp.reset();
            nextFrame.reset();
            yieldedForFeedback = false;
        }

    private:
        double interval() const
        {
            constexpr double maximumPaintTimeShare = 0.5;
            return std::max (1.0 / targetFramesPerSecond, paintSeconds / maximumPaintTimeShare);
        }
        double paintSeconds = 0.0;
        bool yieldedForFeedback = false;
        std::optional<double> lastTimestamp, nextFrame;
    };
}
