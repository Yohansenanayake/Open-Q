// SPDX-License-Identifier: AGPL-3.0-only
#include "PluginEditor.h"
#include <catch2/catch_test_macros.hpp>

namespace
{
    void observeMouseDown (PluginEditor& editor, juce::Component& target)
    {
        const auto time = juce::Time::getCurrentTime();
        const juce::MouseEvent event { juce::Desktop::getInstance().getMainMouseSource(), {}, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &target, &target, time, {}, time, 1, false };
        editor.mouseDown (event);
    }
}

TEST_CASE ("Analyzer configuration round trips coherently and rejects invalid choices", "[analyzer][settings]")
{
    openq::AnalyzerTransport transport;
    const auto session = transport.session();
    for (const auto smoothing : { openq::AnalyzerSmoothing::Erb, openq::AnalyzerSmoothing::Octave })
        for (unsigned erb = 0; erb < openq::analyzerErbAmounts.size(); ++erb)
            for (unsigned octave = 0; octave < openq::analyzerOctaveAmounts.size(); ++octave)
                for (unsigned decay = 0; decay < openq::analyzerDecaySpeeds.size(); ++decay)
                {
                    openq::AnalyzerSettings settings { false, true, openq::AnalyzerQuality::High, smoothing, erb, octave, static_cast<openq::AnalyzerDecay> (decay) };
                    REQUIRE (transport.setSettings (settings));
                    CHECK (transport.getSettings().encode() == settings.encode());
                    CHECK (openq::AnalyzerSettings::decode (settings.encode()).isValid());
                }
    const auto previous = transport.getSettings().encode();
    auto invalid = transport.getSettings();
    invalid.erbAmount = static_cast<unsigned> (openq::analyzerErbAmounts.size());
    CHECK_FALSE (transport.setSettings (invalid));
    invalid = transport.getSettings();
    invalid.decay = static_cast<openq::AnalyzerDecay> (openq::analyzerDecaySpeeds.size());
    CHECK_FALSE (transport.setSettings (invalid));
    CHECK (transport.getSettings().encode() == previous);
    CHECK (transport.session() == session);
}

TEST_CASE ("Analyzer configuration is validated before state replacement and older states use defaults", "[analyzer][state]")
{
    PluginProcessor source, restored;
    auto settings = source.getAnalyzerTransport().getSettings();
    settings.smoothing = openq::AnalyzerSmoothing::Octave;
    settings.erbAmount = 2;
    settings.octaveAmount = 3;
    settings.decay = openq::AnalyzerDecay::VerySlow;
    source.getAnalyzerTransport().setSettings (settings);
    juce::MemoryBlock saved;
    source.getStateInformation (saved);
    restored.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    CHECK (restored.getAnalyzerTransport().getSettings().encode() == settings.encode());
    for (const auto invalid : { "-1", "-4294967295", "256", "74.5", "nan", "78", "170", "74 trailing", "99999999999999999" })
    {
        auto state = source.parameters.copyState();
        state.setProperty ("analyzerConfiguration", invalid, nullptr);
        state.getChildWithProperty ("id", "band.0.gain").setProperty ("value", 12.0, nullptr);
        juce::MemoryBlock bytes;
        juce::AudioProcessor::copyXmlToBinary (*state.createXml(), bytes);
        restored.setStateInformation (bytes.getData(), static_cast<int> (bytes.getSize()));
        CHECK (restored.getAnalyzerTransport().getSettings().encode() == settings.encode());
        CHECK (juce::exactlyEqual (restored.parameters.getRawParameterValue ("band.0.gain")->load(), 0.0f));
    }
    auto older = source.parameters.copyState();
    older.removeProperty ("analyzerConfiguration", nullptr);
    older.setProperty ("analyzerSettings", 6, nullptr);
    juce::MemoryBlock bytes;
    juce::AudioProcessor::copyXmlToBinary (*older.createXml(), bytes);
    restored.setStateInformation (bytes.getData(), static_cast<int> (bytes.getSize()));
    auto expected = openq::AnalyzerSettings { false, true, openq::AnalyzerQuality::High };
    CHECK (restored.getAnalyzerTransport().getSettings().encode() == expected.encode());
}

