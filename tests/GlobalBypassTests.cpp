// SPDX-License-Identifier: AGPL-3.0-only
#include "PluginEditor.h"
#include "ui/Palette.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

namespace
{
    juce::Image snapshot (juce::Component& component)
    {
        return component.createComponentSnapshot (component.getLocalBounds());
    }
    int difference (const juce::Image& a, const juce::Image& b)
    {
        int count = 0;
        for (int y = 0; y < a.getHeight(); ++y)
            for (int x = 0; x < a.getWidth(); ++x)
                if (a.getPixelAt (x, y) != b.getPixelAt (x, y))
                    ++count;
        return count;
    }
    void set (PluginProcessor& p, const juce::String& id, float value)
    {
        auto* parameter = p.parameters.getParameter (id);
        REQUIRE (parameter != nullptr);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    }
    void load (PluginProcessor& p, const juce::ValueTree& state)
    {
        juce::MemoryBlock bytes;
        juce::AudioProcessor::copyXmlToBinary (*state.createXml(), bytes);
        p.setStateInformation (bytes.getData(), static_cast<int> (bytes.getSize()));
    }
    void fill (juce::AudioBuffer<float>& buffer, float value = 0.125f)
    {
        for (int c = 0; c < buffer.getNumChannels(); ++c)
            for (int s = 0; s < buffer.getNumSamples(); ++s)
                buffer.setSample (c, s, value * static_cast<float> (c + 1));
    }
}

TEST_CASE ("Total bypass designates one appended host parameter and rejects older schemas", "[bypass][state]")
{
    PluginProcessor p;
    REQUIRE (p.getBypassParameter() == p.parameters.getParameter (openq::bypassParameterId));
    CHECK (p.getParameters().getLast() == p.getBypassParameter());
    CHECK (juce::exactlyEqual (p.getBypassParameter()->getValue(), 0.0f));
    set (p, openq::bypassParameterId, 1.0f);
    set (p, "input.gain", 3.0f);
    auto saved = p.parameters.copyState();
    PluginProcessor restored;
    load (restored, saved);
    CHECK (juce::exactlyEqual (restored.getBypassParameter()->getValue(), 1.0f));
    CHECK_THAT (restored.parameters.getRawParameterValue ("input.gain")->load(), Catch::Matchers::WithinAbs (3.0, 1e-5));
    const auto unchanged = restored.parameters.copyState();
    auto legacy = saved.createCopy();
    legacy.setProperty ("schemaVersion", 6, nullptr);
    legacy.removeChild (legacy.getChildWithProperty ("id", openq::bypassParameterId), nullptr);
    load (restored, legacy);
    CHECK (restored.parameters.copyState().isEquivalentTo (unchanged));
    CHECK (static_cast<int> (restored.parameters.copyState().getProperty ("schemaVersion")) == openq::stateSchemaVersion);
    for (const auto* bad : { "0.5", "-1", "2", "nan", "1 trailing" })
    {
        auto broken = saved.createCopy();
        broken.getChildWithProperty ("id", openq::bypassParameterId).setProperty ("value", bad, nullptr);
        load (restored, broken);
        CHECK (restored.parameters.copyState().isEquivalentTo (unchanged));
    }
    saved.removeChild (saved.getChildWithProperty ("id", openq::bypassParameterId), nullptr);
    load (restored, saved);
    CHECK (restored.parameters.copyState().isEquivalentTo (unchanged));
    legacy.getChildWithProperty ("id", "band.0.status").setProperty ("value", "0.5", nullptr);
    load (restored, legacy);
    CHECK (restored.parameters.copyState().isEquivalentTo (unchanged));
}

TEST_CASE ("Total bypass is bit exact raw input including trims across host block sizes", "[bypass][audio]")
{
    for (const int channels : { 1, 2 })
    {
        PluginProcessor p;
        const auto layout = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
        REQUIRE (p.setBusesLayout ({ { layout }, { layout } }));
        set (p, "input.gain", 12.0f);
        set (p, "output.gain", -3.0f);
        set (p, "band.0.gain", 18.0f);
        set (p, openq::bypassParameterId, 1.0f);
        p.prepareToPlay (48000, 32);
        juce::MidiBuffer midi;
        for (const int size : { 0, 1, 31, 256, 257, 4097 })
        {
            juce::AudioBuffer<float> buffer (channels, size);
            for (int c = 0; c < channels; ++c)
                for (int s = 0; s < size; ++s)
                    buffer.setSample (c, s, static_cast<float> (std::sin ((s + 1) * (c + 1) * 0.17)) * 0.1f);
            const juce::AudioBuffer<float> dry (buffer);
            p.processBlock (buffer, midi);
            for (int c = 0; c < channels; ++c)
                for (int s = 0; s < size; ++s)
                    REQUIRE (juce::exactlyEqual (buffer.getSample (c, s), dry.getSample (c, s)));
        }
    }
}

