// SPDX-License-Identifier: AGPL-3.0-only
#include "PluginEditor.h"
#include "dsp/GainStage.h"
#include "dsp/PeakLatch.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <limits>
#include <thread>
namespace
{
    void setGain (PluginProcessor& processor, size_t stage, float db)
    {
        auto* parameter = processor.parameters.getParameter (openq::gainParameterIds[stage]);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (db));
    }
    juce::MouseEvent click (juce::Component& target, int modifiers = juce::ModifierKeys::leftButtonModifier)
    {
        const auto time = juce::Time::getCurrentTime();
        return { juce::Desktop::getInstance().getMainMouseSource(), { 5.0f, 5.0f }, juce::ModifierKeys (modifiers), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &target, &target, time, {}, time, 2, false };
    }
    struct GestureCounter : juce::AudioProcessorParameter::Listener
    {
        explicit GestureCounter (juce::AudioProcessorParameter& target) : parameter (target) { parameter.addListener (this); }
        ~GestureCounter() override { parameter.removeListener (this); }
        void parameterValueChanged (int, float) override { ++changes; }
        void parameterGestureChanged (int, bool starting) override
        {
            active += starting ? 1 : -1;
            if (starting)
                ++starts;
        }
        juce::AudioProcessorParameter& parameter;
        int active = 0, starts = 0, changes = 0;
    };
}

TEST_CASE ("Peak latch preserves interval maxima and rejects invalid values", "[meter][realtime]")
{
    openq::PeakLatch peak;
    peak.publish (0.8f);
    peak.publish (0.2f);
    CHECK_THAT (peak.read(), Catch::Matchers::WithinAbs (0.8f, 1e-7));
    CHECK_THAT (peak.read(), Catch::Matchers::WithinAbs (0.0f, 1e-7));
    for (float invalid : { -1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN() })
        peak.publish (invalid);
    CHECK_THAT (peak.read(), Catch::Matchers::WithinAbs (0.0f, 1e-7));
    peak.publish (2.0f);
    CHECK_THAT (peak.read(), Catch::Matchers::WithinAbs (2.0f, 1e-7));
}

TEST_CASE ("Single producer peak publication survives concurrent consuming resets", "[meter][threading]")
{
    openq::PeakLatch peak;
    constexpr int bursts = 2000, valuesPerBurst = 31;
    std::atomic<int> start { 0 }, finished { 0 };
    std::thread producer ([&] {
        for (int burst = 1; burst <= bursts; ++burst)
        {
            while (start.load (std::memory_order_acquire) < burst)
                std::this_thread::yield();
            for (int value = 0; value < valuesPerBurst; ++value)
            {
                peak.publish (static_cast<float> ((value * 17 + burst) % valuesPerBurst + 1));
                if (value % 3 == 0)
                    std::this_thread::yield();
            }
            finished.store (burst, std::memory_order_release);
        }
    });
    bool intact = true;
    for (int burst = 1; burst <= bursts; ++burst)
    {
        start.store (burst, std::memory_order_release);
        float maximum = 0.0f;
        while (finished.load (std::memory_order_acquire) < burst)
            maximum = std::max (maximum, peak.read());
        maximum = std::max (maximum, peak.read());
        intact = intact && juce::exactlyEqual (maximum, static_cast<float> (valuesPerBurst));
    }
    producer.join();
    CHECK (intact);
}

