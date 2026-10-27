#include "PluginEditor.h"
#include "dsp/analyzer/AnalyzerEngine.h"
#include "ui/AnalyzerWorker.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <chrono>
#include <numbers>
#include <random>
#include <thread>

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr double toneHz = 93.75; // Coherent bin 8 in the Normal FFT, outside the smoothed-bin region.

    void feedTone (openq::AnalyzerEngine& engine, int channels, float rightGain = 1.0f, float postGain = 1.0f, double rate = sampleRate, std::uint64_t start = 0)
    {
        openq::AnalyzerPacket packet;
        packet.channels = channels;
        packet.sampleRate = rate;
        packet.session = 1;
        packet.samples = openq::AnalyzerPacket::capacity;
        // Fill the complete longest history, including at higher sample rates.
        const auto samples = static_cast<std::uint64_t> (rate);
        for (std::uint64_t offset = 0; offset < samples; offset += static_cast<std::uint64_t> (packet.samples))
        {
            packet.sequence = start + offset;
            for (int i = 0; i < packet.samples; ++i)
            {
                const auto value = static_cast<float> (std::sin (2.0 * std::numbers::pi * toneHz * static_cast<double> (packet.sequence + static_cast<std::uint64_t> (i)) / rate));
                packet.pre[0][static_cast<size_t> (i)] = value;
                packet.pre[1][static_cast<size_t> (i)] = value * rightGain;
                packet.post[0][static_cast<size_t> (i)] = value * postGain;
                packet.post[1][static_cast<size_t> (i)] = value * rightGain * postGain;
            }
            engine.push (packet);
        }
        engine.analyze ({});
    }

    float dbAt (const openq::AnalyzerEngine& engine, size_t trace, double frequency = toneHz)
    {
        return 10.0f * std::log10 (std::max (1.0e-20f, engine.powerAt (trace, frequency)));
    }

    bool awaitAnalyzer (openq::AnalyzerWorker& worker, const std::function<bool (const openq::AnalyzerFrame&)>& ready)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds (3);
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (worker.pullFrame() && ready (worker.getFrame()))
                return true;
            std::this_thread::sleep_for (std::chrono::milliseconds (2));
        }
        return false;
    }
}

TEST_CASE ("Analyzer capture aligns pre and post across partial packets and ring wrap", "[analyzer]")
{
    openq::AnalyzerTransport transport;
    transport.prepare (sampleRate);
    transport.setActive (true);
    juce::AudioBuffer<float> audio (2, openq::AnalyzerPacket::capacity + 17);
    openq::AnalyzerPacket packet;
    std::uint64_t previousEnd = 0;
    for (int block = 0; block < openq::AnalyzerTransport::packetSlots; ++block)
    {
        for (int channel = 0; channel < audio.getNumChannels(); ++channel)
            for (int i = 0; i < audio.getNumSamples(); ++i)
                audio.setSample (channel, i, static_cast<float> (block * audio.getNumSamples() + i + channel));
        transport.beginBlock (audio, audio.getNumChannels());
        audio.applyGain (0.5f);
        transport.endBlock (audio);
        int offset = 0;
        while (transport.read (packet))
        {
            CHECK (juce::exactlyEqual (packet.sampleRate, sampleRate));
            CHECK (packet.channels == audio.getNumChannels());
            if (previousEnd != 0)
                CHECK (packet.sequence == previousEnd);
            previousEnd = packet.sequence + static_cast<std::uint64_t> (packet.samples);
            for (int channel = 0; channel < packet.channels; ++channel)
                for (int i = 0; i < packet.samples; ++i)
                {
                    const auto ch = static_cast<size_t> (channel);
                    CHECK (juce::exactlyEqual (packet.pre[ch][static_cast<size_t> (i)], static_cast<float> (block * audio.getNumSamples() + offset + i + channel)));
                    CHECK (juce::exactlyEqual (packet.post[ch][static_cast<size_t> (i)], audio.getSample (channel, offset + i)));
                }
            offset += packet.samples;
        }
        CHECK (offset == audio.getNumSamples());
    }
}

