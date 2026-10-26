// SPDX-License-Identifier: AGPL-3.0-only
#include "PluginProcessor.h"
#include "dsp/BandFilter.h"
#include "ui/ResponseWorker.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <numbers>

namespace
{
    void set (PluginProcessor& processor, size_t band, const char* name, float value)
    {
        auto* p = processor.parameters.getParameter (openq::bandParameterId (band, name));
        REQUIRE (p != nullptr);
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    }
}

TEST_CASE ("All cut slopes have stable sections and retain the Ivantsov cutoff convention", "[cuts][coefficients]")
{
    for (auto type : { openq::FilterType::LowCut, openq::FilterType::HighCut })
        for (size_t index = 0; index < openq::cutOrders.size(); ++index)
            for (double rate : { 1000.0, 8000.0, 44100.0, 48000.0, 96000.0, 192000.0 })
                for (double requested : { 20.0, 1000.0, 20000.0 })
                    for (double q : { 0.1, openq::bandLimits::defaultCutQ, 1.0, 10.0 })
                    {
                        CAPTURE (static_cast<int> (type), index, rate, requested, q);
                        const auto slope = static_cast<openq::CutSlope> (index);
                        const auto c = openq::makeBandCoefficients (type, rate, requested, 0.0, q, slope);
                        REQUIRE (c.count == (openq::cutOrders[index] + 1) / 2);
                        for (size_t i = 0; i < c.count; ++i)
                        {
                            const auto& section = c.sections[i];
                            REQUIRE (std::abs (section.a2) < 1.0);
                            REQUIRE (1.0 + section.a1 + section.a2 > 0.0);
                            REQUIRE (1.0 - section.a1 + section.a2 > 0.0);
                        }
                        const openq::BandResponse response { c, rate };
                        const auto frequency = std::min (requested, openq::bandLimits::effectiveMaximumFrequency (rate));
                        const auto expectedDb = index == 0 ? -10.0 * std::log10 (2.0) : 20.0 * std::log10 (q);
                        CHECK_THAT (openq::responseMagnitudeDb (response, frequency), Catch::Matchers::WithinAbs (expectedDb, 1e-6));
                        if (type == openq::FilterType::HighCut)
                            CHECK_THAT (openq::responseMagnitudeDb (response, 0.0), Catch::Matchers::WithinAbs (0.0, 1e-5));
                        for (int point = 0; point <= 32; ++point)
                            REQUIRE (std::isfinite (openq::responseMagnitudeDb (response, rate * point / 64.0)));
                    }
    const auto identity = openq::makeBandCoefficients (openq::FilterType::LowCut, 48000.0, 1000.0, 0.0, 1.0, static_cast<openq::CutSlope> (-1));
    CHECK (identity.count == 1);
    CHECK_THAT (identity.sections[0].b0, Catch::Matchers::WithinAbs (1.0, 0.0));
}

TEST_CASE ("18 dB cut retains validated near-Nyquist anchors and unclamped internal Q", "[cuts][coefficients]")
{
    for (auto type : { openq::FilterType::LowCut, openq::FilterType::HighCut })
    {
        const openq::BandResponse neutral { openq::makeBandCoefficients (type, 48000.0, 20000.0, 0.0, openq::bandLimits::defaultCutQ, openq::CutSlope::Db18), 48000.0 };
        CHECK_THAT (openq::responseMagnitudeDb (neutral, 24000.0), Catch::Matchers::WithinAbs (type == openq::FilterType::LowCut ? -2.07757009894 : -4.19973897834, 1e-8));
        const openq::BandResponse resonant { openq::makeBandCoefficients (type, 48000.0, 1000.0, 0.0, 10.0, openq::CutSlope::Db18), 48000.0 };
        CHECK_THAT (openq::responseMagnitudeDb (resonant, 1000.0), Catch::Matchers::WithinAbs (20.0, 1e-8));
    }
}

