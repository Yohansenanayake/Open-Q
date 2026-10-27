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

TEST_CASE ("All configured band slots survive recall and reject incomplete states", "[multiband][state]")
{
    PluginProcessor processor;
    REQUIRE (processor.getParameters().size() == static_cast<int> (openq::parameterCount));
    for (size_t band = 0; band < openq::bandCount; ++band)
    {
        const auto initial = openq::getBandParameterPointers (processor.parameters, band).load();
        CHECK (initial.status == (band == 0 ? openq::BandStatus::On : openq::BandStatus::Off));
        setBand (processor, band, "frequency", 100.0f + 13000.0f * static_cast<float> (band) / static_cast<float> (openq::bandCount));
        setBand (processor, band, "gain", -4.0f + 8.0f * static_cast<float> (band) / static_cast<float> (openq::bandCount));
        setBand (processor, band, "q", 0.5f + 8.0f * static_cast<float> (band) / static_cast<float> (openq::bandCount));
        setBand (processor, band, "status", static_cast<float> (band % 3));
        setBand (processor, band, "type", static_cast<float> (band % 5));
        setBand (processor, band, "slope", static_cast<float> (band % openq::cutOrders.size()));
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
        CHECK (actual.status == expected.status);
        CHECK (actual.type == expected.type);
        CHECK (actual.slope == expected.slope);
    }

    for (int corruption = 0; corruption < 22; ++corruption)
    {
        auto state = processor.parameters.copyState();
        state.getChildWithProperty ("id", "band.0.gain").setProperty ("value", 20.0, nullptr);
        auto last = state.getChildWithProperty ("id", openq::bandParameterId (openq::bandCount - 1, "status"));
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
                last.setProperty ("id", "band.0.status", nullptr);
                break;
            case 4:
                state.setProperty ("schemaVersion", 1, nullptr);
                break;
            case 5:
                state.setProperty ("bandCount", static_cast<int> (openq::bandCount - 1), nullptr);
                break;
            case 6:
                state.setProperty ("bellDesign", "unknown", nullptr);
                break;
            case 7:
                last.setProperty ("value", 3, nullptr);
                break;
            case 8:
                last.setProperty ("value", -1, nullptr);
                break;
            case 9:
                state.setProperty ("schemaVersion", 2, nullptr);
                break;
            case 11:
                state.setProperty ("schemaVersion", 3, nullptr);
                break;
            case 12:
                state.setProperty ("shelfDesign", "unknown", nullptr);
                break;
            case 13:
                state.getChildWithProperty ("id", openq::bandParameterId (openq::bandCount - 1, "type")).setProperty ("value", 1.5, nullptr);
                break;
            case 14:
                state.getChildWithProperty ("id", openq::bandParameterId (openq::bandCount - 1, "type")).setProperty ("value", 999, nullptr);
                break;
            case 15:
                state.removeChild (state.getChildWithProperty ("id", openq::bandParameterId (openq::bandCount - 1, "type")), nullptr);
                break;
            case 16:
                // A complete state from a differently sized bank must not partially load.
                state.setProperty ("bandCount", static_cast<int> (openq::bandCount - 1), nullptr);
                for (const auto* name : openq::bandParameterNames)
                    state.removeChild (state.getChildWithProperty ("id", openq::bandParameterId (openq::bandCount - 1, name)), nullptr);
                break;
            case 17:
                state.setProperty ("schemaVersion", 4, nullptr);
                break;
            case 18:
                state.setProperty ("cutDesign", "unknown", nullptr);
                break;
            case 19:
                state.getChildWithProperty ("id", "band.0.slope").setProperty ("value", 1.5, nullptr);
                break;
            case 20:
                state.getChildWithProperty ("id", "band.0.slope").setProperty ("value", static_cast<int> (openq::cutOrders.size()), nullptr);
                break;
            case 21:
                state.removeChild (state.getChildWithProperty ("id", "band.0.slope"), nullptr);
                break;
            case 10:
                last.setProperty ("value", 1.5, nullptr);
                break;
        }
        juce::MemoryBlock bad;
        juce::AudioProcessor::copyXmlToBinary (*state.createXml(), bad);
        restored.setStateInformation (bad.getData(), static_cast<int> (bad.getSize()));
        CHECK_THAT (restored.parameters.getRawParameterValue ("band.0.gain")->load(), Catch::Matchers::WithinAbs (-4.0, 0.001));
    }
}