TEST_CASE ("Global bypass fades in twenty milliseconds and reverses without resetting the mix", "[bypass][audio]")
{
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        PluginProcessor p;
        set (p, "output.gain", 6.0f);
        p.prepareToPlay (rate, 16);
        const auto count = static_cast<int> (rate * openq::bypassFadeSeconds);
        const auto wet = 0.125 * juce::Decibels::decibelsToGain (6.0);
        juce::MidiBuffer midi;
        juce::AudioBuffer<float> half (2, count / 2);
        fill (half);
        set (p, openq::bypassParameterId, 1.0f);
        p.processBlock (half, midi);
        CHECK_THAT (half.getSample (0, 0), Catch::Matchers::WithinAbs (wet + (0.125 - wet) / count, 1e-6));
        const auto midpoint = half.getSample (0, half.getNumSamples() - 1);
        CHECK_THAT (midpoint, Catch::Matchers::WithinAbs ((wet + 0.125) * 0.5, 1e-6));
        set (p, openq::bypassParameterId, 0.0f);
        juce::AudioBuffer<float> back (2, count);
        fill (back);
        p.processBlock (back, midi);
        CHECK (back.getSample (0, 0) > midpoint);
        CHECK (back.getSample (0, 0) - midpoint < (wet - 0.125) / count + 1e-6);
        CHECK_THAT (back.getSample (0, count - 1), Catch::Matchers::WithinAbs (wet, 1e-6));
        set (p, openq::bypassParameterId, 1.0f);
        fill (back);
        p.processBlock (back, midi);
        CHECK (juce::exactlyEqual (back.getSample (0, count - 1), 0.125f));
        CHECK (juce::exactlyEqual (back.getSample (1, count - 1), 0.25f));
    }
}

TEST_CASE ("Global bypass keeps the live EQ and trim histories identical to a running wet reference", "[bypass][audio]")
{
    PluginProcessor p, reference;
    for (auto* processor : { &p, &reference })
    {
        set (*processor, "band.0.gain", 15.0f);
        set (*processor, "band.0.frequency", 200.0f);
        processor->prepareToPlay (48000, 128);
    }
    set (p, openq::bypassParameterId, 1.0f);
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> actual (2, 173), expected (2, 173);
    for (int block = 0; block < 80; ++block)
    {
        if (block == 20)
            for (auto* processor : { &p, &reference })
            {
                set (*processor, "band.0.type", static_cast<float> (openq::FilterType::LowCut));
                set (*processor, "band.0.frequency", 500.0f);
                set (*processor, "band.0.q", 1.5f);
                set (*processor, "band.0.slope", static_cast<float> (openq::CutSlope::Db24));
                set (*processor, "input.gain", 4.0f);
                set (*processor, "output.gain", -2.0f);
            }
        if (block == 50)
            set (p, openq::bypassParameterId, 0.0f);
        for (int c = 0; c < 2; ++c)
            for (int s = 0; s < actual.getNumSamples(); ++s)
            {
                const auto sample = static_cast<float> (0.1 * std::sin ((block * actual.getNumSamples() + s) * (c + 1) * 0.03));
                actual.setSample (c, s, sample);
                expected.setSample (c, s, sample);
            }
        p.processBlock (actual, midi);
        reference.processBlock (expected, midi);
        if (block > 60)
            for (int c = 0; c < 2; ++c)
                for (int s = 0; s < actual.getNumSamples(); ++s)
                    REQUIRE (juce::exactlyEqual (actual.getSample (c, s), expected.getSample (c, s)));
    }
    CHECK (p.parameters.copyState().isEquivalentTo (reference.parameters.copyState()));
}

