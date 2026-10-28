// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

namespace openq::rotaryStyle
{
    // Logical pixels; UiLookAndFeel applies the region's existing UI scale.
    inline constexpr float padding = 8.0f;
    inline constexpr float trackWidth = 2.0f;
    inline constexpr float faceGap = 3.0f;
    inline constexpr float rimWidth = 1.0f;
    inline constexpr float indicatorWidth = 2.0f;
    // Indicator endpoints as proportions of the face radius.
    inline constexpr float indicatorInnerRadiusRatio = 0.38f;
    inline constexpr float indicatorOuterRadiusRatio = 0.80f;
}
