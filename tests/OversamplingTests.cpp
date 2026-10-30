// SPDX-License-Identifier: AGPL-3.0-only
#include "PluginEditor.h"
#include "ui/presets/preset_json.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <chrono>
#include <cmath>
#include <limits>
#include <numbers>
#include <thread>

namespace openq
{
    struct ProcessingConfigurationTestAccess
    {
        static void beginPublication (ProcessingConfiguration& configuration) { configuration.sequence.fetch_add (1); }
        static void endPublication (ProcessingConfiguration& configuration) { configuration.sequence.fetch_add (1); }
    };
}

namespace
{
    void parameter (PluginProcessor& processor, const char* id, float value)
    {
        auto* target = processor.parameters.getParameter (id);
        REQUIRE (target != nullptr);
        target->setValueNotifyingHost (target->convertTo0to1 (value));
    }

    juce::ValueTree savedState (PluginProcessor& processor)
    {
        juce::MemoryBlock data;
        processor.getStateInformation (data);
        return juce::ValueTree::fromXml (*juce::AudioProcessor::getXmlFromBinary (data.getData(), static_cast<int> (data.getSize())));
    }

    bool restore (PluginProcessor& processor, const juce::ValueTree& state)
    {
        juce::MemoryBlock data;
        juce::AudioProcessor::copyXmlToBinary (*state.createXml(), data);
        return processor.restoreStateInformation (data.getData(), static_cast<int> (data.getSize()));
    }

    void fill (juce::AudioBuffer<float>& buffer, float value = 0.125f)
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                buffer.setSample (channel, sample, value * static_cast<float> (channel + 1));
    }

    void settle (PluginProcessor& processor, int mode, bool* inAudio = nullptr)
    {
        juce::MidiBuffer midi;
        juce::AudioBuffer<float> buffer (2, 256);
        for (int iteration = 0; iteration < 100; ++iteration)
        {
            fill (buffer);
            if (inAudio != nullptr)
                *inAudio = true;
            processor.processBlock (buffer, midi);
            if (inAudio != nullptr)
                *inAudio = false;
            juce::MessageManager::getInstance()->runDispatchLoopUntil (2);
            if (processor.getActiveOversamplingMode() == mode && !processor.isOversamplingPending())
            {
                for (int i = 0; i < 10; ++i)
                {
                    fill (buffer);
                    if (inAudio != nullptr)
                        *inAudio = true;
                    processor.processBlock (buffer, midi);
                    if (inAudio != nullptr)
                        *inAudio = false;
                }
                return;
            }
        }
        FAIL ("Oversampling handoff did not complete");
    }
}

