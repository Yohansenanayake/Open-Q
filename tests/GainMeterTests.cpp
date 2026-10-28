// SPDX-License-Identifier: AGPL-3.0-only
#include "PluginEditor.h"
#include "ui/GainTimingTrace.h"
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
    juce::MouseEvent click (juce::Component& target, int modifiers = juce::ModifierKeys::leftButtonModifier, juce::Time time = juce::Time::getCurrentTime())
    {
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

TEST_CASE ("Trim parameter round trips preserve exact zero and dry audio", "[gain][dsp][state][regression]")
{
    PluginProcessor processor;
    for (const auto* id : openq::gainParameterIds)
    {
        auto* parameter = processor.parameters.getParameter (id);
        REQUIRE (parameter != nullptr);
        // The old stepped range can only fail numerically when the compiler fuses its snapping arithmetic.
        CHECK (juce::exactlyEqual (parameter->getNormalisableRange().interval, 0.0f));
        CHECK (juce::exactlyEqual (parameter->convertFrom0to1 (parameter->getDefaultValue()), 0.0f));
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (6.0f));
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (0.0f));
        CHECK (juce::exactlyEqual (processor.parameters.getRawParameterValue (id)->load(), 0.0f));
    }
    const auto checkDry = [] (PluginProcessor& target) {
        juce::AudioBuffer<float> buffer (2, 64);
        juce::MidiBuffer midi;
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            buffer.setSample (0, sample, 0.137f);
            buffer.setSample (1, sample, -0.3f);
        }
        target.processBlock (buffer, midi);
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            CHECK (juce::exactlyEqual (buffer.getSample (0, sample), 0.137f));
            CHECK (juce::exactlyEqual (buffer.getSample (1, sample), -0.3f));
        }
    };
    processor.prepareToPlay (48000.0, 1024);
    checkDry (processor);
    juce::AudioBuffer<float> settling (2, 1024);
    juce::MidiBuffer midi;
    for (float db : { 6.0f, 0.0f })
    {
        for (size_t stage = 0; stage < openq::gainParameterIds.size(); ++stage)
            setGain (processor, stage, db);
        settling.clear();
        processor.processBlock (settling, midi);
    }
    checkDry (processor);
    juce::MemoryBlock saved;
    processor.getStateInformation (saved);
    PluginProcessor restored;
    restored.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    for (const auto* id : openq::gainParameterIds)
        CHECK (juce::exactlyEqual (restored.parameters.getRawParameterValue (id)->load(), 0.0f));
    restored.prepareToPlay (48000.0, 64);
    checkDry (restored);
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

TEST_CASE ("Gain wheel edits are immediate fine reversible fractional and bounded", "[gain][ui][wheel]")
{
    for (size_t stage = 0; stage < openq::gainParameterIds.size(); ++stage)
    {
        CAPTURE (stage);
        PluginProcessor processor;
        openq::GainControl control (processor.parameters, openq::gainParameterIds[stage]);
        auto* parameter = processor.parameters.getParameter (openq::gainParameterIds[stage]);
        GestureCounter gestures (*parameter);
        juce::int64 timestamp = 1000;
        const juce::MouseWheelDetails up { 0.0f, 0.1f, false, false, false };
        const auto send = [&] (juce::MouseWheelDetails wheel, int modifiers = 0) {
            control.mouseWheelMove (click (control, modifiers, juce::Time (++timestamp)), wheel);
        };
        const auto checkValue = [&] (double expected) {
            CHECK_THAT (control.getValue(), Catch::Matchers::WithinAbs (expected, 1e-5));
            CHECK_THAT (processor.parameters.getRawParameterValue (openq::gainParameterIds[stage])->load(), Catch::Matchers::WithinAbs (expected, 1e-5));
            CHECK (gestures.active == 0);
        };
        const auto first = click (control, 0, juce::Time (++timestamp));
        control.mouseWheelMove (first, up);
        checkValue (0.5);
        CHECK (gestures.starts == 1);
        control.mouseWheelMove (first, up);
        checkValue (0.5);
        CHECK (gestures.starts == 1);
        send (up, juce::ModifierKeys::shiftModifier);
        checkValue (0.6);
        auto wheel = up;
        wheel.isReversed = true;
        send (wheel);
        checkValue (0.1);
        send ({ 0.2f, 0.01f, false, false, false });
        checkValue (-0.4);
        setGain (processor, stage, 0.0f);
        wheel = { 0.0f, 0.01f, false, true, false };
        for (int i = 0; i < 4; ++i)
            send (wheel);
        checkValue (0.2);
        send (wheel, juce::ModifierKeys::shiftModifier);
        checkValue (0.21);
        setGain (processor, stage, 30.0f);
        auto starts = gestures.starts;
        send (up);
        checkValue (30.0);
        CHECK (gestures.starts == starts);
        wheel = up;
        wheel.deltaY = -up.deltaY;
        send (wheel);
        checkValue (29.5);
        CHECK (gestures.starts == starts + 1);
        setGain (processor, stage, -30.0f);
        starts = gestures.starts;
        send (wheel);
        checkValue (-30.0);
        CHECK (gestures.starts == starts);
        send (up);
        checkValue (-29.5);
        for (size_t other = 0; other < openq::gainParameterIds.size(); ++other)
            if (other != stage)
                CHECK (juce::exactlyEqual (processor.parameters.getRawParameterValue (openq::gainParameterIds[other])->load(), 0.0f));
    }
}

