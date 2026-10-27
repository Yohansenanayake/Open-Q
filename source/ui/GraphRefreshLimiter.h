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

        bool tick (double timestampSeconds)
        {
            if (!std::isfinite (timestampSeconds))
                return false;
            constexpr double roundingToleranceSeconds = 1.0e-9;
            const auto intervalSeconds = interval();
            if (lastTimestamp && timestampSeconds < *lastTimestamp)
                nextFrame.reset();
            lastTimestamp = timestampSeconds;
            if (nextFrame && timestampSeconds + roundingToleranceSeconds < *nextFrame)
                return false;
            const auto nextDeadline = nextFrame.value_or (timestampSeconds) + intervalSeconds;
            // Skip missed frames rather than scheduling a burst after a UI stall.
            nextFrame = nextDeadline > timestampSeconds + roundingToleranceSeconds ? nextDeadline : timestampSeconds + intervalSeconds;
            return true;
        }

        void recordPaintDuration (double seconds)
        {
            if (!std::isfinite (seconds) || seconds < 0.0)
                return;
            // Reserve time for input; recover gradually after an expensive full redraw.
            constexpr double estimateDecay = 0.9, maximumSampleSeconds = 0.1;
            paintSeconds = std::max (std::min (seconds, maximumSampleSeconds), paintSeconds * estimateDecay);
            if (lastTimestamp && nextFrame && interval() > 1.0 / targetFramesPerSecond)
                nextFrame = std::max (*nextFrame, *lastTimestamp + interval());
        }

        void reset()
        {
            paintSeconds = 0.0;
            lastTimestamp.reset();
            nextFrame.reset();
        }

    private:
        double interval() const
        {
            constexpr double maximumPaintTimeShare = 0.5;
            return std::max (1.0 / targetFramesPerSecond, paintSeconds / maximumPaintTimeShare);
        }
        double paintSeconds = 0.0;
        std::optional<double> lastTimestamp, nextFrame;
    };
}
