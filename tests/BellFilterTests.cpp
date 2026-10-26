#include "PluginProcessor.h"
#include "dsp/BellFilter.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <complex>
#include <numbers>

namespace
{
    void setParameter (PluginProcessor& processor, const char* id, float value)
    {
        auto* parameter = processor.parameters.getParameter (id);
        REQUIRE (parameter != nullptr);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    }

    double measuredGain (double sampleRate, double frequency, double gainDb)
    {
        openq::BellFilter filter;
        filter.prepare (sampleRate, frequency, gainDb, 1.0);
        juce::AudioBuffer<float> buffer (1, 128);
        double inputEnergy = 0.0, outputEnergy = 0.0;
        const auto length = static_cast<int> (sampleRate * 0.4);
        for (int offset = 0; offset < length; offset += buffer.getNumSamples())
        {
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                buffer.setSample (0, sample, static_cast<float> (0.05 * std::sin (2.0 * std::numbers::pi * frequency * (offset + sample) / sampleRate)));
            const auto input = buffer;
            filter.process (buffer, 1);
            if (offset > length / 2)
                for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                {
                    inputEnergy += std::pow (input.getSample (0, sample), 2.0);
                    outputEnergy += std::pow (buffer.getSample (0, sample), 2.0);
                }
        }
        return 10.0 * std::log10 (outputEnergy / inputEnergy);
    }
}

TEST_CASE ("Bell renders the requested center gain", "[bell][response]")
{
    for (const auto sampleRate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (const auto frequency : { 1000.0, 20000.0 })
            for (const auto gainDb : { -24.0, -6.0, 6.0, 24.0 })
            {
                CAPTURE (sampleRate, frequency, gainDb);
                CHECK_THAT (measuredGain (sampleRate, frequency, gainDb), Catch::Matchers::WithinAbs (gainDb, 0.05));
            }
}

TEST_CASE ("Bell coefficients retain the probed ZLE response", "[bell][response]")
{
    const auto c = openq::makeBellCoefficients (48000.0, 20000.0, 6.0, 1.0);
    const auto nyquistDb = 20.0 * std::log10 (std::abs ((c.b0 - c.b1 + c.b2) / (1.0 - c.a1 + c.a2)));
    CHECK_THAT (nyquistDb, Catch::Matchers::WithinAbs (5.83299135367, 1e-9));

    const auto clamped = openq::makeBellCoefficients (8000.0, 20000.0, 6.0, 1.0);
    const auto expected = openq::makeBellCoefficients (8000.0, 3920.0, 6.0, 1.0);
    CHECK_THAT (clamped.b0, Catch::Matchers::WithinAbs (expected.b0, 1e-12));
    for (const auto invalid : { std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity() })
    {
        const auto safe = openq::makeBellCoefficients (48000.0, invalid, 6.0, 1.0);
        CHECK_THAT (safe.b0, Catch::Matchers::WithinAbs (1.0, 0.0));
        CHECK_THAT (safe.a1, Catch::Matchers::WithinAbs (0.0, 0.0));
    }
}

TEST_CASE ("Flat bell is transparent and channel states are independent", "[bell]")
{
    openq::BellFilter filter;
    filter.prepare (48000.0, 1000.0, 0.0, 1.0);
    juce::AudioBuffer<float> buffer (2, 257);
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        buffer.setSample (0, sample, static_cast<float> (std::sin (sample * 0.17)));
        buffer.setSample (1, sample, static_cast<float> (std::cos (sample * 0.23)));
    }
    const auto original = buffer;
    filter.process (buffer, 2);
    for (int channel = 0; channel < 2; ++channel)
        CHECK (std::equal (buffer.getReadPointer (channel), buffer.getReadPointer (channel) + buffer.getNumSamples(), original.getReadPointer (channel)));

    filter.prepare (48000.0, 1000.0, 12.0, 4.0);
    buffer.clear();
    buffer.setSample (0, 0, 1.0f);
    filter.process (buffer, 2);
    CHECK (buffer.getMagnitude (0, buffer.getNumSamples()) > 0.0f);
    CHECK_THAT (buffer.getMagnitude (1, 0, buffer.getNumSamples()), Catch::Matchers::WithinAbs (0.0, 0.0));
    filter.reset();
    buffer.clear();
    filter.process (buffer, 2);
    CHECK_THAT (buffer.getMagnitude (0, buffer.getNumSamples()), Catch::Matchers::WithinAbs (0.0, 0.0));
}