TEST_CASE ("Gain wheels ignore inactive invalid inertial dragging and editing input", "[gain][ui][wheel]")
{
    for (const auto* id : openq::gainParameterIds)
    {
        CAPTURE (id);
        PluginProcessor processor;
        openq::GainControl control (processor.parameters, id);
        control.setSize (100, 26);
        GestureCounter gestures (*processor.parameters.getParameter (id));
        juce::int64 timestamp = 2000;
        const juce::MouseWheelDetails up { 0.0f, 0.1f, false, false, false };
        const auto send = [&] (juce::MouseWheelDetails wheel, int modifiers = 0) {
            control.mouseWheelMove (click (control, modifiers, juce::Time (++timestamp)), wheel);
        };
        control.setEnabled (false);
        send (up);
        control.setEnabled (true);
        control.setScrollWheelEnabled (false);
        send (up);
        control.setScrollWheelEnabled (true);
        send (up, juce::ModifierKeys::leftButtonModifier);
        send ({ 0.0f, 0.1f, false, true, true });
        send ({ 0.0f, 0.0f, false, false, false });
        send ({ std::numeric_limits<float>::quiet_NaN(), 0.1f, false, false, false });
        send ({ 0.0f, std::numeric_limits<float>::infinity(), false, false, false });
        CHECK (juce::exactlyEqual (control.getValue(), 0.0));
        CHECK (gestures.starts == 0);
        control.mouseDown (click (control));
        const auto draggingValue = control.getValue();
        const auto draggingStarts = gestures.starts;
        send (up);
        CHECK (juce::exactlyEqual (control.getValue(), draggingValue));
        CHECK (gestures.starts == draggingStarts);
        control.mouseUp (click (control, 0));
        CHECK (gestures.active == 0);
        auto* label = dynamic_cast<juce::Label*> (control.findChildWithID ("gain.value"));
        REQUIRE (label != nullptr);
        REQUIRE (control.keyPressed (juce::KeyPress (juce::KeyPress::returnKey)));
        auto* text = label->getCurrentTextEditor();
        REQUIRE (text != nullptr);
        text->setText ("11.0", false);
        const auto editingValue = control.getValue();
        const auto editingStarts = gestures.starts;
        send (up);
        CHECK (label->getCurrentTextEditor() == text);
        CHECK (text->getText() == "11.0");
        CHECK (juce::exactlyEqual (control.getValue(), editingValue));
        CHECK (gestures.starts == editingStarts);
        CHECK (gestures.active == 0);
        label->hideEditor (true);
    }
}