TEST_CASE ("Gain ramps are stereo linked and independent of host block boundaries", "[gain][dsp]")
{
    for (int channels : { 1, 2 })
        for (double sampleRate : { 48000.0, 96000.0 })
        {
            const auto render = [&] (int blockSize) {
                PluginProcessor processor;
                if (channels == 1)
                {
                    auto layout = processor.getBusesLayout();
                    layout.inputBuses.set (0, juce::AudioChannelSet::mono());
                    layout.outputBuses.set (0, juce::AudioChannelSet::mono());
                    REQUIRE (processor.setBusesLayout (layout));
                }
                processor.prepareToPlay (sampleRate, blockSize);
                setGain (processor, 0, 6.0f);
                setGain (processor, 1, -3.0f);
                const int total = static_cast<int> (sampleRate * 0.05);
                std::vector<float> result;
                juce::MidiBuffer midi;
                for (int offset = 0; offset < total; offset += blockSize)
                {
                    juce::AudioBuffer<float> buffer (channels, std::min (blockSize, total - offset));
                    for (int channel = 0; channel < channels; ++channel)
                        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                            buffer.setSample (channel, sample, channel == 0 ? 0.1f : -0.05f);
                    processor.processBlock (buffer, midi);
                    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                    {
                        result.push_back (buffer.getSample (0, sample));
                        if (channels == 2)
                            CHECK_THAT (buffer.getSample (1, sample), Catch::Matchers::WithinAbs (-0.5f * buffer.getSample (0, sample), 1e-7));
                    }
                }
                return result;
            };
            const auto a = render (64), b = render (257);
            REQUIRE (a.size() == b.size());
            for (size_t sample = 0; sample < a.size(); ++sample)
                REQUIRE_THAT (a[sample], Catch::Matchers::WithinAbs (b[sample], 1e-7));
            CHECK_THAT (a.back(), Catch::Matchers::WithinAbs (0.1 * std::pow (10.0, 3.0 / 20.0), 1e-6));
            const auto settled = static_cast<size_t> (sampleRate * openq::gainLimits::smoothingSeconds);
            CHECK_THAT (a[settled], Catch::Matchers::WithinAbs (a.back(), 1e-6));
            CHECK (a.front() < a.back());
        }
}

TEST_CASE ("Gain meters and analyzer taps measure trimmed input and delivered output", "[gain][meter][analyzer]")
{
    PluginProcessor processor;
    setGain (processor, 0, 6.0f);
    setGain (processor, 1, -6.0f);
    processor.prepareToPlay (48000.0, 256);
    processor.getAnalyzerTransport().setActive (true);
    juce::AudioBuffer<float> buffer (2, openq::AnalyzerPacket::capacity);
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        buffer.setSample (0, sample, 0.5f);
        buffer.setSample (1, sample, -0.25f);
    }
    juce::MidiBuffer midi;
    processor.processBlock (buffer, midi);
    const auto levels = processor.getLevelMeters().read();
    const auto boost = std::pow (10.0f, 6.0f / 20.0f);
    CHECK_THAT (levels.peaks[0][0], Catch::Matchers::WithinAbs (0.5f * boost, 1e-6));
    CHECK_THAT (levels.peaks[0][1], Catch::Matchers::WithinAbs (0.25f * boost, 1e-6));
    CHECK_THAT (levels.peaks[1][0], Catch::Matchers::WithinAbs (0.5f, 1e-6));
    openq::AnalyzerPacket packet;
    REQUIRE (processor.getAnalyzerTransport().read (packet));
    CHECK_THAT (packet.pre[0][0], Catch::Matchers::WithinAbs (levels.peaks[0][0], 1e-6));
    CHECK_THAT (packet.post[0][0], Catch::Matchers::WithinAbs (levels.peaks[1][0], 1e-6));
    auto settings = processor.getAnalyzerTransport().getSettings();
    settings.pre = settings.post = false;
    processor.getAnalyzerTransport().setSettings (settings);
    for (int block = 0; block < 8; ++block)
    {
        buffer.clear();
        buffer.setSample (0, block, block == 2 ? 0.9f : 0.1f);
        processor.processBlock (buffer, midi);
    }
    CHECK_THAT (processor.getLevelMeters().read().peaks[0][0], Catch::Matchers::WithinAbs (0.9f * boost, 1e-6));
    CHECK_FALSE (processor.getAnalyzerTransport().read (packet));
}

TEST_CASE ("Trim extremes remain finite and preserve floating point headroom", "[gain][dsp][meter]")
{
    openq::GainStage stage;
    stage.prepare (48000.0, 30.0f);
    juce::AudioBuffer<float> buffer (2, 4);
    buffer.clear();
    buffer.setSample (0, 0, 0.1f);
    buffer.setSample (0, 1, std::numeric_limits<float>::quiet_NaN());
    buffer.setSample (0, 2, std::numeric_limits<float>::infinity());
    buffer.setSample (0, 3, std::numeric_limits<float>::max());
    const auto peaks = stage.process (buffer, 2);
    CHECK (peaks[0] > 1.0f);
    CHECK_THAT (buffer.getSample (0, 0), Catch::Matchers::WithinAbs (0.1 * std::pow (10.0, 30.0 / 20.0), 1e-5));
    for (int sample = 1; sample < buffer.getNumSamples(); ++sample)
        CHECK_THAT (buffer.getSample (0, sample), Catch::Matchers::WithinAbs (0.0f, 1e-7));
}

