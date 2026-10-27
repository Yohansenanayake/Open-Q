// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <array>

namespace openq
{
    enum class AnalyzerQuality { Normal,
        High };
    enum class AnalyzerSmoothing { Erb,
        Octave };
    enum class AnalyzerDecay { VeryFast,
        Fast,
        Medium,
        Slow,
        VerySlow };
    inline constexpr std::array<double, 3> analyzerErbAmounts { 0.25, 0.5, 1.0 };
    inline constexpr std::array<double, 4> analyzerOctaveAmounts { 1.0 / 24.0, 1.0 / 12.0, 1.0 / 6.0, 1.0 / 3.0 };
    inline constexpr std::array<float, 5> analyzerDecaySpeeds { 4.0f, 2.0f, 1.0f, 0.5f, 0.25f };

    struct AnalyzerSettings
    {
        bool pre = true, post = true;
        AnalyzerQuality quality = AnalyzerQuality::Normal;
        AnalyzerSmoothing smoothing = AnalyzerSmoothing::Erb;
        unsigned erbAmount = 1, octaveAmount = 1;
        AnalyzerDecay decay = AnalyzerDecay::Medium;
        static constexpr unsigned flagsMask = 7, configurationMask = 255;
        constexpr unsigned flags() const { return (pre ? 1u : 0u) | (post ? 2u : 0u) | (quality == AnalyzerQuality::High ? 4u : 0u); }
        constexpr unsigned configuration() const
        {
            return static_cast<unsigned> (smoothing) | (erbAmount << 1) | (octaveAmount << 3) | (static_cast<unsigned> (decay) << 5);
        }
        constexpr unsigned encode() const { return flags() | (configuration() << 3); }
        constexpr bool isValid() const
        {
            return (quality == AnalyzerQuality::Normal || quality == AnalyzerQuality::High)
                   && (smoothing == AnalyzerSmoothing::Erb || smoothing == AnalyzerSmoothing::Octave)
                   && erbAmount < analyzerErbAmounts.size() && octaveAmount < analyzerOctaveAmounts.size()
                   && static_cast<unsigned> (decay) < analyzerDecaySpeeds.size();
        }
        static AnalyzerSettings decode (unsigned bits)
        {
            const auto config = bits >> 3;
            return { (bits & 1u) != 0, (bits & 2u) != 0, (bits & 4u) != 0 ? AnalyzerQuality::High : AnalyzerQuality::Normal, static_cast<AnalyzerSmoothing> (config & 1u), (config >> 1) & 3u, (config >> 3) & 3u, static_cast<AnalyzerDecay> ((config >> 5) & 7u) };
        }
    };
}
