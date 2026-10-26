// SPDX-License-Identifier: AGPL-3.0-only
#include "dsp/BandFilter.h"
#include "ui/ResponseWorker.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <numbers>

TEST_CASE ("Shelves retain the pinned ZLE response and remain stable across supported settings", "[shelf][coefficients]")
{
    for (auto type : { openq::FilterType::LowShelf, openq::FilterType::HighShelf })
        for (double rate : { 8000.0, 44100.0, 48000.0, 96000.0, 192000.0 })
            for (double frequency : { 20.0, 1000.0, 20000.0 })
                for (double gain : { -24.0, -6.0, -0.0001, 0.0, 0.0001, 6.0, 24.0 })
                    for (double q : { 0.1, 0.70710678, 1.0, 10.0 })
                    {
                        const auto c = openq::makeFilterCoefficients (type, rate, frequency, gain, q);
                        REQUIRE (std::abs (c.a2) < 1.0);
                        REQUIRE (1.0 + c.a1 + c.a2 > 0.0);
                        REQUIRE (1.0 - c.a1 + c.a2 > 0.0);
                        openq::DigitalResponse response { c, rate };
                        CHECK_THAT (openq::responseMagnitudeDb (response, 0.0), Catch::Matchers::WithinAbs (type == openq::FilterType::LowShelf ? gain : 0.0, 1e-5));
                        for (int point = 0; point <= 32; ++point)
                            REQUIRE (std::isfinite (openq::responseMagnitudeDb (response, rate * point / 64.0)));
                    }
    const openq::DigitalResponse low { openq::makeFilterCoefficients (openq::FilterType::LowShelf, 48000.0, 20000.0, 6.0, 1.0), 48000.0 };
    const openq::DigitalResponse high { openq::makeFilterCoefficients (openq::FilterType::HighShelf, 48000.0, 20000.0, 6.0, 1.0), 48000.0 };
    CHECK_THAT (openq::responseMagnitudeDb (low, 20000.0), Catch::Matchers::WithinAbs (3.0, 1e-8));
    CHECK_THAT (openq::responseMagnitudeDb (high, 20000.0), Catch::Matchers::WithinAbs (3.0, 1e-8));
    CHECK_THAT (openq::responseMagnitudeDb (low, 24000.0), Catch::Matchers::WithinAbs (2.12539446336, 1e-8));
    CHECK_THAT (openq::responseMagnitudeDb (high, 24000.0), Catch::Matchers::WithinAbs (3.87460553664, 1e-8));
}

TEST_CASE ("Shelf audio agrees with the displayed digital response in mono and stereo", "[shelf][audio]")
{
    for (auto type : { openq::FilterType::LowShelf, openq::FilterType::HighShelf })
        for (double gain : { -12.0, 12.0 })
            for (double probe : { 100.0, 1000.0, 10000.0 })
                for (int channels : { 1, 2 })
                {
                    openq::BandFilter filter;
                    filter.prepare (48000.0, 1000.0, gain, 1.0, openq::BandStatus::On, type);
                    juce::AudioBuffer<float> buffer (channels, 128);
                    double inputEnergy = 0.0, outputEnergy = 0.0;
                    for (int block = 0; block < 375; ++block)
                    {
                        buffer.clear();
                        for (int i = 0; i < 128; ++i)
                        {
                            const auto input = static_cast<float> (0.01 * std::sin (2.0 * std::numbers::pi * probe * (block * 128 + i) / 48000.0));
                            buffer.setSample (0, i, input);
                            if (block >= 187)
                                inputEnergy += input * input;
                        }
                        filter.process (buffer, channels);
                        if (channels == 2)
                            REQUIRE (juce::exactlyEqual (buffer.getMagnitude (1, 0, 128), 0.0f));
                        if (block >= 187)
                            for (int i = 0; i < 128; ++i)
                                outputEnergy += std::pow (buffer.getSample (0, i), 2.0);
                    }
                    const openq::DigitalResponse expected { openq::makeFilterCoefficients (type, 48000.0, 1000.0, gain, 1.0), 48000.0 };
                    CHECK_THAT (10.0 * std::log10 (outputEnergy / inputEnergy), Catch::Matchers::WithinAbs (openq::responseMagnitudeDb (expected, probe), 0.02));
                }
}