TEST_CASE ("All configured bells cascade in mono and stereo with independent channel histories", "[multiband][response]")
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
            setBand (processor, band, "status", 2.0f);
            setBand (processor, band, "gain", 3.0f);
        }
        processor.prepareToPlay (48000.0, 128);
        CHECK_THAT (renderGain (processor, channels), Catch::Matchers::WithinAbs (3.0 * static_cast<double> (openq::bandCount), 0.02));

        for (size_t band = 0; band < openq::bandCount; ++band)
            setBand (processor, band, "gain", band % 2 == 0 ? 12.0f : -12.0f);
        CHECK_THAT (renderGain (processor, channels), Catch::Matchers::WithinAbs (openq::bandCount % 2 == 0 ? 0.0 : 12.0, 0.02));

        for (size_t band = 0; band < openq::bandCount; ++band)
            setBand (processor, band, "status", 0.0f);
        CHECK_THAT (renderGain (processor, channels), Catch::Matchers::WithinAbs (0.0, 1e-9));
        setBand (processor, openq::bandCount - 1, "gain", 6.0f);
        setBand (processor, openq::bandCount - 1, "status", 2.0f);
        CHECK_THAT (renderGain (processor, channels), Catch::Matchers::WithinAbs (6.0, 0.02));
        processor.releaseResources();
    }
}

TEST_CASE ("All band status transitions retain settings and only On applies EQ", "[multiband][automation]")
{
    PluginProcessor processor;
    setBand (processor, 1, "frequency", 4000.0f);
    setBand (processor, 1, "gain", 12.0f);
    setBand (processor, 1, "q", 0.5f);
    processor.prepareToPlay (48000.0, 128);
    for (const auto status : { openq::BandStatus::On, openq::BandStatus::Bypass, openq::BandStatus::On, openq::BandStatus::Off, openq::BandStatus::Bypass, openq::BandStatus::Off, openq::BandStatus::On })
    {
        CAPTURE (static_cast<int> (status));
        setBand (processor, 1, "status", static_cast<float> (status));
        CHECK_THAT (renderGain (processor, 2, 4000.0), Catch::Matchers::WithinAbs (status == openq::BandStatus::On ? 12.0 : 0.0, 0.02));
        const auto settings = openq::getBandParameterPointers (processor.parameters, 1).load();
        CHECK (settings.status == status);
        CHECK_THAT (settings.frequency, Catch::Matchers::WithinAbs (4000.0, 0.001));
        CHECK_THAT (settings.gain, Catch::Matchers::WithinAbs (12.0, 0.001));
        CHECK_THAT (settings.q, Catch::Matchers::WithinAbs (0.5, 0.001));
    }
}

TEST_CASE ("Host tail allowance covers the configured cascade of ringing bells", "[multiband][tail]")
{
    PluginProcessor processor;
    for (size_t band = 0; band < openq::bandCount; ++band)
    {
        setBand (processor, band, "frequency", 20.0f);
        setBand (processor, band, "gain", 24.0f);
        setBand (processor, band, "q", 10.0f);
        setBand (processor, band, "status", 2.0f);
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
        if (block == static_cast<int> (5.0 * 8000.0 / buffer.getNumSamples()))
            peakAtFiveSeconds = buffer.getMagnitude (0, buffer.getNumSamples());
    }
    CHECK (peakAtFiveSeconds > 0.001f);
    CHECK (buffer.getMagnitude (0, buffer.getNumSamples()) < 1e-6f);
}