TEST_CASE ("Cut audio stays active at zero gain and agrees with every slope's digital curve", "[cuts][audio]")
{
    for (auto type : { openq::FilterType::LowCut, openq::FilterType::HighCut })
        for (size_t index = 0; index < openq::cutOrders.size(); ++index)
            for (double probe : { 800.0, 1000.0, 1250.0 })
            {
                CAPTURE (static_cast<int> (type), index, probe);
                const auto slope = static_cast<openq::CutSlope> (index);
                openq::BandFilter filter;
                filter.prepare (48000.0, 1000.0, 0.0, openq::bandLimits::defaultCutQ, openq::BandStatus::On, type, slope);
                juce::AudioBuffer<float> buffer (2, 960);
                double inputEnergy = 0.0, outputEnergy = 0.0;
                for (int block = 0; block < 40; ++block)
                {
                    buffer.clear();
                    for (int i = 0; i < buffer.getNumSamples(); ++i)
                    {
                        const auto input = static_cast<float> (0.001 * std::sin (2.0 * std::numbers::pi * probe * (block * buffer.getNumSamples() + i) / 48000.0));
                        buffer.setSample (0, i, input);
                        if (block >= 20)
                            inputEnergy += static_cast<double> (input) * input;
                    }
                    // Automating retained gain must not alter any cut's processing.
                    filter.setParameters (1000.0, block % 2 == 0 ? -24.0 : 24.0, openq::bandLimits::defaultCutQ, openq::BandStatus::On, type, slope);
                    filter.process (buffer, 2);
                    REQUIRE (juce::exactlyEqual (buffer.getMagnitude (1, 0, buffer.getNumSamples()), 0.0f));
                    if (block >= 20)
                        for (int i = 0; i < buffer.getNumSamples(); ++i)
                            outputEnergy += std::pow (buffer.getSample (0, i), 2.0);
                }
                const openq::BandResponse expected { openq::makeBandCoefficients (type, 48000.0, 1000.0, 0.0, openq::bandLimits::defaultCutQ, slope), 48000.0 };
                CHECK_THAT (10.0 * std::log10 (outputEnergy / inputEnergy), Catch::Matchers::WithinAbs (openq::responseMagnitudeDb (expected, probe), 0.02));
            }
}

TEST_CASE ("Slope changes fade through dry before switching section histories", "[cuts][automation]")
{
    openq::BandFilter filter;
    filter.prepare (48000.0, 1000.0, 0.0, openq::bandLimits::defaultCutQ, openq::BandStatus::On, openq::FilterType::LowCut, openq::CutSlope::Db6);
    juce::AudioBuffer<float> sample (1, 1);
    float previous = 0.0f;
    for (int i = 0; i < 10000; ++i)
    {
        if (i == 4000)
            filter.setParameters (1000.0, 0.0, openq::bandLimits::defaultCutQ, openq::BandStatus::On, openq::FilterType::LowCut, openq::CutSlope::Db96);
        const auto input = static_cast<float> (0.001 * std::sin (2.0 * std::numbers::pi * 1000.0 * i / 48000.0));
        sample.setSample (0, 0, input);
        filter.process (sample, 1);
        REQUIRE (std::isfinite (sample.getSample (0, 0)));
        if (i >= 4000)
            REQUIRE (std::abs (sample.getSample (0, 0) - previous) < 0.0003f);
        if (i == 4959)
            CHECK_THAT (sample.getSample (0, 0), Catch::Matchers::WithinAbs (input, 1e-9));
        previous = sample.getSample (0, 0);
    }
}

TEST_CASE ("Interrupted cut type slope and status changes do not depend on block boundaries", "[cuts][automation]")
{
    openq::BandFilter whole, split;
    whole.prepare (48000.0, 80.0, 0.0, 10.0, openq::BandStatus::On, openq::FilterType::LowCut);
    split.prepare (48000.0, 80.0, 0.0, 10.0, openq::BandStatus::On, openq::FilterType::LowCut);
    for (int segment = 0; segment < 48; ++segment)
    {
        const auto type = segment % 2 == 0 ? openq::FilterType::LowCut : openq::FilterType::HighCut;
        const auto slope = static_cast<openq::CutSlope> (static_cast<size_t> (segment) % openq::cutOrders.size());
        const auto status = static_cast<openq::BandStatus> ((segment / 2) % 3);
        const auto frequency = segment % 2 == 0 ? 80.0 : 16000.0;
        whole.setParameters (frequency, 0.0, 10.0, status, type, slope);
        split.setParameters (frequency, 0.0, 10.0, status, type, slope);
        juce::AudioBuffer<float> buffer (1, segment % 3 == 0 ? 2500 : 137);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample (0, i, static_cast<float> (0.001 * std::sin (0.02 * (segment * 2500 + i))));
        auto expected = buffer;
        whole.process (expected, 1);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            float* data = buffer.getWritePointer (0, i);
            juce::AudioBuffer<float> one (&data, 1, 1);
            split.process (one, 1);
            REQUIRE (std::isfinite (*data));
            REQUIRE_THAT (*data, Catch::Matchers::WithinAbs (expected.getSample (0, i), 1e-7));
        }
    }
}

