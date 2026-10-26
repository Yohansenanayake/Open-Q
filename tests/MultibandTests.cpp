#include "PluginEditor.h"
#include "ui/ResponseWorker.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <numbers>

namespace
{
    void setBand (PluginProcessor& processor, size_t band, const char* name, float value)
    {
        auto* parameter = processor.parameters.getParameter (openq::bandParameterId (band, name));
        REQUIRE (parameter != nullptr);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    }

    double renderGain (PluginProcessor& processor, int channels, double frequency = 1000.0)
    {
        juce::AudioBuffer<float> buffer (channels, 128);
        juce::MidiBuffer midi;
        double inputEnergy = 0.0, outputEnergy = 0.0;
        for (int block = 0; block < 200; ++block)
        {
            buffer.clear();
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                buffer.setSample (0, sample, static_cast<float> (0.01 * std::sin (2.0 * std::numbers::pi * frequency * (block * 128 + sample) / 48000.0)));
            const auto input = buffer;
            processor.processBlock (buffer, midi);
            if (channels == 2)
                REQUIRE (juce::exactlyEqual (buffer.getMagnitude (1, 0, buffer.getNumSamples()), 0.0f));
            if (block > 100)
                for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                {
                    inputEnergy += std::pow (input.getSample (0, sample), 2.0);
                    outputEnergy += std::pow (buffer.getSample (0, sample), 2.0);
                }
        }
        return 10.0 * std::log10 (outputEnergy / inputEnergy);
    }
}

TEST_CASE ("Eight independent band slots survive recall and reject incomplete states", "[multiband][state]")
{
    PluginProcessor processor;
    REQUIRE (processor.getParameters().size() == 32);
    for (size_t band = 0; band < openq::bandCount; ++band)
    {
        const auto initial = openq::getBandParameterPointers (processor.parameters, band).load();
        CHECK (initial.enabled == (band == 0));
        setBand (processor, band, "frequency", 100.0f + static_cast<float> (band) * 1300.0f);
        setBand (processor, band, "gain", static_cast<float> (band) - 4.0f);
        setBand (processor, band, "q", 0.5f + static_cast<float> (band));
        setBand (processor, band, "enabled", band % 2 == 0 ? 1.0f : 0.0f);
    }
    juce::MemoryBlock saved;
    processor.getStateInformation (saved);
    PluginProcessor restored;
    restored.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    for (size_t band = 0; band < openq::bandCount; ++band)
    {
        const auto expected = openq::getBandParameterPointers (processor.parameters, band).load();
        const auto actual = openq::getBandParameterPointers (restored.parameters, band).load();
        CHECK_THAT (actual.frequency, Catch::Matchers::WithinAbs (expected.frequency, 0.001));
        CHECK_THAT (actual.gain, Catch::Matchers::WithinAbs (expected.gain, 0.001));
        CHECK_THAT (actual.q, Catch::Matchers::WithinAbs (expected.q, 0.001));
        CHECK (actual.enabled == expected.enabled);
    }

    for (int corruption = 0; corruption < 7; ++corruption)
    {
        auto state = processor.parameters.copyState();
        state.getChildWithProperty ("id", "band.0.gain").setProperty ("value", 20.0, nullptr);
        auto last = state.getChildWithProperty ("id", "band.7.enabled");
        switch (corruption)
        {
            case 0:
                state.removeChild (last, nullptr);
                break;
            case 1:
                last.setProperty ("value", 0.5, nullptr);
                break;
            case 2:
                last.setProperty ("value", "nan", nullptr);
                break;
            case 3:
                last.setProperty ("id", "band.0.enabled", nullptr);
                break;
            case 4:
                state.setProperty ("schemaVersion", 1, nullptr);
                break;
            case 5:
                state.setProperty ("bandCount", 4, nullptr);
                break;
            case 6:
                state.setProperty ("bellDesign", "unknown", nullptr);
                break;
        }
        juce::MemoryBlock bad;
        juce::AudioProcessor::copyXmlToBinary (*state.createXml(), bad);
        restored.setStateInformation (bad.getData(), static_cast<int> (bad.getSize()));
        CHECK_THAT (restored.parameters.getRawParameterValue ("band.0.gain")->load(), Catch::Matchers::WithinAbs (-4.0, 0.001));
    }
}