TEST_CASE ("Disabled bells settle new settings and resume with fresh coefficients and no stale ringing", "[multiband][automation]")
{
    openq::BandFilter filter;
    filter.prepare (48000.0, 20.0, 24.0, 10.0);
    juce::AudioBuffer<float> sample (1, 1);
    float previous = 0.0f;
    for (int i = 0; i < 241000; ++i)
    {
        if (i == 240000)
            filter.setParameters (20.0, 24.0, 10.0, openq::BandStatus::Off);
        const auto input = static_cast<float> (0.05 * std::sin (2.0 * std::numbers::pi * 20.0 * i / 48000.0));
        sample.setSample (0, 0, input);
        filter.process (sample, 1);
        if (i >= 240000)
            REQUIRE (std::abs (sample.getSample (0, 0) - previous) < 0.005f);
        if (i >= 240960)
            REQUIRE (juce::exactlyEqual (sample.getSample (0, 0), input));
        previous = sample.getSample (0, 0);
    }
    filter.setParameters (4000.0, 12.0, 0.5, openq::BandStatus::Off);
    juce::AudioBuffer<float> silence (1, 4096);
    silence.clear();
    filter.process (silence, 1);
    filter.setParameters (4000.0, 12.0, 0.5, openq::BandStatus::On);
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

TEST_CASE ("Bypass tracks the live signal and parameter automation before fading back to On", "[multiband][automation]")
{
    PluginProcessor processor;
    setBand (processor, 1, "frequency", 80.0f);
    setBand (processor, 1, "gain", 18.0f);
    setBand (processor, 1, "q", 5.0f);
    setBand (processor, 1, "status", 1.0f);
    processor.prepareToPlay (48000.0, 1);
    openq::BandFilter reference;
    reference.prepare (48000.0, 80.0, 18.0, 5.0);
    juce::AudioBuffer<float> actual (2, 1), filtered (2, 1);
    juce::MidiBuffer midi;
    for (int i = 0; i < 9000; ++i)
    {
        if (i == 2500)
        {
            setBand (processor, 1, "frequency", 120.0f);
            setBand (processor, 1, "gain", 12.0f);
            setBand (processor, 1, "q", 3.0f);
            reference.setParameters (120.0, 12.0, 3.0);
        }
        if (i == 5000)
            setBand (processor, 1, "status", 2.0f);
        if (i == 7000)
            setBand (processor, 1, "status", 1.0f);
        for (int channel = 0; channel < 2; ++channel)
        {
            const auto input = static_cast<float> (0.01 * std::sin (2.0 * std::numbers::pi * (80.0 + channel * 40.0) * i / 48000.0));
            actual.setSample (channel, 0, input);
            filtered.setSample (channel, 0, input);
        }
        const auto input = actual;
        reference.process (filtered, 2);
        processor.processBlock (actual, midi);
        const auto mix = i < 5000 ? 0.0 : i < 7000 ? std::min (1.0, (i - 4999) / 960.0)
                                                   : std::max (0.0, 1.0 - (i - 6999) / 960.0);
        for (int channel = 0; channel < 2; ++channel)
        {
            const auto dry = input.getSample (channel, 0);
            const auto expected = dry + mix * (filtered.getSample (channel, 0) - dry);
            REQUIRE_THAT (actual.getSample (channel, 0), Catch::Matchers::WithinAbs (expected, 1e-7));
            if (juce::exactlyEqual (mix, 0.0))
                REQUIRE (juce::exactlyEqual (actual.getSample (channel, 0), dry));
        }
    }
}

TEST_CASE ("Off to Bypass refreshes settled coefficients and retains new ringing for On", "[multiband][automation]")
{
    openq::BandFilter dormant, reference;
    dormant.prepare (48000.0, 1000.0, 6.0, 1.0, openq::BandStatus::Off);
    dormant.setParameters (20.0, 24.0, 10.0, openq::BandStatus::Off);
    juce::AudioBuffer<float> silence (1, 2048);
    silence.clear();
    dormant.process (silence, 1);
    dormant.setParameters (20.0, 24.0, 10.0, openq::BandStatus::Bypass);
    reference.prepare (48000.0, 20.0, 24.0, 10.0);
    juce::AudioBuffer<float> actual (1, 1), filtered (1, 1);
    double audibleTail = 0.0;
    for (int i = 0; i < 2048; ++i)
    {
        if (i == 128)
            dormant.setParameters (20.0, 24.0, 10.0, openq::BandStatus::On);
        const auto input = i == 0 ? 1.0f : 0.0f;
        actual.setSample (0, 0, input);
        filtered.setSample (0, 0, input);
        dormant.process (actual, 1);
        reference.process (filtered, 1);
        const auto mix = i < 128 ? 0.0 : std::min (1.0, (i - 127) / 960.0);
        const auto expected = input + mix * (filtered.getSample (0, 0) - input);
        REQUIRE_THAT (actual.getSample (0, 0), Catch::Matchers::WithinAbs (expected, 1e-7));
        audibleTail += std::abs (actual.getSample (0, 0) - input);
    }
    CHECK (audibleTail > 0.01);
}

TEST_CASE ("Rapid band status automation stays finite and independent of block boundaries", "[multiband][automation]")
{
    openq::BandFilter whole, split;
    whole.prepare (48000.0, 80.0, 18.0, 5.0);
    split.prepare (48000.0, 80.0, 18.0, 5.0);
    float previous = 0.0f;
    for (int segment = 0; segment < 40; ++segment)
    {
        const auto status = static_cast<openq::BandStatus> (segment % 3);
        whole.setParameters (80.0, 18.0, 5.0, status);
        split.setParameters (80.0, 18.0, 5.0, status);
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

TEST_CASE ("Shared controls select all configured slots without overwriting other bands", "[multiband][ui]")
{
    PluginProcessor processor;
    for (size_t band = 0; band < openq::bandCount; ++band)
    {
        setBand (processor, band, "gain", static_cast<float> (band) / static_cast<float> (openq::bandCount));
        setBand (processor, band, "status", 2.0f);
    }
    processor.prepareToPlay (48000.0, 128);
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    auto* gain = dynamic_cast<juce::Slider*> (editor->findChildWithID ("band.card")->findChildWithID ("band.gain"));
    auto* power = dynamic_cast<juce::DrawableButton*> (editor->findChildWithID ("band.card")->findChildWithID ("band.power"));
    REQUIRE (gain != nullptr);
    REQUIRE (power != nullptr);
    auto* type = dynamic_cast<juce::ComboBox*> (editor->findChildWithID ("band.card")->findChildWithID ("band.type"));
    REQUIRE (type != nullptr);
    auto* graph = dynamic_cast<openq::ResponseGraph*> (editor->findChildWithID ("response.graph"));
    REQUIRE (graph != nullptr);
    for (size_t band = 0; band < openq::bandCount; ++band)
    {
        auto* button = dynamic_cast<juce::TextButton*> (graph->findChildWithID ("node." + juce::String (band)));
        REQUIRE (button != nullptr);
        button->onClick();
        CHECK (button->getToggleState());
        CHECK (type->getSelectedItemIndex() == 0);
        type->setSelectedItemIndex (2, juce::sendNotificationSync);
        CHECK (openq::getBandParameterPointers (processor.parameters, band).load().type == openq::FilterType::HighShelf);
        setBand (processor, band, "type", 1.0f);
        CHECK (type->getSelectedItemIndex() == 1);
        type->setSelectedItemIndex (0, juce::sendNotificationSync);
        CHECK_THAT (gain->getValue(), Catch::Matchers::WithinAbs ((static_cast<double> (band) / static_cast<double> (openq::bandCount)), 0.001));
        gain->setValue (-(static_cast<double> (band) / static_cast<double> (openq::bandCount)), juce::sendNotificationSync);
        for (const bool on : { false, true })
        {
            power->setToggleState (on, juce::sendNotificationSync);
            const auto settings = openq::getBandParameterPointers (processor.parameters, band).load();
            CHECK (settings.status == (on ? openq::BandStatus::On : openq::BandStatus::Bypass));
            CHECK_THAT (settings.gain, Catch::Matchers::WithinAbs (-(static_cast<double> (band) / static_cast<double> (openq::bandCount)), 0.001));
        }
    }
    for (size_t band = 0; band < openq::bandCount; ++band)
    {
        const auto target = openq::getBandParameterPointers (processor.parameters, band).load();
        CHECK_THAT (target.gain, Catch::Matchers::WithinAbs (-(static_cast<double> (band) / static_cast<double> (openq::bandCount)), 0.001));
        CHECK (target.status == openq::BandStatus::On);
    }
    setBand (processor, openq::bandCount - 1, "status", 1.0f);
    CHECK_FALSE (power->getToggleState());
    setBand (processor, openq::bandCount - 1, "status", 0.0f);
    CHECK_FALSE (power->getToggleState());
    CHECK_FALSE (power->isEnabled());
    power->onClick();
    CHECK (openq::getBandParameterPointers (processor.parameters, openq::bandCount - 1).load().status == openq::BandStatus::Off);
    setBand (processor, openq::bandCount - 1, "status", 2.0f);
    CHECK (power->getToggleState());
    CHECK (power->isEnabled());
    editor.reset();
    processor.prepareToPlay (48000.0, 128);
    CHECK_THAT (renderGain (processor, 2), Catch::Matchers::WithinAbs (-static_cast<double> (openq::bandCount - 1) / 2.0, 0.05));
}

TEST_CASE ("Mixed bell and shelf bands cascade through processor audio", "[shelf][multiband]")
{
    PluginProcessor processor;
    for (size_t band = 0; band < openq::bandCount; ++band)
    {
        setBand (processor, band, "frequency", 500.0f + 10000.0f * static_cast<float> (band) / static_cast<float> (openq::bandCount));
        setBand (processor, band, "gain", band % 2 == 0 ? 3.0f : -3.0f);
        setBand (processor, band, "type", static_cast<float> (band % 3));
        setBand (processor, band, "status", 2.0f);
    }
    processor.prepareToPlay (48000.0, 128);
    for (double probe : { 100.0, 1000.0, 10000.0 })
    {
        double expected = 0.0;
        for (size_t band = 0; band < openq::bandCount; ++band)
        {
            const auto s = openq::getBandParameterPointers (processor.parameters, band).load();
            expected += openq::responseMagnitudeDb (openq::DigitalResponse { openq::makeFilterCoefficients (s.type, 48000.0, s.frequency, s.gain, s.q), 48000.0 }, probe);
        }
        CHECK_THAT (renderGain (processor, 2, probe), Catch::Matchers::WithinAbs (expected, 0.03));
    }
}

TEST_CASE ("Host tail allowance covers the configured cascade of resonant shelves", "[shelf][tail]")
{
    for (auto type : { openq::FilterType::LowShelf, openq::FilterType::HighShelf })
    {
        PluginProcessor processor;
        for (size_t band = 0; band < openq::bandCount; ++band)
        {
            setBand (processor, band, "frequency", 20.0f);
            setBand (processor, band, "gain", type == openq::FilterType::LowShelf ? 24.0f : -24.0f);
            setBand (processor, band, "q", 10.0f);
            setBand (processor, band, "type", static_cast<float> (type));
            setBand (processor, band, "status", 2.0f);
        }
        processor.prepareToPlay (8000.0, 128);
        juce::AudioBuffer<float> buffer (1, 128);
        juce::MidiBuffer midi;
        constexpr float impulseAmplitude = 1e-20f; // Headroom for stacked high-Q shelf resonance.
        const auto blocks = static_cast<int> (processor.getTailLengthSeconds() * 8000.0 / 128.0);
        for (int block = 0; block < blocks; ++block)
        {
            buffer.clear();
            if (block == 0)
                buffer.setSample (0, 0, impulseAmplitude);
            processor.processBlock (buffer, midi);
            REQUIRE (std::isfinite (buffer.getMagnitude (0, 128)));
        }
        CHECK (buffer.getMagnitude (0, 128) < impulseAmplitude * 0.1f);
    }
}

TEST_CASE ("Shared band constants preserve registered parameter ranges defaults and state schema", "[multiband][state]")
{
    PluginProcessor processor;
    for (size_t band = 0; band < openq::bandCount; ++band)
    {
        const auto settings = openq::getBandParameterPointers (processor.parameters, band).load();
        // APVTS normalizes float defaults, introducing a small round-trip error.
        CHECK_THAT (settings.frequency, Catch::Matchers::WithinAbs (1000.0, 0.001));
        CHECK_THAT (settings.gain, Catch::Matchers::WithinAbs (0.0, 0.0));
        CHECK_THAT (settings.q, Catch::Matchers::WithinAbs (1.0, 1e-6));
        const auto& frequency = processor.parameters.getParameter (openq::bandParameterId (band, "frequency"))->getNormalisableRange();
        const auto& gain = processor.parameters.getParameter (openq::bandParameterId (band, "gain"))->getNormalisableRange();
        const auto& q = processor.parameters.getParameter (openq::bandParameterId (band, "q"))->getNormalisableRange();
        CHECK_THAT (frequency.start, Catch::Matchers::WithinAbs (20.0, 0.0));
        CHECK_THAT (frequency.end, Catch::Matchers::WithinAbs (20000.0, 0.0));
        CHECK_THAT (frequency.convertFrom0to1 (0.5f), Catch::Matchers::WithinAbs (1000.0, 0.001));
        CHECK_THAT (gain.start, Catch::Matchers::WithinAbs (-24.0, 0.0));
        CHECK_THAT (gain.end, Catch::Matchers::WithinAbs (24.0, 0.0));
        CHECK_THAT (q.start, Catch::Matchers::WithinAbs (0.1, 1e-7));
        CHECK_THAT (q.end, Catch::Matchers::WithinAbs (10.0, 0.0));
        CHECK_THAT (q.convertFrom0to1 (0.5f), Catch::Matchers::WithinAbs (1.0, 1e-6));
    }
    const auto state = processor.parameters.copyState();
    CHECK (static_cast<int> (state.getProperty ("schemaVersion")) == openq::stateSchemaVersion);
    CHECK (state.getProperty ("bellDesign").toString() == "zle-ivantsov-v1");
    CHECK (state.getProperty ("shelfDesign").toString() == "zle-ivantsov-shelf-v1");
    CHECK (state.getNumChildren() == static_cast<int> (openq::parameterCount));
}