TEST_CASE ("Analyzer panel remembers each smoothing amount and keeps preferences outside audio parameters", "[analyzer][ui][settings]")
{
    PluginProcessor processor;
    PluginEditor editor (processor);
    const auto before = processor.parameters.copyState();
    CHECK (editor.findChildWithID ("analyzer.panel") == nullptr);
    auto* button = dynamic_cast<juce::TextButton*> (editor.findChildWithID ("analyzer.settings"));
    REQUIRE (button != nullptr);
    button->setToggleState (true, juce::dontSendNotification);
    button->onClick();
    auto* panel = dynamic_cast<openq::AnalyzerPanel*> (editor.findChildWithID ("analyzer.panel"));
    REQUIRE (panel != nullptr);
    CHECK (panel->isVisible());
    auto* smoothing = dynamic_cast<openq::CompactComboBox*> (panel->findChildWithID ("analyzer.smoothing"));
    auto* amount = dynamic_cast<openq::CompactComboBox*> (panel->findChildWithID ("analyzer.amount"));
    auto* decay = dynamic_cast<openq::CompactComboBox*> (panel->findChildWithID ("analyzer.decay"));
    REQUIRE (smoothing != nullptr);
    REQUIRE (amount != nullptr);
    REQUIRE (decay != nullptr);
    CHECK (smoothing->getSelectedId() == 1);
    CHECK (amount->getSelectedId() == 2);
    CHECK (decay->getSelectedId() == 3);
    amount->setSelectedId (3, juce::sendNotificationSync);
    smoothing->setSelectedId (2, juce::sendNotificationSync);
    CHECK (amount->getSelectedId() == 2);
    amount->setSelectedId (4, juce::sendNotificationSync);
    smoothing->setSelectedId (1, juce::sendNotificationSync);
    CHECK (amount->getSelectedId() == 3);
    smoothing->setSelectedId (2, juce::sendNotificationSync);
    CHECK (amount->getSelectedId() == 4);
    decay->setSelectedId (5, juce::sendNotificationSync);
    CHECK (processor.getAnalyzerTransport().getSettings().decay == openq::AnalyzerDecay::VerySlow);
    juce::Label label;
    CHECK (decay->getLookAndFeel().getOptionsForComboBoxPopupMenu (*decay, label).getParentComponent() == &editor);
    editor.setSize (1440, 900);
    CHECK (editor.getLocalBounds().contains (panel->getBounds()));
    const auto image = editor.createComponentSnapshot (editor.getLocalBounds());
    juce::MemoryOutputStream output;
    REQUIRE (juce::PNGImageFormat().writeImageToStream (image, output));
    REQUIRE (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("openq-analyzer-settings.png").replaceWithData (output.getData(), output.getDataSize()));
    REQUIRE (panel->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
    CHECK_FALSE (panel->isVisible());
    CHECK_FALSE (button->getToggleState());
    CHECK (processor.parameters.copyState().isEquivalentTo (before));
    CHECK ((processor.getAnalyzerTransport().session() & 1u) == 0);
}

TEST_CASE ("Analyzer panel menus remain editor children and close safely on hide and resize", "[analyzer][ui][popup]")
{
    PluginProcessor processor;
    PluginEditor editor (processor);
    editor.setVisible (true);
    auto* button = dynamic_cast<juce::TextButton*> (editor.findChildWithID ("analyzer.settings"));
    REQUIRE (button != nullptr);
    button->setToggleState (true, juce::dontSendNotification);
    button->onClick();
    auto* panel = dynamic_cast<openq::AnalyzerPanel*> (editor.findChildWithID ("analyzer.panel"));
    REQUIRE (panel != nullptr);
    auto* quality = dynamic_cast<openq::CompactComboBox*> (panel->findChildWithID ("analyzer.quality"));
    REQUIRE (quality != nullptr);
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr)
        SKIP ("Embedded menu interaction requires a display.");
    const auto desktopCount = juce::Desktop::getInstance().getNumComponents();
    quality->showPopup();
    auto* popup = juce::Component::getCurrentlyModalComponent();
    REQUIRE (popup != nullptr);
    CHECK (popup->getParentComponent() == &editor);
    CHECK_FALSE (popup->isOnDesktop());
    observeMouseDown (editor, *popup);
    CHECK (panel->isVisible());
    CHECK (quality->isPopupActive());
    if (popup->getNumChildComponents() > 0)
    {
        observeMouseDown (editor, *popup->getChildComponent (0));
        CHECK (panel->isVisible());
    }
    CHECK (popup->keyPressed (juce::KeyPress (juce::KeyPress::downKey)));
    CHECK (popup->keyPressed (juce::KeyPress (juce::KeyPress::returnKey)));
    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    CHECK (processor.getAnalyzerTransport().getSettings().quality == openq::AnalyzerQuality::High);
    quality->showPopup();
    REQUIRE (quality->isPopupActive());
    editor.setSize (1013, 900);
    CHECK_FALSE (quality->isPopupActive());
    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    quality->showPopup();
    REQUIRE (quality->isPopupActive());
    panel->onClose();
    CHECK_FALSE (quality->isPopupActive());
    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    CHECK (juce::Component::getCurrentlyModalComponent() == nullptr);
    CHECK (juce::Desktop::getInstance().getNumComponents() == desktopCount);
}