TEST_CASE ("Only local gain edits request priority until each readout paints", "[gain][ui][feedback]")
{
    PluginProcessor processor;
    PluginEditor editor (processor);
    auto* bottom = editor.findChildWithID ("editor.bottom");
    auto* graph = dynamic_cast<openq::ResponseGraph*> (editor.findChildWithID ("response.graph"));
    REQUIRE (bottom != nullptr);
    REQUIRE (graph != nullptr);
    REQUIRE (graph->hasPendingControlFeedback != nullptr);
    CHECK_FALSE (graph->hasPendingControlFeedback());
    for (size_t stage = 0; stage < openq::gainParameterIds.size(); ++stage)
    {
        auto* control = dynamic_cast<openq::GainControl*> (bottom->findChildWithID (openq::gainParameterIds[stage]));
        REQUIRE (control != nullptr);
        setGain (processor, stage, 1.0f);
        CHECK_FALSE (control->hasPendingUserFeedback());
        GestureCounter gestures (*processor.parameters.getParameter (openq::gainParameterIds[stage]));
        control->mouseWheelMove (click (*control, 0, juce::Time (3000)), { 0.0f, 0.1f, false, false, false });
        CHECK_THAT (control->getValue(), Catch::Matchers::WithinAbs (1.5, 1e-5));
        auto* label = dynamic_cast<juce::Label*> (control->findChildWithID ("gain.value"));
        REQUIRE (label != nullptr);
        CHECK (label->getText() == "1.5 dB");
        CHECK (control->hasPendingUserFeedback());
        CHECK (graph->hasPendingControlFeedback());
        CHECK (gestures.starts == 1);
        CHECK (gestures.active == 0);
        // A border-only paint is not acknowledgement of the new readout.
        juce::Image partial (juce::Image::ARGB, control->getWidth(), control->getHeight(), true);
        juce::Graphics g (partial);
        g.reduceClipRegion ({ 0, 0, 1, 1 });
        control->paintEntireComponent (g, true);
        CHECK (control->hasPendingUserFeedback());
        (void) control->createComponentSnapshot (control->getLocalBounds());
        CHECK_FALSE (control->hasPendingUserFeedback());
        CHECK_FALSE (graph->hasPendingControlFeedback());
        label->setText ("3.0 dB", juce::sendNotificationSync);
        CHECK (control->hasPendingUserFeedback());
        (void) control->createComponentSnapshot (control->getLocalBounds());
        control->mouseDoubleClick (click (*control, juce::ModifierKeys::commandModifier));
        CHECK (control->hasPendingUserFeedback());
        control->setVisible (false);
        CHECK_FALSE (control->hasPendingUserFeedback());
        control->setVisible (true);
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
        const auto waitFor = [] (auto&& ready) {
            const auto deadline = juce::Time::getMillisecondCounterHiRes() + 2000.0;
            do
            {
                juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
            } while (!ready() && juce::Time::getMillisecondCounterHiRes() < deadline);
            return ready();
        };
        editor.addToDesktop (juce::ComponentPeer::windowHasTitleBar);
        editor.setVisible (true);
       #if JUCE_MAC
        // Synthetic key calls do not perform AppKit's normal activation on a real mouse click.
        juce::Process::makeForegroundProcess();
        REQUIRE (waitFor ([] { return juce::Process::isForegroundProcess(); }));
       #endif
        const auto beginEditing = [&] {
            editor.toFront (true);
            editor.grabKeyboardFocus();
            REQUIRE (waitFor ([&] { return editor.hasKeyboardFocus (false); }));
            input->mouseDoubleClick (click (*input));
            REQUIRE (waitFor ([&] {
                const auto* text = value->getCurrentTextEditor();
                return text != nullptr && text->hasKeyboardFocus (false);
            }));
            return juce::Component::SafePointer<juce::TextEditor> (value->getCurrentTextEditor());
        };
        auto text = beginEditing();
        text->setText ("18.0", false);
        CHECK (text->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
        REQUIRE (waitFor ([&] { return value->getCurrentTextEditor() == nullptr; }));
        CHECK_THAT (input->getValue(), Catch::Matchers::WithinAbs (-4.5, 1e-5));
        CHECK (gestures.starts == 2);
        text = beginEditing();
        text->setText ("-12.3", false);
        CHECK (text->keyPressed (juce::KeyPress (juce::KeyPress::returnKey)));
        REQUIRE (waitFor ([&] { return value->getCurrentTextEditor() == nullptr; }));
        CHECK_THAT (input->getValue(), Catch::Matchers::WithinAbs (-12.3, 1e-5));
        CHECK (gestures.active == 0);
        CHECK (gestures.starts == 3);
        text = beginEditing();
        text->setText ("9.0", false);
        text->giveAwayKeyboardFocus();
        REQUIRE (waitFor ([&] { return value->getCurrentTextEditor() == nullptr; }));
        CHECK_THAT (input->getValue(), Catch::Matchers::WithinAbs (-12.3, 1e-5));
        CHECK (gestures.starts == 3);
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
    const auto a = render (2), b = render (60);
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

TEST_CASE ("Gain timing capture records updates rejections and completed readout paints", "[gain][ui][timing]")
{
    PluginProcessor processor;
    openq::GainControl gain (processor.parameters, openq::gainParameterIds[0]);
    gain.setBounds (0, 0, 110, 26);
    auto& trace = openq::GainTimingTrace::instance();
    trace.start();
    juce::MouseWheelDetails wheel;
    wheel.deltaY = 0.2f;
    const auto event = click (gain, 0);
    gain.mouseWheelMove (event, wheel);
    gain.mouseWheelMove (event, wheel);
    gain.createComponentSnapshot (gain.getLocalBounds());
    trace.stop();
    const auto csv = trace.toCsv();
    const auto begin = csv.indexOf ("wheel_begin");
    const auto updated = csv.indexOf ("wheel_end,0,0.500000000");
    const auto rejected = csv.indexOf ("wheel_rejected_zero_or_duplicate");
    const auto painted = csv.indexOf ("readout_painted,0,0.500000000");
    CHECK (begin >= 0);
    CHECK (updated > begin);
    CHECK (rejected > updated);
    CHECK (painted > rejected);
    trace.record ("should_not_record");
    CHECK_FALSE (trace.toCsv().contains ("should_not_record"));

    trace.start();
    const auto down = click (gain);
    gain.mouseDown (down);
    gain.mouseDrag (down.withNewPosition (juce::Point<float> { 5.0f, -20.0f }));
    gain.mouseUp (down);
    trace.stop();
    CHECK (trace.toCsv().contains ("drag_begin"));
    CHECK (trace.toCsv().contains ("drag_end"));
}

TEST_CASE ("Gain timing capture is bounded and restarting clears old samples", "[gain][timing]")
{
    auto trace = std::make_unique<openq::GainTimingTrace>();
    trace->start();
    for (size_t i = 0; i < openq::GainTimingTrace::capacity + 3; ++i)
        trace->record ("sample");
    trace->stop();
    CHECK (trace->toCsv().contains ("# dropped_events,3"));
    trace->start();
    trace->record ("fresh");
    trace->stop();
    const auto csv = trace->toCsv();
    CHECK_FALSE (csv.contains ("sample"));
    CHECK (csv.contains ("fresh"));
    CHECK (csv.contains ("# dropped_events,0"));
}