TEST_CASE ("Host bypass shares warm routing without writing the internal parameter", "[bypass][audio]")
{
    PluginProcessor p;
    set (p, "output.gain", 6.0f);
    p.prepareToPlay (48000, 16);
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buffer (2, 2048);
    fill (buffer);
    p.processBlockBypassed (buffer, midi);
    CHECK (juce::exactlyEqual (buffer.getSample (0, 2047), 0.125f));
    CHECK (juce::exactlyEqual (p.getBypassParameter()->getValue(), 0.0f));
    fill (buffer);
    p.processBlock (buffer, midi);
    CHECK (buffer.getSample (0, 0) > 0.125f);
    CHECK_THAT (buffer.getSample (0, 2047), Catch::Matchers::WithinAbs (0.125 * juce::Decibels::decibelsToGain (6.0), 1e-6));
}

TEST_CASE ("Bypassed Post and output meter follow delivered raw audio while Pre retains input trim", "[bypass][analyzer][meter]")
{
    PluginProcessor p;
    set (p, "input.gain", 6.0f);
    set (p, "output.gain", 12.0f);
    set (p, openq::bypassParameterId, 1.0f);
    p.prepareToPlay (48000, 16);
    auto& transport = p.getAnalyzerTransport();
    transport.setActive (true);
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buffer (2, 777);
    fill (buffer);
    p.processBlock (buffer, midi);
    const auto meters = p.getLevelMeters().read();
    CHECK_THAT (meters.peaks[0][0], Catch::Matchers::WithinAbs (0.125 * juce::Decibels::decibelsToGain (6.0), 1e-6));
    CHECK (juce::exactlyEqual (meters.peaks[1][0], 0.125f));
    CHECK (juce::exactlyEqual (meters.peaks[1][1], 0.25f));
    openq::AnalyzerPacket packet;
    int samples = 0;
    std::optional<std::uint64_t> nextSequence;
    while (transport.read (packet))
    {
        if (!nextSequence)
            nextSequence = packet.sequence;
        CHECK (packet.sequence == *nextSequence);
        *nextSequence += static_cast<std::uint64_t> (packet.samples);
        for (int s = 0; s < packet.samples; ++s)
        {
            CHECK (juce::exactlyEqual (packet.post[0][static_cast<size_t> (s)], 0.125f));
            CHECK_THAT (packet.pre[0][static_cast<size_t> (s)], Catch::Matchers::WithinAbs (meters.peaks[0][0], 1e-6));
        }
        samples += packet.samples;
    }
    CHECK (samples == buffer.getNumSamples());
}

TEST_CASE ("Header total bypass is the rightmost control and follows parameter automation", "[bypass][ui]")
{
    PluginProcessor p;
    PluginEditor editor (p);
    auto* header = editor.findChildWithID ("editor.header");
    REQUIRE (header != nullptr);
    auto* button = dynamic_cast<juce::Button*> (header->findChildWithID (openq::bypassParameterId));
    REQUIRE (button != nullptr);
    CHECK_FALSE (button->getToggleState());
    button->triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
    CHECK (button->getToggleState());
    CHECK (juce::exactlyEqual (p.getBypassParameter()->getValue(), 1.0f));
    set (p, openq::bypassParameterId, 0.0f);
    CHECK_FALSE (button->getToggleState());
    for (const int width : { openq::EditorSize::minimumWidth, openq::EditorSize::maximumWidth })
    {
        editor.setSize (width, openq::EditorSize::defaultHeight);
        for (auto* child : header->getChildren())
            if (child != button && child->isVisible())
                CHECK (child->getRight() <= button->getX());
        CHECK (header->getLocalBounds().contains (button->getBounds()));
    }
}

TEST_CASE ("Post capture and meter peaks include the actual global bypass transition", "[bypass][analyzer][meter]")
{
    PluginProcessor p;
    set (p, "output.gain", 6.0f);
    p.prepareToPlay (48000, 64);
    p.getAnalyzerTransport().setActive (true);
    set (p, openq::bypassParameterId, 1.0f);
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buffer (2, 2048);
    fill (buffer);
    p.processBlock (buffer, midi);
    const auto meters = p.getLevelMeters().read();
    for (int c = 0; c < buffer.getNumChannels(); ++c)
        CHECK (juce::exactlyEqual (meters.peaks[1][static_cast<size_t> (c)], buffer.getMagnitude (c, 0, buffer.getNumSamples())));
    openq::AnalyzerPacket packet;
    int offset = 0;
    while (p.getAnalyzerTransport().read (packet))
    {
        for (int c = 0; c < packet.channels; ++c)
            for (int sample = 0; sample < packet.samples; ++sample)
                REQUIRE (juce::exactlyEqual (packet.post[static_cast<size_t> (c)][static_cast<size_t> (sample)], buffer.getSample (c, offset + sample)));
        offset += packet.samples;
    }
    CHECK (offset == buffer.getNumSamples());
}