TEST_CASE ("Bypassed cuts retain every section's warm history and Off clears it", "[cuts][automation]")
{
    for (auto slope : { openq::CutSlope::Db6, openq::CutSlope::Db18, openq::CutSlope::Db96 })
    {
        openq::BandFilter warm, reference;
        warm.prepare (48000.0, 100.0, 0.0, 10.0, openq::BandStatus::Bypass, openq::FilterType::HighCut, slope);
        reference.prepare (48000.0, 100.0, 0.0, 10.0, openq::BandStatus::On, openq::FilterType::HighCut, slope);
        juce::AudioBuffer<float> actual (1, 1), expected (1, 1);
        for (int i = 0; i < 10000; ++i)
        {
            if (i == 5000)
                warm.setParameters (100.0, 0.0, 10.0, openq::BandStatus::On, openq::FilterType::HighCut, slope);
            const auto input = static_cast<float> (0.001 * std::sin (2.0 * std::numbers::pi * 100.0 * i / 48000.0));
            actual.setSample (0, 0, input);
            expected.setSample (0, 0, input);
            warm.process (actual, 1);
            reference.process (expected, 1);
            const auto mix = std::clamp ((i - 4999) / 960.0, 0.0, 1.0);
            REQUIRE_THAT (actual.getSample (0, 0), Catch::Matchers::WithinAbs (input + mix * (expected.getSample (0, 0) - input), 1e-8));
        }
        warm.setParameters (100.0, 0.0, 10.0, openq::BandStatus::Off, openq::FilterType::HighCut, slope);
        juce::AudioBuffer<float> silence (1, 2048);
        silence.clear();
        warm.process (silence, 1);
        warm.setParameters (100.0, 0.0, 10.0, openq::BandStatus::On, openq::FilterType::HighCut, slope);
        silence.clear();
        warm.process (silence, 1);
        CHECK (juce::exactlyEqual (silence.getMagnitude (0, silence.getNumSamples()), 0.0f));
    }
}

TEST_CASE ("All configured bands process 96 dB cuts through the plugin", "[cuts][multiband]")
{
    PluginProcessor processor;
    for (size_t band = 0; band < openq::bandCount; ++band)
    {
        set (processor, band, "type", static_cast<float> (band % 2 == 0 ? openq::FilterType::LowCut : openq::FilterType::HighCut));
        set (processor, band, "slope", static_cast<float> (openq::CutSlope::Db96));
        set (processor, band, "q", static_cast<float> (openq::bandLimits::defaultCutQ));
        set (processor, band, "status", static_cast<float> (openq::BandStatus::On));
    }
    processor.prepareToPlay (48000.0, 960);
    juce::AudioBuffer<float> buffer (2, 960);
    juce::MidiBuffer midi;
    double inputEnergy = 0.0, outputEnergy = 0.0;
    for (int block = 0; block < 80; ++block)
    {
        buffer.clear();
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto input = static_cast<float> (0.001 * std::sin (2.0 * std::numbers::pi * 1000.0 * (block * buffer.getNumSamples() + i) / 48000.0));
            buffer.setSample (0, i, input);
            if (block >= 40)
                inputEnergy += static_cast<double> (input) * input;
        }
        processor.processBlock (buffer, midi);
        REQUIRE (juce::exactlyEqual (buffer.getMagnitude (1, 0, buffer.getNumSamples()), 0.0f));
        if (block >= 40)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                outputEnergy += std::pow (buffer.getSample (0, i), 2.0);
    }
    CHECK_THAT (10.0 * std::log10 (outputEnergy / inputEnergy), Catch::Matchers::WithinAbs (-10.0 * std::log10 (2.0) * static_cast<double> (openq::bandCount), 0.03));
}

TEST_CASE ("Host tail covers the full bank of resonant 96 dB cuts", "[cuts][tail]")
{
    constexpr double sampleRate = 8000.0;
    constexpr int blockSize = 128;
    constexpr float impulse = 1e-20f; // Headroom for the complete resonant cascade.
    for (auto type : { openq::FilterType::LowCut, openq::FilterType::HighCut })
    {
        PluginProcessor processor;
        for (size_t band = 0; band < openq::bandCount; ++band)
        {
            set (processor, band, "type", static_cast<float> (type));
            set (processor, band, "slope", static_cast<float> (openq::CutSlope::Db96));
            set (processor, band, "frequency", 20.0f);
            set (processor, band, "q", 10.0f);
            set (processor, band, "status", static_cast<float> (openq::BandStatus::On));
        }
        processor.prepareToPlay (sampleRate, blockSize);
        juce::AudioBuffer<float> buffer (1, blockSize);
        juce::MidiBuffer midi;
        const auto blocks = static_cast<int> (processor.getTailLengthSeconds() * sampleRate / blockSize);
        float largestTail = 0.0f;
        for (int block = 0; block < blocks; ++block)
        {
            buffer.clear();
            if (block == 0)
                buffer.setSample (0, 0, impulse);
            processor.processBlock (buffer, midi);
            const auto magnitude = buffer.getMagnitude (0, blockSize);
            REQUIRE (std::isfinite (magnitude));
            if (block > 0)
                largestTail = std::max (largestTail, magnitude);
        }
        CHECK (largestTail > impulse * 0.01f);
        CHECK (buffer.getMagnitude (0, blockSize) < impulse * 0.1f);
    }
}