TEST_CASE ("Global trims save restore and reject malformed complete states", "[gain][state]")
{
    PluginProcessor processor;
    setGain (processor, 0, 12.0f);
    setGain (processor, 1, -9.0f);
    juce::MemoryBlock saved;
    processor.getStateInformation (saved);
    PluginProcessor restored;
    restored.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    CHECK (restored.getParameters().size() == static_cast<int> (openq::parameterCount));
    const auto unchanged = restored.parameters.copyState();
    const auto load = [&] (const juce::ValueTree& tree) {
        juce::MemoryBlock bytes;
        juce::AudioProcessor::copyXmlToBinary (*tree.createXml(), bytes);
        restored.setStateInformation (bytes.getData(), static_cast<int> (bytes.getSize()));
        CHECK (restored.parameters.copyState().isEquivalentTo (unchanged));
    };
    for (const auto* id : openq::gainParameterIds)
    {
        CHECK_THAT (restored.parameters.getRawParameterValue (id)->load(), Catch::Matchers::WithinAbs (processor.parameters.getRawParameterValue (id)->load(), 1e-6));
        for (const auto* invalid : { "nan", "inf", "-31", "30.1", "1 trailing" })
        {
            auto state = unchanged.createCopy();
            state.getChildWithProperty ("id", id).setProperty ("value", invalid, nullptr);
            state.getChildWithProperty ("id", "band.0.gain").setProperty ("value", 10.0, nullptr);
            load (state);
        }
        auto missing = unchanged.createCopy();
        missing.removeChild (missing.getChildWithProperty ("id", id), nullptr);
        load (missing);
        auto duplicate = unchanged.createCopy();
        duplicate.removeChild (duplicate.getChildWithProperty ("id", id), nullptr);
        duplicate.addChild (duplicate.getChild (0).createCopy(), -1, nullptr);
        load (duplicate);
    }
    for (const auto* invalid : { "5", "6.5", "6 trailing" })
    {
        auto state = unchanged.createCopy();
        state.setProperty ("schemaVersion", invalid, nullptr);
        load (state);
    }
}

TEST_CASE ("Gain fields support exact entry reset cancellation and host gestures", "[gain][ui]")
{
    PluginProcessor processor;
    PluginEditor editor (processor);
    auto* bottom = editor.findChildWithID ("editor.bottom");
    REQUIRE (bottom != nullptr);
    auto* input = dynamic_cast<openq::GainControl*> (bottom->findChildWithID (openq::gainParameterIds[0]));
    auto* output = dynamic_cast<openq::GainControl*> (bottom->findChildWithID (openq::gainParameterIds[1]));
    auto* analyzer = bottom->findChildWithID ("analyzer.settings");
    REQUIRE (input != nullptr);
    REQUIRE (output != nullptr);
    REQUIRE (analyzer != nullptr);
    CHECK (input->getRight() < analyzer->getX());
    CHECK (analyzer->getRight() < output->getX());
    auto* parameter = processor.parameters.getParameter (openq::gainParameterIds[0]);
    GestureCounter gestures (*parameter);
    auto* value = dynamic_cast<juce::Label*> (input->findChildWithID ("gain.value"));
    REQUIRE (value != nullptr);
    value->setText ("+6.25 dB", juce::sendNotificationSync);
    CHECK_THAT (parameter->convertFrom0to1 (parameter->getValue()), Catch::Matchers::WithinAbs (6.25, 1e-5));
    CHECK (gestures.active == 0);
    CHECK (gestures.starts == 1);
    for (const auto* invalid : { "nan", "inf", "banana", "4 trailing" })
    {
        value->setText (invalid, juce::sendNotificationSync);
        CHECK_THAT (input->getValue(), Catch::Matchers::WithinAbs (6.25, 1e-5));
    }
    input->mouseDoubleClick (click (*input, juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::commandModifier));
    CHECK_THAT (input->getValue(), Catch::Matchers::WithinAbs (0.0, 1e-6));
    CHECK (gestures.active == 0);
    CHECK (gestures.starts == 2);
    setGain (processor, 0, -4.5f);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    CHECK_THAT (input->getValue(), Catch::Matchers::WithinAbs (-4.5, 1e-5));
    CHECK (gestures.starts == 2);
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr)
    {
        editor.addToDesktop (juce::ComponentPeer::windowIsTemporary);
        editor.setVisible (true);
        input->mouseDoubleClick (click (*input));
        auto* text = value->getCurrentTextEditor();
        REQUIRE (text != nullptr);
        text->setText ("18.0", false);
        CHECK (text->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        CHECK_THAT (input->getValue(), Catch::Matchers::WithinAbs (-4.5, 1e-5));
        input->mouseDoubleClick (click (*input));
        text = value->getCurrentTextEditor();
        REQUIRE (text != nullptr);
        text->setText ("-12.3", false);
        CHECK (text->keyPressed (juce::KeyPress (juce::KeyPress::returnKey)));
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        CHECK_THAT (input->getValue(), Catch::Matchers::WithinAbs (-12.3, 1e-5));
        CHECK (gestures.active == 0);
    }
}