TEST_CASE ("Oversampling matches JUCE conversion across stream boundaries", "[oversampling][audio]")
{
    for (const int channels : { 1, 2 })
        for (const int mode : { 0, 1, 2 })
            for (const double rate : { 8000.0, 44100.0, 48000.0, 96000.0, 192000.0 })
            {
                CAPTURE (channels, mode, rate);
                PluginProcessor processor;
                const auto layout = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
                REQUIRE (processor.setBusesLayout ({ { layout }, { layout } }));
                REQUIRE (processor.setOversamplingMode (mode));
                processor.prepareToPlay (rate, 16);
                juce::dsp::Oversampling<float> reference (static_cast<size_t> (channels), static_cast<size_t> (mode), juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
                reference.initProcessing (openq::processingChunkSamples);
                CHECK (processor.getLatencySamples() == juce::roundToInt (reference.getLatencyInSamples()));
                juce::MidiBuffer midi;
                int position = 0;
                for (const int size : { 0, 1, 31, 256, 257, 4097 })
                {
                    juce::AudioBuffer<float> actual (channels, size);
                    for (int channel = 0; channel < channels; ++channel)
                        for (int sample = 0; sample < size; ++sample)
                            actual.setSample (channel, sample, static_cast<float> (0.1 * std::sin ((position + sample) * (channel + 1) * 0.137)));
                    position += size;
                    juce::AudioBuffer<float> expected (actual);
                    if (mode != 0)
                        for (int offset = 0; offset < size; offset += openq::processingChunkSamples)
                        {
                            juce::dsp::AudioBlock<float> block (expected);
                            auto chunk = block.getSubBlock (static_cast<size_t> (offset), static_cast<size_t> (std::min (openq::processingChunkSamples, size - offset)));
                            (void) reference.processSamplesUp (chunk);
                            reference.processSamplesDown (chunk);
                        }
                    processor.processBlock (actual, midi);
                    for (int channel = 0; channel < channels; ++channel)
                        for (int sample = 0; sample < size; ++sample)
                            REQUIRE (juce::exactlyEqual (actual.getSample (channel, sample), expected.getSample (channel, sample)));
                }
                processor.releaseResources();
                processor.prepareToPlay (rate, 1024);
                CHECK (processor.getActiveOversamplingMode() == mode);
            }
}

TEST_CASE ("Oversampled global bypass returns exactly delayed raw input despite trims", "[oversampling][bypass]")
{
    for (const int mode : { 1, 2 })
    {
        PluginProcessor processor;
        REQUIRE (processor.setOversamplingMode (mode));
        parameter (processor, "global.bypass", 1.0f);
        parameter (processor, "input.gain", 12.0f);
        parameter (processor, "output.gain", -7.0f);
        parameter (processor, "band.0.gain", 18.0f);
        processor.prepareToPlay (48000.0, 16);
        const auto delay = processor.getLatencySamples();
        REQUIRE (delay > 0);
        juce::MidiBuffer midi;
        std::array<std::vector<float>, 2> input;
        int position = 0;
        for (const int size : { 1, 31, 257, 4097 })
        {
            juce::AudioBuffer<float> buffer (2, size);
            for (int channel = 0; channel < 2; ++channel)
                for (int sample = 0; sample < size; ++sample)
                {
                    const auto value = static_cast<float> (0.1 * std::sin ((position + sample + 1) * (channel + 1) * 0.17));
                    input[static_cast<size_t> (channel)].push_back (value);
                    buffer.setSample (channel, sample, value);
                }
            processor.processBlockBypassed (buffer, midi);
            for (int channel = 0; channel < 2; ++channel)
                for (int sample = 0; sample < size; ++sample)
                {
                    const auto index = position + sample - delay;
                    const auto expected = index < 0 ? 0.0f : input[static_cast<size_t> (channel)][static_cast<size_t> (index)];
                    REQUIRE (juce::exactlyEqual (buffer.getSample (channel, sample), expected));
                }
            position += size;
        }
    }
}

TEST_CASE ("Oversampling switches with editor closed and defers offline changes", "[oversampling][threading]")
{
    struct Listener final : juce::AudioProcessorListener
    {
        bool inAudio = false, notifiedInAudio = false;
        int latencyChanges = 0;
        void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override {}
        void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails& details) override
        {
            if (details.latencyChanged)
            {
                ++latencyChanges;
                notifiedInAudio |= inAudio;
            }
        }
    } listener;
    PluginProcessor processor;
    processor.prepareToPlay (48000, 64);
    processor.addListener (&listener);
    REQUIRE (processor.setOversamplingMode (2));
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buffer (2, 1024);
    fill (buffer);
    listener.inAudio = true;
    processor.processBlock (buffer, midi);
    listener.inAudio = false;
    CHECK (juce::exactlyEqual (buffer.getSample (0, 959), 0.0f));
    CHECK (processor.getActiveOversamplingMode() == 0);
    CHECK (processor.getLatencySamples() == 0);
    CHECK (processor.isOversamplingPending());
    settle (processor, 2, &listener.inAudio);
    CHECK (processor.getLatencySamples() > 0);
    REQUIRE (processor.setOversamplingMode (1));
    REQUIRE (processor.setOversamplingMode (0));
    settle (processor, 0, &listener.inAudio);
    CHECK (processor.getLatencySamples() == 0);
    CHECK (listener.latencyChanges >= 2);
    CHECK_FALSE (listener.notifiedInAudio);
    processor.setNonRealtime (true);
    REQUIRE (processor.setOversamplingMode (2));
    fill (buffer);
    processor.processBlock (buffer, midi);
    CHECK (processor.getActiveOversamplingMode() == 0);
    CHECK (juce::exactlyEqual (buffer.getSample (0, buffer.getNumSamples() - 1), 0.125f));
    processor.prepareToPlay (48000, 64);
    CHECK (processor.getActiveOversamplingMode() == 2);
    CHECK_FALSE (processor.isOversamplingPending());
    processor.setNonRealtime (false);
    REQUIRE (processor.setOversamplingMode (0));
    fill (buffer);
    processor.processBlock (buffer, midi);
    fill (buffer);
    processor.processBlock (buffer, midi);
    processor.setNonRealtime (true);
    fill (buffer);
    processor.processBlock (buffer, midi);
    CHECK (processor.getActiveOversamplingMode() == 2);
    CHECK_THAT (buffer.getSample (0, buffer.getNumSamples() - 1), Catch::Matchers::WithinAbs (0.125, 1e-5));
    CHECK (processor.isOversamplingPending());
    processor.removeListener (&listener);
}