TEST_CASE ("Analyzer overflow drops new capture and tags the next gap", "[analyzer]")
{
    openq::AnalyzerTransport transport;
    transport.prepare (sampleRate);
    transport.setActive (true);
    constexpr auto capacity = (openq::AnalyzerTransport::packetSlots - 1) * openq::AnalyzerPacket::capacity;
    juce::AudioBuffer<float> audio (1, capacity + 13);
    audio.clear();
    transport.beginBlock (audio, 1);
    transport.endBlock (audio);
    transport.beginBlock (audio, 1); // Full: must return without overwriting unread data.
    transport.endBlock (audio);
    openq::AnalyzerPacket packet;
    int consumed = 0;
    std::uint64_t previousEnd = 0;
    while (transport.read (packet))
    {
        consumed += packet.samples;
        previousEnd = packet.sequence + static_cast<std::uint64_t> (packet.samples);
    }
    CHECK (consumed == capacity);
    transport.beginBlock (audio, 1);
    transport.endBlock (audio);
    REQUIRE (transport.read (packet));
    CHECK (packet.sequence > previousEnd);
    while (transport.read (packet))
    {
    }
    transport.setActive (false);
    transport.beginBlock (audio, 1);
    transport.endBlock (audio);
    CHECK_FALSE (transport.read (packet));
    transport.setActive (true);
    const auto session = transport.session();
    transport.beginBlock (audio, 1);
    transport.endBlock (audio);
    REQUIRE (transport.read (packet));
    CHECK (packet.session == session);
    CHECK (packet.session > 1);
}

TEST_CASE ("Analyzer channel power convention preserves dual mono and polarity", "[analyzer]")
{
    openq::AnalyzerEngine engine;
    feedTone (engine, 1);
    const auto mono = dbAt (engine, 0);
    CHECK_THAT (mono, Catch::Matchers::WithinAbs (0.0, 0.08));
    feedTone (engine, 2);
    CHECK_THAT (dbAt (engine, 0), Catch::Matchers::WithinAbs (mono, 0.001));
    feedTone (engine, 2, -1.0f, 0.5f);
    CHECK_THAT (dbAt (engine, 0), Catch::Matchers::WithinAbs (mono, 0.001));
    CHECK_THAT (dbAt (engine, 1) - dbAt (engine, 0), Catch::Matchers::WithinAbs (-6.0206, 0.001));
    feedTone (engine, 2, 0.0f);
    CHECK_THAT (dbAt (engine, 0) - mono, Catch::Matchers::WithinAbs (-3.0103, 0.001));
}

TEST_CASE ("Analyzer rate scaling and high resolution stay finite and preserve pre post ratios", "[analyzer]")
{
    openq::AnalyzerEngine engine;
    for (const auto rate : { 44100.0, sampleRate, 96000.0, 192000.0 })
    {
        feedTone (engine, 2, -1.0f, 0.5f, rate);
        CHECK (engine.getMiddleFFTSize() == (rate <= 50000.0 ? 4096 : rate <= 100000.0 ? 8192
                                                                                       : 16384));
        engine.analyze ({ true, true, openq::AnalyzerQuality::High });
        for (const auto frequency : { 20.0, 93.75, 200.0, 300.0, 400.0, 1000.0, 2000.0, 3000.0, 4000.0, 20000.0 })
        {
            CHECK (std::isfinite (engine.powerAt (0, frequency)));
            CHECK (engine.powerAt (0, frequency) >= 0.0f);
        }
        CHECK_THAT (dbAt (engine, 1) - dbAt (engine, 0), Catch::Matchers::WithinAbs (-6.0206, 0.002));
    }
}

TEST_CASE ("Analyzer resets discontinuous history and sanitizes nonfinite capture", "[analyzer]")
{
    openq::AnalyzerEngine engine;
    feedTone (engine, 1);
    const auto revision = engine.getRevision();
    openq::AnalyzerPacket packet;
    packet.sampleRate = sampleRate;
    packet.channels = 1;
    packet.samples = openq::AnalyzerPacket::capacity;
    packet.session = 1;
    packet.sequence = 1000000;
    packet.pre[0].fill (std::numeric_limits<float>::quiet_NaN());
    packet.post[0].fill (std::numeric_limits<float>::infinity());
    engine.push (packet);
    engine.analyze ({});
    CHECK (engine.getRevision() > revision);
    CHECK (juce::exactlyEqual (engine.powerAt (0, toneHz), 0.0f));
    CHECK (juce::exactlyEqual (engine.powerAt (1, toneHz), 0.0f));
}