TEST_CASE ("Bell smoothing is independent of audio block sizes", "[bell][automation]")
{
    openq::BellFilter whole, split;
    whole.prepare (48000.0, 200.0, -12.0, 0.5);
    split.prepare (48000.0, 200.0, -12.0, 0.5);
    whole.setParameters (18000.0, 12.0, 8.0);
    split.setParameters (18000.0, 12.0, 8.0);
    juce::AudioBuffer<float> input (2, 4097);
    for (int channel = 0; channel < 2; ++channel)
        for (int sample = 0; sample < input.getNumSamples(); ++sample)
            input.setSample (channel, sample, static_cast<float> (0.05 * std::sin (sample * (channel + 1) * 0.13)));
    auto expected = input;
    whole.process (expected, 2);
    int offset = 0;
    for (const int blockSize : { 1, 23, 127, 511, 3435 })
    {
        juce::AudioBuffer<float> block (2, blockSize);
        for (int channel = 0; channel < 2; ++channel)
            block.copyFrom (channel, 0, input, channel, offset, blockSize);
        split.process (block, 2);
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < blockSize; ++sample)
                REQUIRE_THAT (block.getSample (channel, sample), Catch::Matchers::WithinAbs (expected.getSample (channel, offset + sample), 1e-7));
        offset += blockSize;
    }
    CHECK (offset == input.getNumSamples());
}