TEST_CASE ("Global bypass dims dynamic meter colours without changing static artwork or readings", "[bypass][ui][meter]")
{
    openq::LevelMeter meter ("IN");
    meter.setSize (openq::LevelMeter::preferredWidth, 400);
    meter.update ({ 0.25f, 0.5f }, 2, 0.1);
    auto* artwork = meter.findChildWithID ("meter.artwork");
    REQUIRE (artwork != nullptr);
    const auto original = snapshot (meter), staticOriginal = snapshot (*artwork);
    meter.setGloballyBypassed (true);
    const auto dimmed = snapshot (meter);
    CHECK (difference (original, dimmed) > 0);
    CHECK (difference (staticOriginal, snapshot (*artwork)) == 0);
    // The numeric readout is above the bar area and stays readable.
    CHECK (difference (original.getClippedImage ({ 0, 0, meter.getWidth(), 30 }), dimmed.getClippedImage ({ 0, 0, meter.getWidth(), 30 })) == 0);
    meter.setGloballyBypassed (false);
    CHECK (difference (original, snapshot (meter)) == 0);
    CHECK (juce::exactlyEqual (meter.getAlpha(), 1.0f));
}

TEST_CASE ("Global bypass dims nodes and curves while preserving graph artwork and editability", "[bypass][ui][response]")
{
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr)
        SKIP ("Response worker rendering requires a display.");
    PluginProcessor p;
    p.prepareToPlay (48000, 64);
    PluginEditor editor (p);
    editor.addToDesktop (juce::ComponentPeer::windowIsTemporary);
    editor.setVisible (true);
    auto* graph = dynamic_cast<openq::ResponseGraph*> (editor.findChildWithID ("response.graph"));
    REQUIRE (graph != nullptr);
    auto* node = graph->findChildWithID ("node.0");
    auto* artwork = graph->findChildWithID ("response.artwork");
    auto* curves = graph->findChildWithID ("response.curves");
    REQUIRE (node != nullptr);
    REQUIRE (artwork != nullptr);
    REQUIRE (curves != nullptr);
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 2000.0;
    bool ready = false;
    while (!ready && juce::Time::getMillisecondCounterHiRes() < deadline)
    {
        (void) snapshot (*graph);
        const auto image = snapshot (*curves);
        for (int y = 0; y < image.getHeight() && !ready; ++y)
            for (int x = 0; x < image.getWidth() && !ready; ++x)
                ready = image.getPixelAt (x, y) == openq::palette::text;
        if (!ready)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
    }
    REQUIRE (ready);
    const auto nodeOn = snapshot (*node), curvesOn = snapshot (*curves), artworkOn = snapshot (*artwork);
    set (p, openq::bypassParameterId, 1.0f);
    graph->onFrameRefresh();
    CHECK (difference (nodeOn, snapshot (*node)) > 0);
    CHECK (difference (curvesOn, snapshot (*curves)) > 0);
    CHECK (difference (artworkOn, snapshot (*artwork)) == 0);
    CHECK (node->isEnabled());
    CHECK (juce::exactlyEqual (graph->getAlpha(), 1.0f));
    const auto state = p.parameters.copyState();
    set (p, openq::bypassParameterId, 0.0f);
    graph->onFrameRefresh();
    CHECK (difference (curvesOn, snapshot (*curves)) == 0);
    CHECK (difference (nodeOn, snapshot (*node)) == 0);
    // Visual refresh itself never modifies band parameters.
    const auto after = p.parameters.copyState();
    for (size_t band = 0; band < openq::bandCount; ++band)
        for (const auto* name : openq::bandParameterNames)
        {
            const auto id = openq::bandParameterId (band, name);
            CHECK (state.getChildWithProperty ("id", id).isEquivalentTo (after.getChildWithProperty ("id", id)));
        }
}
