// SPDX-License-Identifier: AGPL-3.0-only
#include "PluginEditor.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE ("Editor dimensions persist across reopening and state recall without new audio parameters", "[resize][state][ui]")
{
    PluginProcessor processor;
    CHECK (processor.getEditorSize() == openq::EditorSize {});
    const auto parameterCount = processor.getParameters().size();
    {
        PluginEditor editor (processor);
        editor.setSize (1013, 900);
    }
    {
        PluginEditor reopened (processor);
        CHECK (reopened.getWidth() == 1013);
        CHECK (reopened.getHeight() == 900);
    }
    juce::MemoryBlock saved;
    processor.getStateInformation (saved);
    PluginProcessor restored;
    restored.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    PluginEditor editor (restored);
    CHECK (editor.getWidth() == 1013);
    CHECK (editor.getHeight() == 900);
    CHECK (restored.getParameters().size() == parameterCount);
    processor.setEditorSize ({ 1440, 1160 });
    processor.getStateInformation (saved);
    restored.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    const auto deadline = juce::Time::getMillisecondCounter() + 1500;
    while (editor.getWidth() != 1440 && juce::Time::getMillisecondCounter() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    CHECK (editor.getWidth() == 1440);
    CHECK (editor.getHeight() == 1160);
}

TEST_CASE ("Invalid editor dimensions reject the complete state and missing dimensions use defaults", "[resize][state]")
{
    PluginProcessor processor;
    processor.setEditorSize ({ 1013, 900 });
    auto load = [&] (juce::ValueTree state) {
        juce::MemoryBlock bytes;
        juce::AudioProcessor::copyXmlToBinary (*state.createXml(), bytes);
        processor.setStateInformation (bytes.getData(), static_cast<int> (bytes.getSize()));
    };
    for (const auto* property : { "editorWidth", "editorHeight" })
        for (const auto* invalid : { "-1", "0", "1", "999999999999", "800.5", "nan", "inf", "900 trailing" })
        {
            auto state = processor.parameters.copyState();
            state.setProperty ("editorWidth", 1013, nullptr);
            state.setProperty ("editorHeight", 900, nullptr);
            state.setProperty (property, invalid, nullptr);
            state.getChildWithProperty ("id", "band.0.gain").setProperty ("value", 12.0, nullptr);
            load (state);
            CHECK (processor.getEditorSize() == openq::EditorSize { 1013, 900 });
            CHECK (juce::exactlyEqual (processor.parameters.getRawParameterValue ("band.0.gain")->load(), 0.0f));
        }
    for (const auto* property : { "editorWidth", "editorHeight" })
    {
        auto partial = processor.parameters.copyState();
        partial.setProperty (property, 1000, nullptr);
        load (partial);
        CHECK (processor.getEditorSize() == openq::EditorSize { 1013, 900 });
    }
    auto shortWide = processor.parameters.copyState();
    shortWide.setProperty ("editorWidth", openq::EditorSize::maximumWidth, nullptr);
    shortWide.setProperty ("editorHeight", openq::EditorSize::minimumHeight, nullptr);
    load (shortWide);
    CHECK (processor.getEditorSize() == openq::EditorSize { 1013, 900 });
    auto legacy = processor.parameters.copyState();
    legacy.removeProperty ("editorWidth", nullptr);
    legacy.removeProperty ("editorHeight", nullptr);
    load (legacy);
    CHECK (processor.getEditorSize() == openq::EditorSize {});
}
