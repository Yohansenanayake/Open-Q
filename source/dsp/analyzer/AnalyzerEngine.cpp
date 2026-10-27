// SPDX-License-Identifier: AGPL-3.0-only
// Pipeline follows ZLEqualizer 3ce598ee065b3d3847121e76e196feda7ed8f56d (C) 2026 zsliu98.
// Open-Q: channel-averaged power, sample-tagged history and calibrated peak-sine scaling.
#include "AnalyzerEngine.h"
#include "reference/SpectrumBlender.h"
#include "reference/SpectrumSmoother.h"
#include <numbers>
// Upstream half-float comparisons and integer extensions are intentional.
JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wfloat-equal", "-Wpedantic")
#include "reference/SpectrumDecayer.h"
#include <hwy/aligned_allocator.h>
#include <zldsp_fft_rfft.hpp>
JUCE_END_IGNORE_WARNINGS_GCC_LIKE

namespace openq
{
    namespace
    {
        constexpr size_t middleResolution = 1;
        constexpr double dcCutoffHz = 5.0;

        int middleOrder (double sampleRate)
        {
            // ZLE's getScaledOrder(sampleRate, 12).
            return sampleRate <= 12500.0 ? 10 : sampleRate <= 25000.0 ? 11
                                            : sampleRate <= 50000.0   ? 12
                                            : sampleRate <= 100000.0  ? 13
                                            : sampleRate <= 200000.0  ? 14
                                                                      : 15;
        }
    }

    struct AnalyzerEngine::Impl
    {
        struct Resolution
        {
            std::unique_ptr<zldsp::fft::RFFT<float>> fft;
            hwy::AlignedFreeUniquePtr<float[]> input, output;
            std::vector<float> window;
            std::array<std::vector<float>, 2> powers;
            zldsp::analyzer::SpectrumSmoother smoother;
        };
        std::array<Resolution, 3> resolutions;
        std::array<std::array<std::vector<float>, 2>, 2> history;
        std::array<std::array<double, 2>, 2> previousInput {}, previousOutput {};
        std::array<std::vector<float>, 2> blended;
        std::vector<float> frequencies;
        size_t writePosition = 0, historySize = 0;
        double sampleRate = 0.0, dcPole = 0.0;
        int channels = 0;
        unsigned session = 0;
        std::uint64_t expectedSequence = 0, revision = 0;
        AnalyzerQuality quality = AnalyzerQuality::Normal;
        AnalyzerSettings settings;
        std::array<zldsp::analyzer::SpectrumDecayer, 2> decayers;
        std::array<std::vector<float>, 2> displayLevels;
        std::vector<float> displayFrequencies, displayTilt;
        std::array<bool, 2> displayEnabled {};
        AnalyzerQuality displayQuality = AnalyzerQuality::Normal;
        float displayTiltSlope = 0.0f;
        bool displayReady = false;

        void configureSmoothing (AnalyzerSettings value)
        {
            for (auto& resolution : resolutions)
                if (value.smoothing == AnalyzerSmoothing::Erb)
                    resolution.smoother.setSmoothERB (sampleRate, analyzerErbAmounts[value.erbAmount]);
                else
                    resolution.smoother.setSmoothOCT (analyzerOctaveAmounts[value.octaveAmount]);
            settings = value;
        }

