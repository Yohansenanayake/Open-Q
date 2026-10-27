// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <cmath>

namespace openq
{
    struct EditorSize
    {
        static constexpr int defaultWidth = 720, defaultHeight = 580;
        static constexpr int minimumWidth = 720, minimumHeight = 360;
        static constexpr int maximumWidth = 2160, maximumHeight = 2400;
        int width = defaultWidth, height = defaultHeight;

        static int minimumHeightForWidth (int width)
        {
            // Keep the width-scaled card and controls inside short, wide windows.
            return static_cast<int> (std::ceil (static_cast<float> (minimumHeight) * scaleForWidth (width)));
        }
        bool isValid() const
        {
            return width >= minimumWidth && width <= maximumWidth
                   && height >= minimumHeightForWidth (width) && height <= maximumHeight;
        }
        EditorSize constrained() const
        {
            const auto w = std::clamp (width, minimumWidth, maximumWidth);
            return { w, std::clamp (height, minimumHeightForWidth (w), maximumHeight) };
        }
        static float scaleForWidth (int width)
        {
            // Grow controls more slowly than the window so wide layouts gain graph space.
            return std::sqrt (static_cast<float> (width) / static_cast<float> (defaultWidth));
        }
        float scale() const { return scaleForWidth (width); }
        bool operator== (const EditorSize&) const = default;
    };
}