TEST_CASE ("High analyzer resolutions retain the Normal broadband noise reference", "[analyzer]")
{
    openq::AnalyzerEngine engine;
    openq::AnalyzerPacket packet;
    packet.sampleRate = sampleRate;
    packet.channels = 1;
    packet.samples = openq::AnalyzerPacket::capacity;
    packet.session = 1;
    std::mt19937 random (42);
    std::uniform_real_distribution<float> noise (-0.5f, 0.5f);
    double normalPower = 0.0, highPower = 0.0;
    constexpr int historyPackets = 16384 / openq::AnalyzerPacket::capacity;
    for (int window = 0; window < 12; ++window)
    {
        for (int block = 0; block < historyPackets; ++block)
        {
            packet.sequence = static_cast<std::uint64_t> ((window * historyPackets + block) * packet.samples);
            for (int i = 0; i < packet.samples; ++i)
                packet.pre[0][static_cast<size_t> (i)] = noise (random);
            packet.post = packet.pre;
            engine.push (packet);
        }
        for (const auto quality : { openq::AnalyzerQuality::Normal, openq::AnalyzerQuality::High })
        {
            engine.analyze ({ true, false, quality });
            for (double frequency = 50.0; frequency < 15000.0; frequency *= 1.1)
            {
                auto& sum = quality == openq::AnalyzerQuality::Normal ? normalPower : highPower;
                sum += engine.powerAt (0, frequency);
            }
        }
    }
    CHECK_THAT (10.0 * std::log10 (highPower / normalPower), Catch::Matchers::WithinAbs (0.0, 0.8));
}

TEST_CASE ("Analyzer worker publishes geometry tagged traces and decays suspended input", "[analyzer][threading]")
{
    openq::AnalyzerTransport transport;
    transport.prepare (sampleRate);
    openq::AnalyzerWorker worker (transport);
    worker.setGeometry ({ 320, 180 });
    REQUIRE (worker.start());
    juce::AudioBuffer<float> audio (1, 8192);
    for (int i = 0; i < audio.getNumSamples(); ++i)
        audio.setSample (0, i, static_cast<float> (std::sin (2.0 * std::numbers::pi * toneHz * i / sampleRate)));
    transport.beginBlock (audio, 1);
    audio.applyGain (0.5f);
    transport.endBlock (audio);
    REQUIRE (awaitAnalyzer (worker, [] (const auto& frame) { return frame.valid && !frame.stale; }));
    const auto initialTop = worker.getFrame().outlines[0].getBounds().getY();
    CHECK_FALSE (worker.getFrame().fills[0].isEmpty());
    CHECK_FALSE (worker.getFrame().fills[1].isEmpty());
    CHECK (worker.getFrame().outlines[1].getBounds().getY() > initialTop);
    worker.setGeometry ({ 480, 240 });
    REQUIRE (awaitAnalyzer (worker, [] (const auto& frame) { return frame.valid && frame.geometry == openq::ResponseGeometry { 480, 240 }; }));
    REQUIRE (awaitAnalyzer (worker, [] (const auto& frame) { return frame.stale; }));
    const auto staleTop = worker.getFrame().outlines[0].getBounds().getY();
    REQUIRE (awaitAnalyzer (worker, [=] (const auto& frame) { return frame.stale && frame.outlines[0].getBounds().getY() > staleTop + 2.0f; }));
    transport.setSettings ({ false, true, openq::AnalyzerQuality::High });
    REQUIRE (awaitAnalyzer (worker, [] (const auto& frame) { return frame.settings.quality == openq::AnalyzerQuality::High && frame.outlines[0].isEmpty(); }));
    worker.stop();
    CHECK ((transport.session() & 1u) == 0);
    REQUIRE (worker.start());
    CHECK ((transport.session() & 1u) != 0);
    REQUIRE (awaitAnalyzer (worker, [] (const auto& frame) { return !frame.valid; }));
    worker.stop();
}