        void prepare (double rate)
        {
            sampleRate = rate;
            dcPole = std::exp (-2.0 * std::numbers::pi * dcCutoffHz / rate);
            const auto order = middleOrder (rate);
            for (size_t index = 0; index < resolutions.size(); ++index)
            {
                auto& resolution = resolutions[index];
                resolution.fft = std::make_unique<zldsp::fft::RFFT<float>> (static_cast<size_t> (order + 2 - 2 * static_cast<int> (index)));
                const auto size = resolution.fft->get_size();
                resolution.input = hwy::AllocateAligned<float> (size);
                resolution.output = hwy::AllocateAligned<float> (size / 2 + 1);
                resolution.window.resize (size);
                for (size_t i = 0; i < size; ++i)
                    resolution.window[i] = static_cast<float> ((1.0 - std::cos (2.0 * std::numbers::pi * static_cast<double> (i) / static_cast<double> (size))) * 2.0 / static_cast<double> (size));
                for (auto& power : resolution.powers)
                    power.assign (size / 2 + 1, 0.0f);
                resolution.smoother.prepare (size);
            }
            configureSmoothing (settings);
            historySize = resolutions.front().fft->get_size();
            for (auto& trace : history)
                for (auto& channel : trace)
                    channel.assign (historySize, 0.0f);
            frequencies = zldsp::analyzer::SpectrumBlender::createFrequencyGrid (
                resolutions[0].fft->get_size(), resolutions[1].fft->get_size(), resolutions[2].fft->get_size(), rate);
            for (auto& trace : blended)
                trace.assign (frequencies.size(), 0.0f);
        }

        void reset()
        {
            for (auto& trace : history)
                for (auto& channel : trace)
                    std::fill (channel.begin(), channel.end(), 0.0f);
            displayReady = false;
            previousInput = {};
            previousOutput = {};
            writePosition = 0;
            ++revision;
        }
    };

    AnalyzerEngine::AnalyzerEngine() : impl (std::make_unique<Impl>()) {}
    AnalyzerEngine::~AnalyzerEngine() = default;

    void AnalyzerEngine::push (const AnalyzerPacket& packet)
    {
        auto& state = *impl;
        if (packet.samples <= 0 || packet.samples > AnalyzerPacket::capacity || packet.channels < 1 || packet.channels > 2
            || !std::isfinite (packet.sampleRate) || packet.sampleRate < bandLimits::minimumSampleRateHz || packet.sampleRate > maximumAnalyzerSampleRate)
            return;
        const bool rateChanged = !juce::exactlyEqual (state.sampleRate, packet.sampleRate);
        if (rateChanged)
            state.prepare (packet.sampleRate);
        if (rateChanged || packet.channels != state.channels || packet.session != state.session || packet.sequence != state.expectedSequence)
            state.reset();
        state.channels = packet.channels;
        state.session = packet.session;
        state.expectedSequence = packet.sequence + static_cast<std::uint64_t> (packet.samples);
        for (int i = 0; i < packet.samples; ++i)
        {
            for (size_t trace = 0; trace < state.history.size(); ++trace)
                for (int channel = 0; channel < state.channels; ++channel)
                {
                    const auto ch = static_cast<size_t> (channel);
                    const auto value = (trace == 0 ? packet.pre[ch] : packet.post[ch])[static_cast<size_t> (i)];
                    // A bad display sample must not poison the history or the audible buffer.
                    const double input = std::isfinite (value) ? std::clamp (static_cast<double> (value), -1.0e12, 1.0e12) : 0.0;
                    const auto output = input - state.previousInput[trace][ch] + state.dcPole * state.previousOutput[trace][ch];
                    state.previousInput[trace][ch] = input;
                    state.previousOutput[trace][ch] = output;
                    state.history[trace][ch][state.writePosition] = static_cast<float> (output);
                }
            state.writePosition = (state.writePosition + 1) % state.historySize;
        }
    }