TEST_CASE ("Bell remains bounded through extreme parameter changes and returns to flat", "[bell][automation]")
{
    for (const auto sampleRate : { 8000.0, 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        CAPTURE (sampleRate);
        openq::BellFilter filter;
        filter.prepare (sampleRate, 1000.0, 0.0, 1.0);
        juce::AudioBuffer<float> buffer (2, 64);
        float peak = 0.0f;
        for (int block = 0; block < 800; ++block)
        {
            if (block % 100 == 0)
            {
                const bool alternate = (block / 100) % 2 != 0;
                filter.setParameters (alternate ? 20.0 : 20000.0, alternate ? -24.0 : 24.0, alternate ? 0.1 : 10.0);
            }
            for (int channel = 0; channel < 2; ++channel)
                for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                    buffer.setSample (channel, sample, static_cast<float> (0.05 * std::sin (2.0 * std::numbers::pi * 1000.0 * (block * 64 + sample) / sampleRate)));
            filter.process (buffer, 2);
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                REQUIRE (std::isfinite (buffer.getSample (0, sample)));
            peak = std::max (peak, buffer.getMagnitude (0, buffer.getNumSamples()));
        }
        CHECK (peak < 4.0f);
        filter.setParameters (1000.0, 0.0, 1.0);
        for (int i = 0; i < 100; ++i)
        {
            buffer.clear();
            filter.process (buffer, 2);
        }
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                buffer.setSample (channel, sample, 0.125f);
        filter.process (buffer, 2);
        CHECK_THAT (buffer.getSample (0, 0), Catch::Matchers::WithinAbs (0.125, 0.0));
        CHECK_THAT (buffer.getSample (1, 63), Catch::Matchers::WithinAbs (0.125, 0.0));
    }
}

TEST_CASE ("Bell impulse decays at low frequency and high Q", "[bell][response]")
{
    openq::BellFilter filter;
    filter.prepare (48000.0, 20.0, 24.0, 10.0);
    juce::AudioBuffer<float> buffer (1, 256);
    buffer.clear();
    buffer.setSample (0, 0, 1.0f);
    filter.process (buffer, 1);
    for (int block = 0; block < 1200; ++block)
    {
        buffer.clear();
        filter.process (buffer, 1);
    }
    CHECK (buffer.getMagnitude (0, buffer.getNumSamples()) < 1e-5f);
}

TEST_CASE ("Bell parameters survive session recall and malformed states are ignored", "[bell][state]")
{
    PluginProcessor source;
    setParameter (source, "bell.frequency", 7500.0f);
    setParameter (source, "bell.gain", -9.0f);
    setParameter (source, "bell.q", 3.0f);
    juce::MemoryBlock saved;
    source.getStateInformation (saved);
    PluginProcessor restored;
    restored.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    CHECK_THAT (restored.parameters.getRawParameterValue ("bell.frequency")->load(), Catch::Matchers::WithinAbs (7500.0, 0.01));
    CHECK_THAT (restored.parameters.getRawParameterValue ("bell.gain")->load(), Catch::Matchers::WithinAbs (-9.0, 0.001));
    CHECK_THAT (restored.parameters.getRawParameterValue ("bell.q")->load(), Catch::Matchers::WithinAbs (3.0, 0.001));
    for (const auto* invalid : { "nan", "inf", "nonsense", "20001", "1.0garbage", "", "1000,5", "1000 5", "1e999" })
    {
        auto state = source.parameters.copyState();
        state.getChildWithProperty ("id", "bell.frequency").setProperty ("value", invalid, nullptr);
        juce::MemoryBlock bad;
        juce::AudioProcessor::copyXmlToBinary (*state.createXml(), bad);
        restored.setStateInformation (bad.getData(), static_cast<int> (bad.getSize()));
        CHECK_THAT (restored.parameters.getRawParameterValue ("bell.frequency")->load(), Catch::Matchers::WithinAbs (7500.0, 0.01));
    }
    restored.setStateInformation (nullptr, 0);
    const char garbage[] = "not a preset";
    restored.setStateInformation (garbage, sizeof (garbage));
    CHECK_THAT (restored.parameters.getRawParameterValue ("bell.gain")->load(), Catch::Matchers::WithinAbs (-9.0, 0.001));

    auto scientificState = source.parameters.copyState();
    scientificState.getChildWithProperty ("id", "bell.frequency").setProperty ("value", "1.25e3", nullptr);
    juce::MemoryBlock scientific;
    juce::AudioProcessor::copyXmlToBinary (*scientificState.createXml(), scientific);
    restored.setStateInformation (scientific.getData(), static_cast<int> (scientific.getSize()));
    CHECK_THAT (restored.parameters.getRawParameterValue ("bell.frequency")->load(), Catch::Matchers::WithinAbs (1250.0, 0.01));
}

TEST_CASE ("Processor applies host automation to rendered audio", "[bell][automation]")
{
    PluginProcessor processor;
    processor.prepareToPlay (48000.0, 128);
    juce::AudioBuffer<float> buffer (2, 128);
    juce::MidiBuffer midi;
    for (const auto gainDb : { -6.0f, 6.0f })
    {
        setParameter (processor, "bell.gain", gainDb);
        double inputEnergy = 0.0, outputEnergy = 0.0;
        for (int block = 0; block < 100; ++block)
        {
            buffer.clear();
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                buffer.setSample (0, sample, static_cast<float> (0.05 * std::sin (2.0 * std::numbers::pi * 1000.0 * (block * 128 + sample) / 48000.0)));
            const auto input = buffer;
            processor.processBlock (buffer, midi);
            CHECK_THAT (buffer.getMagnitude (1, 0, buffer.getNumSamples()), Catch::Matchers::WithinAbs (0.0, 0.0));
            if (block > 50)
                for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                {
                    inputEnergy += std::pow (input.getSample (0, sample), 2.0);
                    outputEnergy += std::pow (buffer.getSample (0, sample), 2.0);
                }
        }
        CHECK_THAT (10.0 * std::log10 (outputEnergy / inputEnergy), Catch::Matchers::WithinAbs (gainDb, 0.05));
    }
}

TEST_CASE ("Gain automation begins with a ramp instead of a coefficient jump", "[bell][automation]")
{
    openq::BellFilter filter;
    filter.prepare (48000.0, 1000.0, 0.0, 1.0);
    filter.setParameters (1000.0, 24.0, 1.0);
    juce::AudioBuffer<float> buffer (1, 1);
    buffer.setSample (0, 0, 0.1f);
    filter.process (buffer, 1);
    CHECK_THAT (buffer.getSample (0, 0), Catch::Matchers::WithinAbs (0.1, 0.001));
}

TEST_CASE ("Returning a ringing low bell to zero gain has no final reset jump", "[bell][automation]")
{
    openq::BellFilter filter;
    filter.prepare (48000.0, 20.0, 24.0, 10.0);
    juce::AudioBuffer<float> buffer (1, 1);
    float previous = 0.0f;
    for (int sample = 0; sample < 240000; ++sample)
    {
        if (sample == 235001)
            filter.setParameters (20.0, 0.0, 10.0);
        buffer.setSample (0, 0, static_cast<float> (0.05 * std::sin (2.0 * std::numbers::pi * 20.0 * sample / 48000.0)));
        filter.process (buffer, 1);
        if (sample >= 235950 && sample <= 235970)
            CHECK (std::abs (buffer.getSample (0, 0) - previous) < 0.001f);
        previous = buffer.getSample (0, 0);
    }
}