TEST_CASE ("Oversampling sessions migrate schema seven and reject malformed quality atomically", "[oversampling][state]")
{
    PluginProcessor processor;
    REQUIRE (processor.setOversamplingMode (2));
    auto state = savedState (processor);
    PluginProcessor restored;
    REQUIRE (restore (restored, state));
    CHECK (restored.getOversamplingMode() == 2);
    for (const auto* invalid : { "-1", "3", "0.5", "nan", "1 trailing" })
    {
        auto bad = state.createCopy();
        bad.setProperty ("oversamplingMode", invalid, nullptr);
        bad.getChildWithProperty ("id", "band.0.gain").setProperty ("value", 12, nullptr);
        CHECK_FALSE (restore (restored, bad));
        CHECK (restored.getOversamplingMode() == 2);
        CHECK (juce::exactlyEqual (restored.parameters.getRawParameterValue ("band.0.gain")->load(), 0.0f));
    }
    state.setProperty ("schemaVersion", 7, nullptr);
    state.removeProperty ("oversamplingMode", nullptr);
    REQUIRE (restore (restored, state));
    CHECK (restored.getOversamplingMode() == 0);
    CHECK (savedState (restored).getProperty ("schemaVersion").toString() == "8");
    state.removeChild (0, nullptr);
    CHECK_FALSE (restore (restored, state));
    CHECK_FALSE (restored.setOversamplingMode (-1));
    CHECK_FALSE (restored.setOversamplingMode (3));
}

TEST_CASE ("Library presets and Default preserve oversampling quality", "[oversampling][preset]")
{
    PluginProcessor source, target;
    REQUIRE (source.setOversamplingMode (1));
    REQUIRE (target.setOversamplingMode (2));
    juce::MemoryBlock bytes;
    source.getStateInformation (bytes);
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("openq-oversampling", ".json", false);
    REQUIRE (openq::presets::PresetJson::write (file, bytes).wasOk());
    CHECK_FALSE (file.loadFileAsString().contains ("oversamplingMode"));
    REQUIRE (openq::presets::PresetJson::read (file, bytes).wasOk());
    REQUIRE (openq::presets::PresetJson::apply (target, bytes));
    CHECK (target.getOversamplingMode() == 2);
    auto legacy = juce::ValueTree::fromXml (*juce::AudioProcessor::getXmlFromBinary (bytes.getData(), static_cast<int> (bytes.getSize())));
    legacy.setProperty ("schemaVersion", 7, nullptr);
    juce::AudioProcessor::copyXmlToBinary (*legacy.createXml(), bytes);
    REQUIRE (openq::presets::PresetJson::apply (target, bytes));
    CHECK (target.getOversamplingMode() == 2);
    REQUIRE (target.loadDefaultPreset());
    CHECK (target.getOversamplingMode() == 2);
    REQUIRE (file.deleteFile());
}