    void AnalyzerEngine::analyze (AnalyzerSettings settings)
    {
        auto& state = *impl;
        if (!settings.isValid())
            return;
        if (state.channels == 0)
        {
            state.settings = settings;
            return;
        }
        if (settings.smoothing != state.settings.smoothing || settings.erbAmount != state.settings.erbAmount || settings.octaveAmount != state.settings.octaveAmount)
            state.configureSmoothing (settings);
        state.settings = settings;
        state.quality = settings.quality;
        const auto first = settings.quality == AnalyzerQuality::High ? size_t (0) : middleResolution;
        const auto end = settings.quality == AnalyzerQuality::High ? state.resolutions.size() : middleResolution + 1;
        for (size_t trace = 0; trace < state.history.size(); ++trace)
        {
            if (!(trace == 0 ? settings.pre : settings.post))
                continue;
            for (size_t index = first; index < end; ++index)
            {
                auto& resolution = state.resolutions[index];
                auto& power = resolution.powers[trace];
                std::fill (power.begin(), power.end(), 0.0f);
                const auto size = resolution.fft->get_size();
                for (int channel = 0; channel < state.channels; ++channel)
                {
                    const auto& history = state.history[trace][static_cast<size_t> (channel)];
                    for (size_t i = 0; i < size; ++i)
                        resolution.input[i] = history[(state.writePosition + state.historySize - size + i) % state.historySize] * resolution.window[i];
                    resolution.fft->forward_sqr_mag (resolution.input.get(), resolution.output.get());
                    const auto noiseScale = static_cast<float> (size) / static_cast<float> (state.resolutions[middleResolution].fft->get_size());
                    for (size_t i = 0; i < power.size(); ++i)
                        power[i] += resolution.output[i] * noiseScale / static_cast<float> (state.channels);
                }
                // DC/Nyquist have no distinct negative-frequency partner to double.
                power.front() *= 0.25f;
                power.back() *= 0.25f;
                resolution.smoother.smooth (power);
            }
            if (settings.quality == AnalyzerQuality::High)
                zldsp::analyzer::SpectrumBlender::blend (state.blended[trace], state.frequencies, state.resolutions[0].powers[trace], state.resolutions[1].powers[trace], state.resolutions[2].powers[trace], state.sampleRate);
        }
    }

    void AnalyzerEngine::updateDisplay (double elapsedSeconds, bool stale, float minimumDb, float tiltDbPerOctave)
    {
        auto& state = *impl;
        if (state.channels == 0 || !std::isfinite (elapsedSeconds) || elapsedSeconds <= 0.0
            || !std::isfinite (minimumDb) || !std::isfinite (tiltDbPerOctave))
            return;
        constexpr float silenceDb = -240.0f;
        if (!state.displayReady || state.displayQuality != state.quality)
        {
            state.displayQuality = state.quality;
            if (state.quality == AnalyzerQuality::High)
                state.displayFrequencies = state.frequencies;
            else
            {
                const auto size = state.resolutions[middleResolution].powers[0].size();
                state.displayFrequencies.resize (size);
                const auto delta = static_cast<float> (state.sampleRate / state.resolutions[middleResolution].fft->get_size());
                for (size_t i = 0; i < size; ++i)
                    state.displayFrequencies[i] = static_cast<float> (i) * delta;
            }
            for (size_t trace = 0; trace < state.displayLevels.size(); ++trace)
            {
                state.displayLevels[trace].assign (state.displayFrequencies.size(), silenceDb);
                state.decayers[trace].prepareSpectrum (state.displayFrequencies.size());
            }
            state.displayTilt.clear();
            state.displayEnabled = {};
            state.displayReady = true;
        }
        if (state.displayTilt.size() != state.displayFrequencies.size() || !juce::exactlyEqual (state.displayTiltSlope, tiltDbPerOctave))
        {
            state.displayTiltSlope = tiltDbPerOctave;
            state.displayTilt.resize (state.displayFrequencies.size());
            for (size_t i = 0; i < state.displayTilt.size(); ++i)
                state.displayTilt[i] = tiltDbPerOctave * std::log2 (std::max (state.displayFrequencies[i], state.displayFrequencies[1]) / 1000.0f);
        }
        const auto speed = analyzerDecaySpeeds[static_cast<size_t> (state.settings.decay)];
        // ZLE's default extra-speed control is 1: its multiplier is 1^2 + 0.1.
        const auto duration = 0.15f / (speed * 1.1f);
        const auto refresh = static_cast<float> (1.0 / std::max (elapsedSeconds, 1.0e-6));
        for (size_t trace = 0; trace < state.displayLevels.size(); ++trace)
        {
            const auto enabled = trace == 0 ? state.settings.pre : state.settings.post;
            auto& levels = state.displayLevels[trace];
            if (!enabled)
            {
                if (state.displayEnabled[trace])
                {
                    state.decayers[trace].prepareSpectrum (levels.size());
                    std::fill (levels.begin(), levels.end(), silenceDb);
                }
                state.displayEnabled[trace] = false;
                continue;
            }
            state.displayEnabled[trace] = true;
            const auto& powers = state.quality == AnalyzerQuality::High ? state.blended[trace] : state.resolutions[middleResolution].powers[trace];
            for (size_t i = 0; i < levels.size(); ++i)
                levels[i] = (stale ? silenceDb : 10.0f * std::log10 (std::max (powers[i], 1.0e-24f))) + state.displayTilt[i];
            auto& decayer = state.decayers[trace];
            decayer.setDecaySpeed (refresh, minimumDb, duration);
            decayer.decay (levels);
        }
    }