TEST_CASE ("Meter decay is refresh independent and reset clears held readings", "[meter][ui]")
{
    const auto render = [] (int fps) {
        openq::LevelMeter meter ("IN");
        meter.setSize (openq::LevelMeter::preferredWidth, 300);
        meter.update ({ 0.5f, 0.25f }, 2, 0.1);
        for (int frame = 0; frame < fps; ++frame)
            meter.update ({}, 2, 1.0 / fps);
        return meter.createComponentSnapshot (meter.getLocalBounds());
    };
    const auto a = render (30), b = render (60);
    int differences = 0;
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
        {
            const auto ca = a.getPixelAt (x, y), cb = b.getPixelAt (x, y);
            if (std::abs (ca.getRed() - cb.getRed()) > 2 || std::abs (ca.getGreen() - cb.getGreen()) > 2 || std::abs (ca.getBlue() - cb.getBlue()) > 2)
                ++differences;
        }
    CHECK (differences == 0);
    openq::LevelMeter meter ("OUT");
    meter.setSize (openq::LevelMeter::preferredWidth, 300);
    const auto empty = meter.createComponentSnapshot (meter.getLocalBounds());
    meter.update ({ 2.0f, 0.5f }, 2, 0.1);
    const auto lit = meter.createComponentSnapshot (meter.getLocalBounds());
    CHECK (lit.getPixelAt (10, 100) != empty.getPixelAt (10, 100));
    meter.mouseDown (click (meter));
    meter.reset();
    const auto reset = meter.createComponentSnapshot (meter.getLocalBounds());
    for (int y = 0; y < empty.getHeight(); ++y)
        for (int x = 0; x < empty.getWidth(); ++x)
            REQUIRE (empty.getPixelAt (x, y) == reset.getPixelAt (x, y));
}

