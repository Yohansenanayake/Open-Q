// SPDX-License-Identifier: AGPL-3.0-only
// Offline comparison; calls the production Open-Q and pinned JUCE designs directly.
#include "dsp/BandLimits.h"
#include "dsp/FilterCoefficients.h"
#include <complex>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <juce_dsp/juce_dsp.h>
#include <numbers>
#include <stdexcept>

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr double nyquist = sampleRate / 2.0;
    constexpr double minimumRelativeFrequency = 0.001;
    constexpr int frequencyIntervals = 1024;
    constexpr double quality = 1.0;
    constexpr std::array centres { 0.05, 0.5, 0.8 };
    constexpr std::array gains { 12.0, -12.0 };
    using Coefficients = openq::BiquadCoefficients;
    using Complex = std::complex<double>;

    void check (bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error (message);
    }

    double analogueDb (double frequencyRatio, double gainDb)
    {
        // Midpoint-Q bell: H(s) = (s² + A*s/Q + 1)/(s² + s/(A*Q) + 1).
        const auto a = std::pow (10.0, gainDb / 40.0);
        const auto real = 1.0 - frequencyRatio * frequencyRatio;
        return 20.0 * std::log10 (std::abs (Complex (real, frequencyRatio * a / quality) / Complex (real, frequencyRatio / (a * quality))));
    }

    Coefficients bltBell (double frequencyHz, double gainDb)
    {
        // Apply s = cot(pi*f0/fs)*(1-z^-1)/(1+z^-1) to the analogue prototype.
        const auto k = 1.0 / std::tan (std::numbers::pi * frequencyHz / sampleRate);
        const auto a = std::pow (10.0, gainDb / 40.0);
        const auto numeratorDamping = k * a / quality;
        const auto denominatorDamping = k / (a * quality);
        const auto a0 = k * k + denominatorDamping + 1.0;
        return { (k * k + numeratorDamping + 1.0) / a0,
            2.0 * (1.0 - k * k) / a0,
            (k * k - numeratorDamping + 1.0) / a0,
            2.0 * (1.0 - k * k) / a0,
            (k * k - denominatorDamping + 1.0) / a0 };
    }

    Coefficients juceBell (double frequencyHz, double gainDb)
    {
        const auto c = juce::dsp::IIR::ArrayCoefficients<double>::makePeakFilter (
            sampleRate, frequencyHz, quality, std::pow (10.0, gainDb / 20.0));
        return { c[0] / c[3], c[1] / c[3], c[2] / c[3], c[4] / c[3], c[5] / c[3] };
    }

    Coefficients openqBell (double frequencyHz, double gainDb)
    {
        return openq::makeBandCoefficients (openq::FilterType::Bell,
            sampleRate,
            frequencyHz,
            gainDb,
            quality)
            .sections[0];
    }

    double digitalDb (const Coefficients& c, double relativeFrequency)
    {
        const auto z = std::polar (1.0, -std::numbers::pi * relativeFrequency);
        return 20.0 * std::log10 (std::abs ((c.b0 + c.b1 * z + c.b2 * z * z) / (1.0 + c.a1 * z + c.a2 * z * z)));
    }

    void checkStable (const Coefficients& c)
    {
        for (const auto value : { c.b0, c.b1, c.b2, c.a1, c.a2 })
            check (std::isfinite (value), "Non-finite coefficient");
        check (std::abs (c.a2) < 1.0 && 1.0 + c.a1 + c.a2 > 0.0
                   && 1.0 - c.a1 + c.a2 > 0.0,
            "Unstable denominator");
    }

    void exportCurves (std::ostream& output)
    {
        output << "case_id,sample_rate_hz,centre_nyquist,centre_hz,gain_db,q,frequency_nyquist,frequency_hz,analogue_db,openq_db,juce_db,blt_db\n"
               << std::setprecision (17);
        int caseIndex = 0;
        for (const auto gain : gains)
        {
            for (const auto centre : centres)
            {
                const auto centreHz = centre * nyquist;
                check (centreHz >= openq::bandLimits::minimumFrequencyHz
                           && centreHz <= openq::bandLimits::effectiveMaximumFrequency (sampleRate),
                    "Comparison centre would be clamped by Open-Q");
                const std::array filters { openqBell (centreHz, gain), juceBell (centreHz, gain), bltBell (centreHz, gain) };
                const auto opposite = openqBell (centreHz, -gain);
                for (const auto& filter : filters)
                {
                    checkStable (filter);
                    check (std::abs (digitalDb (filter, centre) - gain) < 1e-6, "Centre gain mismatch");
                }
                check (std::abs (analogueDb (1.0, gain) - gain) < 1e-10, "Analogue centre mismatch");
                check (std::abs (digitalDb (openqBell (centreHz, 0.0), centre)) < 1e-12, "Unity mismatch");
                check (std::abs (digitalDb (filters[1], 0.0)) < 1e-8
                           && std::abs (digitalDb (filters[1], 1.0)) < 1e-8,
                    "BLT endpoint mismatch");
                std::vector<double> frequencies;
                for (int point = 0; point <= frequencyIntervals; ++point)
                    frequencies.push_back (std::exp (std::log (minimumRelativeFrequency)
                                                     * (1.0 - static_cast<double> (point) / frequencyIntervals)));
                frequencies.push_back (centre);
                std::sort (frequencies.begin(), frequencies.end());
                frequencies.erase (std::unique (frequencies.begin(), frequencies.end()), frequencies.end());
                for (const auto frequency : frequencies)
                {
                    const auto analogue = analogueDb (frequency / centre, gain);
                    const auto openq = digitalDb (filters[0], frequency);
                    const auto juce = digitalDb (filters[1], frequency);
                    const auto blt = digitalDb (filters[2], frequency);
                    check (std::isfinite (analogue) && std::isfinite (openq)
                               && std::isfinite (juce) && std::isfinite (blt),
                        "Non-finite response");
                    check (std::abs (juce - blt) < 1e-8, "JUCE and equivalent BLT disagree");
                    check (std::abs (openq + digitalDb (opposite, frequency)) < 1e-8,
                        "Open-Q boost/cut cancellation mismatch");
                    output << caseIndex << ',' << sampleRate << ',' << centre << ',' << centreHz << ',' << gain << ','
                           << quality << ',' << frequency << ',' << frequency * nyquist << ',' << analogue << ','
                           << openq << ',' << juce << ',' << blt << '\n';
                }
                ++caseIndex;
            }
        }
        check (output.good(), "Failed to write comparison CSV");
    }

    struct AnalogueSection
    {
        std::array<double, 3> b, a; // Ascending powers of s = j*f/f0.
    };

    Coefficients bilinear (const AnalogueSection& section, double centreHz)
    {
        const auto k = 1.0 / std::tan (std::numbers::pi * centreHz / sampleRate);
        const auto& b = section.b;
        const auto& a = section.a;
        if (juce::exactlyEqual (a[2], 0.0) && juce::exactlyEqual (b[2], 0.0))
        {
            const auto a0 = a[0] + a[1] * k;
            return { (b[0] + b[1] * k) / a0, (b[0] - b[1] * k) / a0, 0.0, (a[0] - a[1] * k) / a0, 0.0 };
        }
        const auto a0 = a[0] + a[1] * k + a[2] * k * k;
        return { (b[0] + b[1] * k + b[2] * k * k) / a0,
            2.0 * (b[0] - b[2] * k * k) / a0,
            (b[0] - b[1] * k + b[2] * k * k) / a0,
            2.0 * (a[0] - a[2] * k * k) / a0,
            (a[0] - a[1] * k + a[2] * k * k) / a0 };
    }

    Coefficients normalize (const std::array<double, 6>& c)
    {
        return { c[0] / c[3], c[1] / c[3], c[2] / c[3], c[4] / c[3], c[5] / c[3] };
    }

    std::vector<AnalogueSection> prototypes (openq::FilterType type, size_t order, double gain)
    {
        if (!openq::isCut (type))
        {
            const auto a = std::pow (10.0, gain / 40.0);
            const auto damping = std::sqrt (a) / quality;
            if (type == openq::FilterType::LowShelf)
                return { { { a * a, a * damping, a }, { 1.0, damping, a } } };
            return { { { a, a * damping, a * a }, { a, damping, 1.0 } } };
        }
        const bool highPass = type == openq::FilterType::LowCut;
        std::vector<AnalogueSection> result;
        if (order % 2 != 0)
            result.push_back ({ highPass ? std::array { 0.0, 1.0, 0.0 } : std::array { 1.0, 0.0, 0.0 }, { 1.0, 1.0, 0.0 } });
        for (size_t section = 0; section < order / 2; ++section)
        {
            // Butterworth pole pairs, including Q=1 for the third-order pair.
            const auto q = order == 3 ? 1.0
                                      : 1.0 / (2.0 * std::cos (std::numbers::pi * static_cast<double> (2 * section + 1) / static_cast<double> (2 * order)));
            result.push_back ({ highPass ? std::array { 0.0, 0.0, 1.0 } : std::array { 1.0, 0.0, 0.0 }, { 1.0, 1.0 / q, 1.0 } });
        }
        return result;
    }

    double analogueCascadeDb (const std::vector<AnalogueSection>& sections, double ratio)
    {
        const Complex s (0.0, ratio);
        double result = 0.0;
        for (const auto& section : sections)
            result += 20.0 * std::log10 (std::abs ((section.b[0] + section.b[1] * s + section.b[2] * s * s) / (section.a[0] + section.a[1] * s + section.a[2] * s * s)));
        return result;
    }

    double digitalCascadeDb (const std::vector<Coefficients>& sections, double frequency)
    {
        const auto z = juce::exactlyEqual (frequency, 1.0) ? Complex (-1.0, 0.0) : std::polar (1.0, -std::numbers::pi * frequency);
        double result = 0.0;
        for (const auto& c : sections)
        {
            auto numerator = c.b0 + c.b1 * z + c.b2 * z * z;
            // Factor known zeros to avoid cancellation in deep cut stopbands.
            if (juce::exactlyEqual (c.b0, c.b2) && juce::exactlyEqual (c.b1, 2.0 * c.b0))
                numerator = c.b0 * (1.0 + z) * (1.0 + z);
            else if (juce::exactlyEqual (c.b0, c.b2) && juce::exactlyEqual (c.b1, -2.0 * c.b0))
                numerator = c.b0 * (1.0 - z) * (1.0 - z);
            else if (juce::exactlyEqual (c.b2, 0.0) && juce::exactlyEqual (c.b0, c.b1))
                numerator = c.b0 * (1.0 + z);
            result += 20.0 * std::log10 (std::abs (numerator / (1.0 + c.a1 * z + c.a2 * z * z)));
        }
        return result;
    }

    std::vector<Coefficients> juceStages (openq::FilterType type, const std::vector<AnalogueSection>& analogue, double centreHz, double gain)
    {
        using Designer = juce::dsp::IIR::ArrayCoefficients<double>;
        if (!openq::isCut (type))
        {
            const auto g = std::pow (10.0, gain / 20.0);
            return { normalize (type == openq::FilterType::LowShelf ? Designer::makeLowShelf (sampleRate, centreHz, quality, g)
                                                                    : Designer::makeHighShelf (sampleRate, centreHz, quality, g)) };
        }
        std::vector<Coefficients> result;
        for (const auto& section : analogue)
        {
            const bool highPass = type == openq::FilterType::LowCut;
            if (juce::exactlyEqual (section.a[2], 0.0))
            {
                const auto c = highPass ? Designer::makeFirstOrderHighPass (sampleRate, centreHz)
                                        : Designer::makeFirstOrderLowPass (sampleRate, centreHz);
                result.push_back ({ c[0] / c[2], c[1] / c[2], 0.0, c[3] / c[2], 0.0 });
            }
            else
            {
                const auto q = 1.0 / section.a[1];
                result.push_back (normalize (highPass ? Designer::makeHighPass (sampleRate, centreHz, q)
                                                      : Designer::makeLowPass (sampleRate, centreHz, q)));
            }
        }
        return result;
    }

    void exportFamily (const std::filesystem::path& path, openq::FilterType type, size_t slopeIndex)
    {
        const bool cut = openq::isCut (type);
        const auto order = cut ? openq::cutOrders[slopeIndex] : 2;
        const auto q = cut ? openq::bandLimits::defaultCutQ : quality;
        std::ofstream output (path);
        check (output.is_open(), "Cannot open family CSV");
        output << "case_id,sample_rate_hz,centre_nyquist,centre_hz,gain_db,q,frequency_nyquist,frequency_hz,analogue_db,openq_db,juce_db,blt_db,filter_type,slope_db_oct\n"
               << std::setprecision (17);
        int caseIndex = 0;
        for (const auto gain : (cut ? std::vector { 0.0 } : std::vector<double> (gains.begin(), gains.end())))
        {
            for (const auto centre : centres)
            {
                const auto centreHz = centre * nyquist;
                check (centreHz >= openq::bandLimits::minimumFrequencyHz
                           && centreHz <= openq::bandLimits::effectiveMaximumFrequency (sampleRate),
                    "Centre would be clamped");
                const auto analogue = prototypes (type, order, gain);
                const auto applied = openq::makeBandCoefficients (type, sampleRate, centreHz, gain, q, static_cast<openq::CutSlope> (slopeIndex));
                const std::vector<Coefficients> openq (applied.sections.begin(), applied.sections.begin() + static_cast<std::ptrdiff_t> (applied.count));
                check (applied.count == analogue.size(), "Open-Q stage count does not match the reference order");
                const auto juce = juceStages (type, analogue, centreHz, gain);
                std::vector<Coefficients> blt;
                for (const auto& section : analogue)
                    blt.push_back (bilinear (section, centreHz));
                for (const auto* chain : std::array<const std::vector<Coefficients>*, 3> { &openq, &juce, &blt })
                    for (const auto& section : *chain)
                        checkStable (section);
                const auto expectedCentre = cut ? -10.0 * std::log10 (2.0) : gain / 2.0;
                check (std::abs (analogueCascadeDb (analogue, 1.0) - expectedCentre) < 1e-8, "Prototype centre mismatch");
                check (std::abs (digitalCascadeDb (juce, centre) - expectedCentre) < 1e-8, "JUCE centre mismatch");
                check (std::abs (digitalCascadeDb (openq, centre) - expectedCentre) < 1e-6, "Open-Q centre mismatch");
                if (!cut)
                    check (std::abs (digitalCascadeDb (openq, 0.0) - (type == openq::FilterType::LowShelf ? gain : 0.0)) < 1e-8,
                        "Open-Q shelf DC mismatch");
                std::vector<double> frequencies;
                for (int point = 0; point <= frequencyIntervals; ++point)
                    frequencies.push_back (std::exp (std::log (minimumRelativeFrequency)
                                                     * (1.0 - static_cast<double> (point) / frequencyIntervals)));
                frequencies.push_back (centre);
                std::sort (frequencies.begin(), frequencies.end());
                frequencies.erase (std::unique (frequencies.begin(), frequencies.end()), frequencies.end());
                for (const auto frequency : frequencies)
                {
                    const auto a = analogueCascadeDb (analogue, frequency / centre);
                    const auto o = digitalCascadeDb (openq, frequency);
                    const auto j = digitalCascadeDb (juce, frequency);
                    const auto b = digitalCascadeDb (blt, frequency);
                    check (std::isfinite (a) && std::isfinite (o), "Invalid analogue/Open-Q response");
                    if (type == openq::FilterType::HighCut && juce::exactlyEqual (frequency, 1.0))
                        check (std::isinf (j) && std::signbit (j) && std::isinf (b) && std::signbit (b), "Expected exact low-pass zero at Nyquist");
                    else
                        check (std::isfinite (j) && std::isfinite (b) && std::abs (j - b) < 1e-6, "JUCE and BLT disagree");
                    if (!cut)
                    {
                        const auto reciprocal = openq::makeBandCoefficients (type, sampleRate, centreHz, -gain, q);
                        check (std::abs (o + digitalCascadeDb ({ reciprocal.sections[0] }, frequency)) < 1e-8, "Shelf boost/cut mismatch");
                    }
                    output << caseIndex << ',' << sampleRate << ',' << centre << ',' << centreHz << ',' << gain << ',' << q << ','
                           << frequency << ',' << frequency * nyquist << ',' << a << ',' << o << ',' << j << ',' << b << ','
                           << static_cast<int> (type) << ',' << (cut ? order * 6 : 0) << '\n';
                }
                ++caseIndex;
            }
        }
        output.close();
        check (!output.fail(), "Cannot finish family CSV");
    }

    void exportOtherFilters (const std::filesystem::path& directory)
    {
        std::filesystem::create_directories (directory);
        exportFamily (directory / "low-shelf-curves.csv", openq::FilterType::LowShelf, 0);
        exportFamily (directory / "high-shelf-curves.csv", openq::FilterType::HighShelf, 0);
        for (size_t slope = 0; slope < openq::cutOrders.size(); ++slope)
        {
            const auto suffix = "-" + std::to_string (openq::cutOrders[slope] * 6) + "db-curves.csv";
            exportFamily (directory / ("low-cut" + suffix), openq::FilterType::LowCut, slope);
            exportFamily (directory / ("high-cut" + suffix), openq::FilterType::HighCut, slope);
        }
        std::cout << "Exported two shelf families and all eight slopes for both cuts; numerical checks passed.\n";
    }

}

int main (int argc, char** argv)
{
    try
    {
        if (argc == 3 && std::string_view (argv[1]) == "--other-filters")
        {
            exportOtherFilters (argv[2]);
            return 0;
        }
        check (argc == 2, "Usage: EqComparison output.csv | --other-filters output-directory");
        std::ofstream output (argv[1]);
        check (output.is_open(), "Cannot open output CSV");
        exportCurves (output);
        output.close();
        check (!output.fail(), "Cannot finish output CSV");
        std::cout << "Exported six bell cases; stability, centre gain, unity, boost/cut cancellation and JUCE/BLT agreement checks passed.\n";
        return 0;
    } catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