    float AnalyzerEngine::displayDbAt (size_t trace, double frequency) const
    {
        const auto& state = *impl;
        if (!state.displayReady || trace >= state.displayLevels.size() || !std::isfinite (frequency))
            return -240.0f;
        const auto& levels = state.displayLevels[trace];
        if (state.displayQuality == AnalyzerQuality::Normal)
        {
            const auto bin = std::clamp (frequency * state.resolutions[middleResolution].fft->get_size() / state.sampleRate, 0.0, static_cast<double> (levels.size() - 1));
            const auto lower = static_cast<size_t> (bin);
            return std::lerp (levels[lower], levels[std::min (lower + 1, levels.size() - 1)], static_cast<float> (bin - lower));
        }
        const auto upper = std::lower_bound (state.displayFrequencies.begin(), state.displayFrequencies.end(), static_cast<float> (frequency));
        if (upper == state.displayFrequencies.begin())
            return levels.front();
        if (upper == state.displayFrequencies.end())
            return levels.back();
        const auto index = static_cast<size_t> (upper - state.displayFrequencies.begin());
        const auto mix = static_cast<float> ((frequency - state.displayFrequencies[index - 1]) / (*upper - state.displayFrequencies[index - 1]));
        return std::lerp (levels[index - 1], levels[index], mix);
    }

    float AnalyzerEngine::powerAt (size_t trace, double frequency) const
    {
        const auto& state = *impl;
        if (state.channels == 0 || trace >= state.history.size() || !std::isfinite (frequency))
            return 0.0f;
        if (state.quality == AnalyzerQuality::Normal)
        {
            const auto& spectrum = state.resolutions[middleResolution].powers[trace];
            const auto bin = std::clamp (frequency * static_cast<double> (state.resolutions[middleResolution].fft->get_size()) / state.sampleRate, 0.0, static_cast<double> (spectrum.size() - 1));
            const auto lower = static_cast<size_t> (bin);
            return std::lerp (spectrum[lower], spectrum[std::min (lower + 1, spectrum.size() - 1)], static_cast<float> (bin - static_cast<double> (lower)));
        }
        const auto upper = std::lower_bound (state.frequencies.begin(), state.frequencies.end(), static_cast<float> (frequency));
        if (upper == state.frequencies.begin())
            return state.blended[trace].front();
        if (upper == state.frequencies.end())
            return state.blended[trace].back();
        const auto index = static_cast<size_t> (upper - state.frequencies.begin());
        const auto mix = static_cast<float> ((frequency - state.frequencies[index - 1]) / (*upper - state.frequencies[index - 1]));
        return std::lerp (state.blended[trace][index - 1], state.blended[trace][index], mix);
    }

    double AnalyzerEngine::getSampleRate() const { return impl->sampleRate; }
    std::uint64_t AnalyzerEngine::getRevision() const { return impl->revision; }
    size_t AnalyzerEngine::getMiddleFFTSize() const { return impl->channels == 0 ? 0 : impl->resolutions[middleResolution].fft->get_size(); }
}