TEST_CASE ("Gain and meter layout keeps strips clear while resized and shows live readings", "[gain][meter][ui][layout]")
{
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr)
        SKIP ("Live meter layout requires a display.");
    PluginProcessor processor;
    setGain (processor, 0, 6.0f);
    setGain (processor, 1, -3.0f);
    processor.prepareToPlay (48000.0, 512);
    PluginEditor editor (processor);
    editor.addToDesktop (juce::ComponentPeer::windowIsTemporary);
    editor.setVisible (true);
    auto* input = dynamic_cast<openq::LevelMeter*> (editor.findChildWithID ("meter.input"));
    auto* output = dynamic_cast<openq::LevelMeter*> (editor.findChildWithID ("meter.output"));
    auto* graph = editor.findChildWithID ("response.graph");
    auto* card = editor.findChildWithID ("band.card");
    auto* bottom = editor.findChildWithID ("editor.bottom");
    REQUIRE (input != nullptr);
    REQUIRE (output != nullptr);
    REQUIRE (graph != nullptr);
    REQUIRE (card != nullptr);
    REQUIRE (bottom != nullptr);
    auto* node = graph->findChildWithID ("node.0");
    REQUIRE (node != nullptr);
    node->grabKeyboardFocus();
    REQUIRE (card->isVisible());
    juce::AudioBuffer<float> audio (2, 512);
    juce::MidiBuffer midi;
    auto render = [&] {
        for (int block = 0; block < 6; ++block)
        {
            for (int sample = 0; sample < audio.getNumSamples(); ++sample)
            {
                audio.setSample (0, sample, 0.25f);
                audio.setSample (1, sample, 0.125f);
            }
            processor.processBlock (audio, midi);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (35);
        }
    };
    const auto verify = [&] {
        CHECK (input->getX() == 0);
        CHECK (output->getRight() == editor.getWidth());
        CHECK (input->getBottom() == bottom->getY());
        CHECK (output->getBottom() == bottom->getY());
        CHECK_FALSE (card->getBounds().intersects (input->getBounds()));
        CHECK_FALSE (card->getBounds().intersects (output->getBounds()));
        CHECK_FALSE (card->getBounds().intersects (bottom->getBounds()));
    };
    render();
    verify();
    auto snapshot = [&] (const char* name) {
        juce::MemoryOutputStream png;
        REQUIRE (juce::PNGImageFormat().writeImageToStream (editor.createComponentSnapshot (editor.getLocalBounds()), png));
        auto* artwork = graph->findChildWithID ("response.artwork");
        REQUIRE (artwork != nullptr);
        for (auto* label : artwork->getChildren())
            if (label->isVisible())
            {
                const auto bounds = editor.getLocalArea (label, label->getLocalBounds());
                CHECK_FALSE (bounds.intersects (input->getBounds()));
                CHECK_FALSE (bounds.intersects (output->getBounds()));
            }
        REQUIRE (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (name).replaceWithData (png.getData(), png.getDataSize()));
    };
    snapshot ("openq-gain-meters.png");
    editor.setSize (1440, 900);
    render();
    verify();
    snapshot ("openq-gain-meters-large.png");
    editor.setSize (openq::EditorSize::maximumWidth, openq::EditorSize::minimumHeight);
    verify();
    auto* gain = dynamic_cast<openq::GainControl*> (bottom->findChildWithID (openq::gainParameterIds[0]));
    REQUIRE (gain != nullptr);
    gain->setValue (0.0, juce::sendNotificationSync);
    auto ordinary = click (*gain);
    gain->mouseDown (ordinary);
    gain->mouseDrag (ordinary.withNewPosition (juce::Point<float> { 5.0f, -45.0f }));
    gain->mouseUp (ordinary);
    const auto normalChange = gain->getValue();
    gain->setValue (0.0, juce::sendNotificationSync);
    const auto normal = gain->getMouseDragSensitivity();
    auto fine = click (*gain, juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier);
    gain->mouseDown (fine);
    CHECK_THAT (gain->getMouseDragSensitivity(), Catch::Matchers::WithinAbs (normal * 10, 5.0));
    gain->mouseDrag (fine.withNewPosition (juce::Point<float> { 5.0f, -45.0f }));
    gain->mouseUp (fine);
    CHECK (normalChange > gain->getValue());
    CHECK (gain->getValue() > 0.0);
    gain->mouseDown (ordinary);
    CHECK (gain->getMouseDragSensitivity() == normal);
    gain->mouseUp (ordinary);
}

TEST_CASE ("Stalled gain drags discard catch-up movement and keep host gestures balanced", "[gain][ui][stall]")
{
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr)
        SKIP ("Gain drag focus requires a display.");
    PluginProcessor processor;
    PluginEditor editor (processor);
    editor.addToDesktop (juce::ComponentPeer::windowIsTemporary);
    editor.setVisible (true);
    auto* bottom = editor.findChildWithID ("editor.bottom");
    REQUIRE (bottom != nullptr);
    for (size_t stage = 0; stage < openq::gainParameterIds.size(); ++stage)
    {
        auto* gain = dynamic_cast<openq::GainControl*> (bottom->findChildWithID (openq::gainParameterIds[stage]));
        REQUIRE (gain != nullptr);
        auto* parameter = processor.parameters.getParameter (openq::gainParameterIds[stage]);
        GestureCounter gestures (*parameter);
        const auto down = click (*gain);
        const auto moved = down.withNewPosition (juce::Point<float> { 5.0f, -200.0f });
        for (bool timerFirst : { false, true })
        {
            gain->setValue (0.0, juce::sendNotificationSync);
            gain->mouseDown (down);
            REQUIRE (gestures.active == 1);
            const auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;
            // Model either the timer or the pending mouse event being delivered first after a stall.
            gain->messageThreadTick (now + (timerFirst ? 1.0 : -1.0));
            gain->mouseDrag (moved);
            CHECK_THAT (gain->getValue(), Catch::Matchers::WithinAbs (0.0, 1e-6));
            CHECK (gestures.active == 0);
            gain->mouseDrag (moved);
            gain->mouseUp (down);
            CHECK (gestures.active == 0);
        }
        gain->mouseDown (down);
        const auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        // Holding still is valid while the message thread continues ticking.
        for (int pulse = 1; pulse <= 10; ++pulse)
            gain->messageThreadTick (now + pulse * 0.05);
        gain->mouseDrag (moved);
        CHECK (gain->getValue() > 0.0);
        gain->mouseUp (down);
        CHECK (gestures.active == 0);
        CHECK (gestures.starts == 3);
    }
}