TEST_CASE ("Analyzer cross and outside clicks dismiss without changing preferences or blocking controls", "[analyzer][ui][dismiss]")
{
    PluginProcessor processor;
    PluginEditor editor (processor);
    auto* button = dynamic_cast<juce::TextButton*> (editor.findChildWithID ("analyzer.settings"));
    REQUIRE (button != nullptr);
    button->setToggleState (true, juce::dontSendNotification);
    button->onClick();
    auto* panel = dynamic_cast<openq::AnalyzerPanel*> (editor.findChildWithID ("analyzer.panel"));
    auto* pre = dynamic_cast<juce::ToggleButton*> (editor.findChildWithID ("analyzer.pre"));
    auto* graph = editor.findChildWithID ("response.graph");
    REQUIRE (panel != nullptr);
    REQUIRE (button != nullptr);
    REQUIRE (pre != nullptr);
    REQUIRE (graph != nullptr);
    auto* close = dynamic_cast<juce::DrawableButton*> (panel->findChildWithID ("analyzer.close"));
    REQUIRE (close != nullptr);
    REQUIRE (close->getCurrentImage() != nullptr);
    const auto settings = processor.getAnalyzerTransport().getSettings().encode();
    const auto open = [&] {
        button->setToggleState (true, juce::dontSendNotification);
        button->onClick();
        REQUIRE (panel->isVisible());
    };
    open();
    observeMouseDown (editor, *panel);
    CHECK (panel->isVisible());
    auto* smoothing = panel->findChildWithID ("analyzer.smoothing");
    REQUIRE (smoothing != nullptr);
    observeMouseDown (editor, *smoothing);
    CHECK (panel->isVisible());
    observeMouseDown (editor, *button);
    CHECK (panel->isVisible()); // The button's normal click will toggle it off.
    button->setToggleState (false, juce::dontSendNotification);
    button->onClick();
    CHECK_FALSE (panel->isVisible());
    open();
    close->onClick();
    CHECK_FALSE (panel->isVisible());
    CHECK_FALSE (button->getToggleState());
    open();
    observeMouseDown (editor, *graph);
    CHECK_FALSE (panel->isVisible());
    CHECK_FALSE (button->getToggleState());
    CHECK (processor.getAnalyzerTransport().getSettings().encode() == settings);
    open();
    observeMouseDown (editor, *pre);
    CHECK_FALSE (panel->isVisible());
    pre->setToggleState (false, juce::dontSendNotification);
    pre->onClick();
    CHECK_FALSE (processor.getAnalyzerTransport().getSettings().pre);
    open();
    observeMouseDown (editor, editor);
    CHECK_FALSE (panel->isVisible());
}

TEST_CASE ("Host editor probing defers analyzer controls and preserves configuration", "[analyzer][ui][startup]")
{
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr)
        SKIP ("Startup assertion regression requires a display.");
    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    PluginProcessor processor;
    const auto settings = processor.getAnalyzerTransport().getSettings().encode();
    // AudioPluginHost probes twice and creates the real editor before its queue can drain.
    for (int creation = 0; creation < 3; ++creation)
    {
        PluginEditor editor (processor);
        CHECK (editor.findChildWithID ("analyzer.panel") == nullptr);
        CHECK (editor.findChildWithID ("analyzer.settings") != nullptr);
        CHECK (processor.getAnalyzerTransport().getSettings().encode() == settings);
    }
}