TEST_CASE ("All eight bells cascade in mono and stereo with independent channel histories", "[multiband][response]")
{
    for (const int channels : { 1, 2 })
    {
        PluginProcessor processor;
        auto layout = processor.getBusesLayout();
        layout.inputBuses.set (0, channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
        layout.outputBuses.set (0, layout.inputBuses[0]);
        REQUIRE (processor.setBusesLayout (layout));
        for (size_t band = 0; band < openq::bandCount; ++band)
        {
            setBand (processor, band, "enabled", 1.0f);
            setBand (processor, band, "gain", 3.0f);
        }
        processor.prepareToPlay (48000.0, 128);
        CHECK_THAT (renderGain (processor, channels), Catch::Matchers::WithinAbs (24.0, 0.02));

        for (size_t band = 0; band < openq::bandCount; ++band)
            setBand (processor, band, "gain", band % 2 == 0 ? 12.0f : -12.0f);
        CHECK_THAT (renderGain (processor, channels), Catch::Matchers::WithinAbs (0.0, 0.02));

        for (size_t band = 0; band < openq::bandCount; ++band)
            setBand (processor, band, "enabled", 0.0f);
        CHECK_THAT (renderGain (processor, channels), Catch::Matchers::WithinAbs (0.0, 1e-9));
        setBand (processor, 7, "gain", 6.0f);
        setBand (processor, 7, "enabled", 1.0f);
        CHECK_THAT (renderGain (processor, channels), Catch::Matchers::WithinAbs (6.0, 0.02));
        processor.releaseResources();
    }
}

TEST_CASE ("Host tail allowance covers eight cascaded ringing bells", "[multiband][tail]")
{
    PluginProcessor processor;
    for (size_t band = 0; band < openq::bandCount; ++band)
    {
        setBand (processor, band, "frequency", 20.0f);
        setBand (processor, band, "gain", 24.0f);
        setBand (processor, band, "q", 10.0f);
        setBand (processor, band, "enabled", 1.0f);
    }
    processor.prepareToPlay (8000.0, 128);
    juce::AudioBuffer<float> buffer (1, 128);
    juce::MidiBuffer midi;
    const auto blocks = static_cast<int> (processor.getTailLengthSeconds() * 8000.0 / 128.0);
    float peakAtFiveSeconds = 0.0f;
    for (int block = 0; block < blocks; ++block)
    {
        buffer.clear();
        if (block == 0)
            buffer.setSample (0, 0, 1e-5f);
        processor.processBlock (buffer, midi);
        REQUIRE (std::isfinite (buffer.getMagnitude (0, buffer.getNumSamples())));
        if (block == 312)
            peakAtFiveSeconds = buffer.getMagnitude (0, buffer.getNumSamples());
    }
    CHECK (peakAtFiveSeconds > 0.001f);
    CHECK (buffer.getMagnitude (0, buffer.getNumSamples()) < 1e-6f);
}

TEST_CASE ("Disabled bells settle new settings and resume with fresh coefficients and no stale ringing", "[multiband][automation]")
{
    openq::BellFilter filter;
    filter.prepare (48000.0, 20.0, 24.0, 10.0);
    juce::AudioBuffer<float> sample (1, 1);
    float previous = 0.0f;
    for (int i = 0; i < 241000; ++i)
    {
        if (i == 240000)
            filter.setParameters (20.0, 24.0, 10.0, false);
        const auto input = static_cast<float> (0.05 * std::sin (2.0 * std::numbers::pi * 20.0 * i / 48000.0));
        sample.setSample (0, 0, input);
        filter.process (sample, 1);
        if (i >= 240000)
            REQUIRE (std::abs (sample.getSample (0, 0) - previous) < 0.005f);
        if (i >= 240960)
            REQUIRE (juce::exactlyEqual (sample.getSample (0, 0), input));
        previous = sample.getSample (0, 0);
    }
    filter.setParameters (4000.0, 12.0, 0.5, false);
    juce::AudioBuffer<float> silence (1, 4096);
    silence.clear();
    filter.process (silence, 1);
    filter.setParameters (4000.0, 12.0, 0.5, true);
    filter.process (silence, 1);
    CHECK (juce::exactlyEqual (silence.getMagnitude (0, silence.getNumSamples()), 0.0f));

    juce::AudioBuffer<float> buffer (1, 128);
    double inputEnergy = 0.0, outputEnergy = 0.0;
    for (int block = 0; block < 100; ++block)
    {
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample (0, i, static_cast<float> (0.01 * std::sin (2.0 * std::numbers::pi * 4000.0 * (block * 128 + i) / 48000.0)));
        const auto input = buffer;
        filter.process (buffer, 1);
        if (block > 50)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                inputEnergy += std::pow (input.getSample (0, i), 2.0);
                outputEnergy += std::pow (buffer.getSample (0, i), 2.0);
            }
    }
    CHECK_THAT (10.0 * std::log10 (outputEnergy / inputEnergy), Catch::Matchers::WithinAbs (12.0, 0.02));
}