TEST_CASE ("Analyzer capture does not change audio and display settings survive state restore", "[analyzer][state]")
{
    PluginProcessor withAnalyzer, withoutAnalyzer;
    withAnalyzer.prepareToPlay (sampleRate, 512);
    withoutAnalyzer.prepareToPlay (sampleRate, 512);
    withAnalyzer.getAnalyzerTransport().setActive (true);
    withAnalyzer.getAnalyzerTransport().setSettings ({ false, true, openq::AnalyzerQuality::High });
    auto* gainA = withAnalyzer.parameters.getParameter ("band.0.gain");
    auto* gainB = withoutAnalyzer.parameters.getParameter ("band.0.gain");
    gainA->setValueNotifyingHost (gainA->convertTo0to1 (6.0f));
    gainB->setValueNotifyingHost (gainB->convertTo0to1 (6.0f));
    juce::AudioBuffer<float> a (2, 512), b (2, 512);
    juce::MidiBuffer midi;
    for (int block = 0; block < 40; ++block)
    {
        for (int channel = 0; channel < a.getNumChannels(); ++channel)
            for (int i = 0; i < a.getNumSamples(); ++i)
            {
                const auto value = static_cast<float> (std::sin (2.0 * std::numbers::pi * 1000.0 * static_cast<double> (block * a.getNumSamples() + i) / sampleRate));
                a.setSample (channel, i, value);
                b.setSample (channel, i, value);
            }
        withAnalyzer.processBlock (a, midi);
        withoutAnalyzer.processBlock (b, midi);
        for (int channel = 0; channel < a.getNumChannels(); ++channel)
            for (int i = 0; i < a.getNumSamples(); ++i)
                REQUIRE (juce::exactlyEqual (a.getSample (channel, i), b.getSample (channel, i)));
    }
    openq::AnalyzerEngine engine;
    openq::AnalyzerPacket captured;
    while (withAnalyzer.getAnalyzerTransport().read (captured))
        engine.push (captured);
    engine.analyze ({});
    CHECK_THAT (dbAt (engine, 1, 1000.0) - dbAt (engine, 0, 1000.0), Catch::Matchers::WithinAbs (6.0, 0.02));
    juce::MemoryBlock state;
    withAnalyzer.getStateInformation (state);
    withoutAnalyzer.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    CHECK (withoutAnalyzer.getAnalyzerTransport().getSettings().encode() == withAnalyzer.getAnalyzerTransport().getSettings().encode());
    CHECK (withoutAnalyzer.getLatencySamples() == 0);
}

TEST_CASE ("Analyzer preferences validate before restoring any band state", "[analyzer][state]")
{
    PluginProcessor processor;
    processor.getAnalyzerTransport().setSettings ({ false, true, openq::AnalyzerQuality::High });
    const auto previous = processor.getAnalyzerTransport().getSettings().encode();
    for (const auto invalid : { "-1", "8", "1.5", "nan", "invalid", "2 trailing" })
    {
        auto state = processor.parameters.copyState();
        state.setProperty ("analyzerSettings", invalid, nullptr);
        state.getChildWithProperty ("id", "band.0.gain").setProperty ("value", 12.0, nullptr);
        juce::MemoryBlock bytes;
        juce::AudioProcessor::copyXmlToBinary (*state.createXml(), bytes);
        processor.setStateInformation (bytes.getData(), static_cast<int> (bytes.getSize()));
        CHECK (juce::exactlyEqual (processor.parameters.getRawParameterValue ("band.0.gain")->load(), 0.0f));
        CHECK (processor.getAnalyzerTransport().getSettings().encode() == previous);
    }
    auto olderState = processor.parameters.copyState();
    olderState.removeProperty ("analyzerSettings", nullptr);
    juce::MemoryBlock bytes;
    juce::AudioProcessor::copyXmlToBinary (*olderState.createXml(), bytes);
    processor.setStateInformation (bytes.getData(), static_cast<int> (bytes.getSize()));
    CHECK (processor.getAnalyzerTransport().getSettings().encode() == openq::AnalyzerSettings {}.encode());
}

TEST_CASE ("Analyzer capture transfers complete samples between concurrent producer and consumer", "[analyzer][threading]")
{
    openq::AnalyzerTransport transport;
    transport.prepare (sampleRate);
    transport.setActive (true);
    std::atomic<bool> finished { false };
    std::thread producer ([&] {
        juce::AudioBuffer<float> audio (2, 137);
        for (int block = 0; block < 2000; ++block)
        {
            for (int i = 0; i < audio.getNumSamples(); ++i)
            {
                const auto value = static_cast<float> (block * audio.getNumSamples() + i);
                audio.setSample (0, i, value);
                audio.setSample (1, i, -value);
            }
            transport.beginBlock (audio, audio.getNumChannels());
            audio.applyGain (0.5f);
            transport.endBlock (audio);
        }
        finished.store (true, std::memory_order_release);
    });
    openq::AnalyzerPacket packet;
    bool intact = true;
    int packets = 0;
    std::uint64_t previousEnd = 0;
    auto check = [&] {
        ++packets;
        intact = intact && packet.sequence >= previousEnd;
        previousEnd = packet.sequence + static_cast<std::uint64_t> (packet.samples);
        for (int i = 0; i < packet.samples; ++i)
        {
            const auto value = static_cast<float> (packet.sequence - 1 + static_cast<std::uint64_t> (i));
            const auto index = static_cast<size_t> (i);
            intact = intact && juce::exactlyEqual (packet.pre[0][index], value) && juce::exactlyEqual (packet.pre[1][index], -value)
                     && juce::exactlyEqual (packet.post[0][index], value * 0.5f) && juce::exactlyEqual (packet.post[1][index], -value * 0.5f);
        }
    };
    while (!finished.load (std::memory_order_acquire))
        if (transport.read (packet))
            check();
    producer.join();
    while (transport.read (packet))
        check();
    CHECK (intact);
    CHECK (packets > 0);
}