TEST_CASE ("Filter type switches fade through dry and settle to the new shelf", "[shelf][automation]")
{
    openq::BandFilter filter;
    filter.prepare (48000.0, 1000.0, 12.0, 1.0);
    juce::AudioBuffer<float> sample (1, 1);
    float previous = 0.01f;
    for (int i = 0; i < 10000; ++i)
    {
        if (i == 4000)
            filter.setParameters (1000.0, 12.0, 1.0, openq::BandStatus::On, openq::FilterType::LowShelf);
        sample.setSample (0, 0, 0.01f);
        filter.process (sample, 1);
        const auto output = sample.getSample (0, 0);
        if (i >= 4000)
            REQUIRE (std::abs (output - previous) < 0.0002f);
        if (i == 4959)
            CHECK_THAT (output, Catch::Matchers::WithinAbs (0.01, 1e-8));
        if (i == 9999)
            CHECK_THAT (output, Catch::Matchers::WithinAbs (0.01 * std::pow (10.0, 12.0 / 20.0), 1e-7));
        previous = output;
    }
}

TEST_CASE ("Shelf type and status automation is independent of block boundaries", "[shelf][automation]")
{
    openq::BandFilter whole, split;
    whole.prepare (48000.0, 200.0, 12.0, 1.0);
    split.prepare (48000.0, 200.0, 12.0, 1.0);
    for (int segment = 0; segment < 60; ++segment)
    {
        const auto type = static_cast<openq::FilterType> (segment % 3);
        const auto status = static_cast<openq::BandStatus> ((segment / 3) % 3);
        whole.setParameters (200.0, 12.0, 1.0, status, type);
        split.setParameters (200.0, 12.0, 1.0, status, type);
        // Both interrupted changes and complete fade-out/fade-in transitions.
        const int size = segment % 2 == 0 ? 137 : 2500;
        juce::AudioBuffer<float> buffer (1, size);
        for (int i = 0; i < size; ++i)
            buffer.setSample (0, i, static_cast<float> (0.01 * std::sin (0.02 * (segment * 2500 + i))));
        auto expected = buffer;
        whole.process (expected, 1);
        for (int i = 0; i < size; ++i)
        {
            float* data = buffer.getWritePointer (0, i);
            juce::AudioBuffer<float> one (&data, 1, 1);
            split.process (one, 1);
            REQUIRE (std::isfinite (*data));
            REQUIRE_THAT (*data, Catch::Matchers::WithinAbs (expected.getSample (0, i), 1e-7));
        }
    }
}

TEST_CASE ("Changing a bypassed or Off band's type stays dry and warms Bypass for On", "[shelf][automation]")
{
    for (auto status : { openq::BandStatus::Off, openq::BandStatus::Bypass })
    {
        openq::BandFilter filter, reference;
        filter.prepare (48000.0, 1000.0, 12.0, 1.0, status);
        filter.setParameters (1000.0, 12.0, 1.0, status, openq::FilterType::LowShelf);
        reference.prepare (48000.0, 1000.0, 12.0, 1.0, openq::BandStatus::On, openq::FilterType::LowShelf);
        juce::AudioBuffer<float> actual (1, 1), expected (1, 1);
        for (int i = 0; i < 5000; ++i)
        {
            if (i == 3000)
                filter.setParameters (1000.0, 12.0, 1.0, openq::BandStatus::On, openq::FilterType::LowShelf);
            actual.setSample (0, 0, 0.01f);
            expected.setSample (0, 0, 0.01f);
            filter.process (actual, 1);
            reference.process (expected, 1);
            if (i < 3000)
                REQUIRE (juce::exactlyEqual (actual.getSample (0, 0), 0.01f));
            if (status == openq::BandStatus::Bypass)
            {
                const auto mix = std::clamp ((i - 2999) / 960.0, 0.0, 1.0);
                REQUIRE_THAT (actual.getSample (0, 0), Catch::Matchers::WithinAbs (0.01f + mix * (expected.getSample (0, 0) - 0.01f), 1e-7));
            }
            if (i == 4999)
                CHECK_THAT (actual.getSample (0, 0), Catch::Matchers::WithinAbs (expected.getSample (0, 0), 1e-7));
        }
    }
}