TEST_CASE ("Rapid enable automation stays finite and independent of block boundaries", "[multiband][automation]")
{
    openq::BellFilter whole, split;
    whole.prepare (48000.0, 80.0, 18.0, 5.0);
    split.prepare (48000.0, 80.0, 18.0, 5.0);
    float previous = 0.0f;
    for (int segment = 0; segment < 40; ++segment)
    {
        const bool enabled = segment % 2 == 0;
        whole.setParameters (80.0, 18.0, 5.0, enabled);
        split.setParameters (80.0, 18.0, 5.0, enabled);
        juce::AudioBuffer<float> buffer (1, 137);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample (0, i, static_cast<float> (0.01 * std::sin (2.0 * std::numbers::pi * 80.0 * (segment * 137 + i) / 48000.0)));
        auto expected = buffer;
        whole.process (expected, 1);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            float* data = buffer.getWritePointer (0, i);
            juce::AudioBuffer<float> one (&data, 1, 1);
            split.process (one, 1);
            REQUIRE (std::isfinite (*data));
            REQUIRE_THAT (*data, Catch::Matchers::WithinAbs (expected.getSample (0, i), 1e-7));
            REQUIRE (std::abs (*data - previous) < 0.01f);
            previous = *data;
        }
    }
}

TEST_CASE ("Shared controls select all eight slots without overwriting other bands", "[multiband][ui]")
{
    PluginProcessor processor;
    for (size_t band = 0; band < openq::bandCount; ++band)
        setBand (processor, band, "gain", static_cast<float> (band));
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    auto* gain = dynamic_cast<juce::Slider*> (editor->findChildWithID ("band.gain"));
    auto* enabled = dynamic_cast<juce::ToggleButton*> (editor->findChildWithID ("band.enabled"));
    REQUIRE (gain != nullptr);
    REQUIRE (enabled != nullptr);
    for (size_t band = 0; band < openq::bandCount; ++band)
    {
        auto* button = dynamic_cast<juce::TextButton*> (editor->findChildWithID ("select.band." + juce::String (band)));
        REQUIRE (button != nullptr);
        button->onClick();
        CHECK (button->getToggleState());
        CHECK_THAT (gain->getValue(), Catch::Matchers::WithinAbs (static_cast<double> (band), 0.001));
        gain->setValue (-static_cast<double> (band), juce::sendNotificationSync);
        enabled->setToggleState (true, juce::sendNotificationSync);
    }
    for (size_t band = 0; band < openq::bandCount; ++band)
    {
        const auto target = openq::getBandParameterPointers (processor.parameters, band).load();
        CHECK_THAT (target.gain, Catch::Matchers::WithinAbs (-static_cast<double> (band), 0.001));
        CHECK (target.enabled);
    }
    editor.reset();
    processor.prepareToPlay (48000.0, 128);
    CHECK_THAT (renderGain (processor, 2), Catch::Matchers::WithinAbs (-28.0, 0.05));
}