TEST_CASE ("Analyzer UI switches remain independent of band parameters", "[analyzer][ui]")
{
    PluginProcessor processor;
    PluginEditor editor (processor);
    const auto before = processor.parameters.copyState();
    auto* pre = dynamic_cast<juce::ToggleButton*> (editor.findChildWithID ("analyzer.pre"));
    auto* post = dynamic_cast<juce::ToggleButton*> (editor.findChildWithID ("analyzer.post"));
    auto* quality = dynamic_cast<juce::ComboBox*> (editor.findChildWithID ("analyzer.quality"));
    REQUIRE (pre != nullptr);
    REQUIRE (post != nullptr);
    REQUIRE (quality != nullptr);
    CHECK (pre->getToggleState());
    CHECK (post->getToggleState());
    CHECK (quality->getSelectedId() == 1);
    pre->setToggleState (false, juce::dontSendNotification);
    pre->onClick();
    CHECK_FALSE (processor.getAnalyzerTransport().getSettings().pre);
    CHECK (processor.getAnalyzerTransport().getSettings().post);
    quality->setSelectedId (2, juce::sendNotificationSync);
    CHECK (processor.getAnalyzerTransport().getSettings().quality == openq::AnalyzerQuality::High);
    CHECK (processor.parameters.copyState().isEquivalentTo (before));
    CHECK ((processor.getAnalyzerTransport().session() & 1u) == 0); // No capture from a hidden editor.
}

TEST_CASE ("Spectrum fill is a saved display preference independent of analysis", "[analyzer][state][ui]")
{
    PluginProcessor processor;
    auto& transport = processor.getAnalyzerTransport();
    CHECK (transport.getFillEnabled());
    PluginEditor editor (processor);
    auto* fill = dynamic_cast<juce::ToggleButton*> (editor.findChildWithID ("analyzer.fill"));
    REQUIRE (fill != nullptr);
    CHECK (fill->getToggleState());
    const auto settings = transport.getSettings().encode();
    const auto session = transport.session();
    const auto parameters = processor.parameters.copyState();
    fill->setToggleState (false, juce::dontSendNotification);
    fill->onClick();
    CHECK_FALSE (transport.getFillEnabled());
    CHECK (transport.getSettings().encode() == settings);
    CHECK (transport.session() == session);
    CHECK (processor.parameters.copyState().isEquivalentTo (parameters));
    juce::MemoryBlock saved;
    processor.getStateInformation (saved);
    PluginProcessor restored;
    restored.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    CHECK_FALSE (restored.getAnalyzerTransport().getFillEnabled());
    PluginEditor reopened (restored);
    auto* reopenedFill = dynamic_cast<juce::ToggleButton*> (reopened.findChildWithID ("analyzer.fill"));
    REQUIRE (reopenedFill != nullptr);
    CHECK_FALSE (reopenedFill->getToggleState());

    for (const auto invalid : { "-1", "2", "0.5", "nan", "invalid", "1 trailing" })
    {
        auto state = parameters.createCopy();
        state.setProperty ("analyzerFill", invalid, nullptr);
        state.getChildWithProperty ("id", "band.0.gain").setProperty ("value", 12.0, nullptr);
        juce::MemoryBlock bytes;
        juce::AudioProcessor::copyXmlToBinary (*state.createXml(), bytes);
        restored.setStateInformation (bytes.getData(), static_cast<int> (bytes.getSize()));
        CHECK_FALSE (restored.getAnalyzerTransport().getFillEnabled());
        CHECK (juce::exactlyEqual (restored.parameters.getRawParameterValue ("band.0.gain")->load(), 0.0f));
    }
    auto legacy = parameters.createCopy();
    legacy.removeProperty ("analyzerFill", nullptr);
    juce::MemoryBlock bytes;
    juce::AudioProcessor::copyXmlToBinary (*legacy.createXml(), bytes);
    restored.setStateInformation (bytes.getData(), static_cast<int> (bytes.getSize()));
    CHECK (restored.getAnalyzerTransport().getFillEnabled());
}