TEST_CASE ("Oversampled response separates host limits from internal coefficients", "[oversampling][response]")
{
    PluginProcessor processor;
    REQUIRE (processor.setOversamplingMode (2));
    parameter (processor, "band.0.frequency", 20000.0f);
    parameter (processor, "band.0.gain", 12.0f);
    processor.prepareToPlay (8000.0, 64);
    openq::ResponseWorker worker (processor.parameters, processor.getProcessingConfiguration());
    worker.setGeometry ({ 500, 300 });
    REQUIRE (worker.start());
    worker.requestUpdate();
    bool received = false;
    for (int i = 0; i < 1000 && !received; ++i)
    {
        received = worker.pullFrame();
        if (!received)
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
    REQUIRE (received);
    const auto& frame = worker.getFrame();
    CHECK (frame.valid);
    CHECK (juce::exactlyEqual (frame.hostSampleRate, 8000.0));
    CHECK (juce::exactlyEqual (frame.response.sampleRate, 32000.0));
    CHECK_THAT (openq::responseMagnitudeDb (frame.response, 3920.0), Catch::Matchers::WithinAbs (12.0, 1e-5));
    worker.stop();
}

TEST_CASE ("Shared converter suppresses folded harmonics from a test-only nonlinear core", "[oversampling][audio]")
{
    std::array<double, 3> alias {};
    for (int mode = 0; mode < 3; ++mode)
    {
        openq::Oversampling converter;
        double real = 0.0, imaginary = 0.0;
        constexpr int samples = 24000, skip = 2400;
        juce::AudioBuffer<float> buffer (2, 240);
        for (int offset = 0; offset < samples; offset += buffer.getNumSamples())
        {
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                for (int channel = 0; channel < 2; ++channel)
                    buffer.setSample (channel, sample, static_cast<float> (0.5 * std::sin (2.0 * std::numbers::pi * 10000.0 * (offset + sample) / 48000.0)));
            juce::dsp::AudioBlock<float> block (buffer);
            auto internal = mode == 0 ? block : converter.up (block, mode);
            for (size_t channel = 0; channel < internal.getNumChannels(); ++channel)
                for (size_t sample = 0; sample < internal.getNumSamples(); ++sample)
                {
                    auto& x = internal.getChannelPointer (channel)[sample];
                    x = x * x * x;
                }
            if (mode != 0)
                converter.down (block, mode);
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                if (offset + sample >= skip)
                {
                    const auto phase = 2.0 * std::numbers::pi * 18000.0 * (offset + sample) / 48000.0;
                    real += buffer.getSample (0, sample) * std::cos (phase);
                    imaginary += buffer.getSample (0, sample) * std::sin (phase);
                }
        }
        alias[static_cast<size_t> (mode)] = 2.0 * std::hypot (real, imaginary) / (samples - skip);
    }
    REQUIRE (alias[0] > 0.03);
    CHECK (alias[1] < alias[0] * 0.01);
    CHECK (alias[2] < alias[0] * 0.01);
}

TEST_CASE ("Oversampling selector preserves asynchronous selections and shows pending status", "[oversampling][ui]")
{
    PluginProcessor processor;
    processor.prepareToPlay (48000, 64);
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    editor->setSize (openq::EditorSize::minimumWidth, openq::EditorSize::minimumHeight);
    auto* bottom = editor->findChildWithID ("editor.bottom");
    REQUIRE (bottom != nullptr);
    auto* selector = dynamic_cast<juce::ComboBox*> (bottom->findChildWithID ("processing.oversampling"));
    REQUIRE (selector != nullptr);
    REQUIRE (selector->getNumItems() == 3);
    CHECK (selector->getSelectedId() == 1);
    CHECK (bottom->getLocalBounds().contains (selector->getBounds()));
    selector->setSelectedId (3, juce::sendNotificationAsync);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    CHECK (processor.getOversamplingMode() == 2);
    CHECK (selector->getSelectedId() == 3);
    CHECK (processor.isOversamplingPending());
}

TEST_CASE ("Oversampled bands match independently prepared high-rate filters", "[oversampling][audio]")
{
    for (const double rate : { 8000.0, 48000.0 })
        for (const auto type : { openq::FilterType::Bell, openq::FilterType::HighShelf, openq::FilterType::LowCut })
        {
            PluginProcessor processor;
            REQUIRE (processor.setOversamplingMode (2));
            parameter (processor, "band.0.frequency", 20000.0f);
            parameter (processor, "band.0.gain", 12.0f);
            parameter (processor, "band.0.type", static_cast<float> (type));
            processor.prepareToPlay (rate, 32);
            openq::BandFilter band;
            const auto target = openq::getBandParameterPointers (processor.parameters, 0).load();
            band.prepare (rate * 4, std::min (target.frequency, openq::bandLimits::effectiveMaximumFrequency (rate)), target.gain, target.q, target.status, type, target.slope);
            openq::Oversampling converter;
            juce::MidiBuffer midi;
            for (int iteration = 0; iteration < 12; ++iteration)
            {
                if (iteration == 6)
                {
                    parameter (processor, "band.0.gain", -12.0f);
                    parameter (processor, "band.0.frequency", 2000.0f);
                    const auto next = openq::getBandParameterPointers (processor.parameters, 0).load();
                    band.setParameters (std::min (next.frequency, openq::bandLimits::effectiveMaximumFrequency (rate)), next.gain, next.q, next.status, next.type, next.slope);
                }
                juce::AudioBuffer<float> expected (2, 31);
                for (int channel = 0; channel < 2; ++channel)
                    for (int sample = 0; sample < 31; ++sample)
                        expected.setSample (channel, sample, static_cast<float> (0.05 * std::sin ((iteration * 31 + sample) * (channel + 1) * 0.31)));
                juce::AudioBuffer<float> actual (expected);
                juce::dsp::AudioBlock<float> block (expected);
                auto internal = converter.up (block, 2);
                std::array<float*, 2> pointers { internal.getChannelPointer (0), internal.getChannelPointer (1) };
                juce::AudioBuffer<float> highRate (pointers.data(), 2, static_cast<int> (internal.getNumSamples()));
                band.process (highRate, 2);
                converter.down (block, 2);
                processor.processBlock (actual, midi);
                for (int channel = 0; channel < 2; ++channel)
                    for (int sample = 0; sample < 31; ++sample)
                        REQUIRE (juce::exactlyEqual (actual.getSample (channel, sample), expected.getSample (channel, sample)));
            }
        }
}

TEST_CASE ("Oversampling keeps analyzer packets at host rate with final output samples", "[oversampling][analyzer]")
{
    PluginProcessor processor;
    REQUIRE (processor.setOversamplingMode (2));
    processor.prepareToPlay (48000, 16);
    auto& transport = processor.getAnalyzerTransport();
    transport.setActive (true);
    juce::AudioBuffer<float> buffer (2, 513);
    fill (buffer);
    juce::MidiBuffer midi;
    processor.processBlock (buffer, midi);
    openq::AnalyzerPacket packet;
    int received = 0;
    while (transport.read (packet))
    {
        CHECK (juce::exactlyEqual (packet.sampleRate, 48000.0));
        CHECK (packet.sequence == static_cast<std::uint64_t> (received + 1));
        for (int channel = 0; channel < packet.channels; ++channel)
            for (int sample = 0; sample < packet.samples; ++sample)
            {
                CHECK (juce::exactlyEqual (packet.pre[static_cast<size_t> (channel)][static_cast<size_t> (sample)], 0.125f * static_cast<float> (channel + 1)));
                CHECK (juce::exactlyEqual (packet.post[static_cast<size_t> (channel)][static_cast<size_t> (sample)], buffer.getSample (channel, received + sample)));
            }
        received += packet.samples;
    }
    CHECK (received == buffer.getNumSamples());
}

TEST_CASE ("Host preparation invalidates an outstanding oversampling handoff", "[oversampling][threading]")
{
    struct RestartingHost final : juce::AudioProcessorListener
    {
        bool restarted = false;
        void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override {}
        void audioProcessorChanged (juce::AudioProcessor* processor, const ChangeDetails& details) override
        {
            if (details.latencyChanged && !restarted)
            {
                restarted = true;
                processor->prepareToPlay (44100, 32);
            }
        }
    } host;
    PluginProcessor processor;
    processor.prepareToPlay (48000, 32);
    processor.addListener (&host);
    REQUIRE (processor.setOversamplingMode (2));
    settle (processor, 2);
    CHECK (host.restarted);
    openq::ProcessingSnapshot configuration;
    REQUIRE (processor.getProcessingConfiguration().tryRead (configuration));
    CHECK (juce::exactlyEqual (configuration.hostRate, 44100.0));
    CHECK (juce::exactlyEqual (configuration.internalRate(), 176400.0));
    CHECK_FALSE (processor.isOversamplingPending());
    processor.removeListener (&host);
}

TEST_CASE ("Prepared processing snapshots stay coherent while the writer changes rates", "[oversampling][threading]")
{
    openq::ProcessingConfiguration configuration;
    std::atomic<bool> done { false };
    std::thread writer ([&] {
        for (int i = 0; i < 10000; ++i)
            configuration.publish ((i & 1) == 0 ? openq::ProcessingSnapshot { 8000.0, 0 } : openq::ProcessingSnapshot { 48000.0, 2 });
        done.store (true);
    });
    bool coherent = true;
    openq::ProcessingSnapshot value;
    do
    {
        const auto previous = value;
        if (configuration.tryRead (value))
            coherent &= value == openq::ProcessingSnapshot {} || value == openq::ProcessingSnapshot { 8000.0, 0 } || value == openq::ProcessingSnapshot { 48000.0, 2 };
        else
            coherent &= value == previous;
    } while (!done.load());
    writer.join();
    CHECK (coherent);
    REQUIRE (configuration.tryRead (value));
    CHECK (value == openq::ProcessingSnapshot { 48000.0, 2 });
}

TEST_CASE ("Host frequency limiting preserves nonfinite parameter fallback", "[oversampling][audio]")
{
    for (const int mode : { 0, 2 })
        for (const float frequency : { std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN() })
        {
            PluginProcessor processor;
            REQUIRE (processor.setOversamplingMode (mode));
            parameter (processor, "band.0.gain", 6.0f);
            processor.parameters.getRawParameterValue ("band.0.frequency")->store (frequency);
            processor.prepareToPlay (48000, 32);
            juce::AudioBuffer<float> buffer (2, 4800);
            for (int channel = 0; channel < 2; ++channel)
                for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                    buffer.setSample (channel, sample, static_cast<float> (0.1 * std::sin (2.0 * std::numbers::pi * 1000.0 * sample / 48000.0)));
            juce::MidiBuffer midi;
            processor.processBlock (buffer, midi);
            const auto expected = 0.1 / std::sqrt (2.0) * juce::Decibels::decibelsToGain (6.0);
            CHECK_THAT (buffer.getRMSLevel (0, 2400, 2400), Catch::Matchers::WithinAbs (expected, 1e-4));
        }
}

TEST_CASE ("UI snapshot reads retain their cache while a publication is stalled", "[oversampling][response][ui][threading]")
{
    PluginProcessor processor;
    openq::ProcessingConfiguration configuration;
    const openq::ProcessingSnapshot initial { 48000.0, 1 };
    configuration.publish (initial);
    openq::ProcessingSnapshot cached;
    REQUIRE (configuration.tryRead (cached));
    REQUIRE (cached == initial);
    openq::ResponseGraph graph (processor.parameters, configuration);
    graph.setSize (640, 360);
    graph.setSelectedBand (0);
    const auto bounds = graph.getSelectedNodeBounds();
    REQUIRE_FALSE (bounds.isEmpty());

    openq::ProcessingConfigurationTestAccess::beginPublication (configuration);
    CHECK_FALSE (configuration.tryRead (cached));
    CHECK (cached == initial);
    graph.setSelectedBand (0);
    CHECK (graph.getSelectedNodeBounds() == bounds);
    CHECK (graph.createComponentSnapshot (graph.getLocalBounds()).isValid());
    openq::ProcessingConfigurationTestAccess::endPublication (configuration);

    configuration.publish ({ 8000.0, 2 });
    REQUIRE (configuration.tryRead (cached));
    CHECK (cached == openq::ProcessingSnapshot { 8000.0, 2 });
    graph.setSelectedBand (0);
    CHECK (graph.getSelectedNodeBounds() != bounds);
}

TEST_CASE ("Response worker retains queued geometry while processing configuration is busy", "[oversampling][response][threading]")
{
    PluginProcessor processor;
    openq::ProcessingConfiguration configuration;
    configuration.publish ({ 48000.0, 0 });
    openq::ResponseWorker worker (processor.parameters, configuration);
    const auto awaitGeometry = [&] (openq::ResponseGeometry expected) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds (2);
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (worker.pullFrame() && worker.getFrame().valid && worker.getFrame().geometry == expected)
                return true;
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
        }
        return false;
    };
    worker.setGeometry ({ 320, 180 });
    REQUIRE (worker.start());
    worker.requestUpdate();
    REQUIRE (awaitGeometry ({ 320, 180 }));
    openq::ProcessingConfigurationTestAccess::beginPublication (configuration);
    worker.setGeometry ({ 640, 360 });
    std::this_thread::sleep_for (std::chrono::milliseconds (20));
    CHECK_FALSE (worker.pullFrame());
    CHECK (worker.getFrame().geometry == openq::ResponseGeometry { 320, 180 });
    openq::ProcessingConfigurationTestAccess::endPublication (configuration);
    worker.requestUpdate();
    REQUIRE (awaitGeometry ({ 640, 360 }));
    worker.stop();
}
