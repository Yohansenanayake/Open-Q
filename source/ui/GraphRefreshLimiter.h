// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
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
            constexpr double intervalSeconds = 1.0 / targetFramesPerSecond;
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

        void reset()
        {
            lastTimestamp.reset();
            nextFrame.reset();
        }

    private:
        std::optional<double> lastTimestamp, nextFrame;
    };
}
