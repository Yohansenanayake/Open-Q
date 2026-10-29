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

TEST_CASE ("Editor bars scale independently and analyzer opens above its footer button", "[resize][ui][layout]")
{
    PluginProcessor processor;
    processor.prepareToPlay (48000.0, 64);
    PluginEditor editor (processor);
    editor.setVisible (true);
    auto* header = editor.findChildWithID ("editor.header");
    auto* bottom = editor.findChildWithID ("editor.bottom");
    auto* graph = editor.findChildWithID ("response.graph");
    REQUIRE (header != nullptr);
    REQUIRE (bottom != nullptr);
    REQUIRE (graph != nullptr);
    auto* analyzer = dynamic_cast<juce::TextButton*> (bottom->findChildWithID ("analyzer.settings"));
    auto* pre = dynamic_cast<juce::ToggleButton*> (header->findChildWithID ("analyzer.pre"));
    REQUIRE (analyzer != nullptr);
    REQUIRE (pre != nullptr);
    CHECK (editor.findChildWithID ("analyzer.panel") == nullptr);
    const auto baseHeaderHeight = header->getHeight();
    const auto baseBottomHeight = bottom->getHeight();
    const auto baseFont = analyzer->getLookAndFeel().getTextButtonFont (*analyzer, analyzer->getHeight()).getHeight();
    analyzer->setToggleState (true, juce::dontSendNotification);
    analyzer->onClick();
    auto* panel = editor.findChildWithID ("analyzer.panel");
    REQUIRE (panel != nullptr);
    const auto checkLayout = [&] {
        CHECK (graph->getBounds() == editor.getLocalBounds());
        CHECK (header->getX() == 0);
        CHECK (header->getY() == 0);
        CHECK (header->getWidth() == editor.getWidth());
        CHECK (bottom->getX() == 0);
        CHECK (bottom->getRight() == editor.getWidth());
        CHECK (bottom->getBottom() == editor.getHeight());
        CHECK (editor.getIndexOfChildComponent (header) > editor.getIndexOfChildComponent (graph));
        CHECK (editor.getIndexOfChildComponent (bottom) > editor.getIndexOfChildComponent (graph));
        CHECK (bottom->hitTest (bottom->getWidth() / 2, 1));
        CHECK (bottom->hitTest (bottom->getWidth() / 2, bottom->getHeight() - 2));
        CHECK (bottom->hitTest (2, 2));
        auto* throughFooter = editor.getComponentAt (bottom->getWidth() / 2, bottom->getY() + 1);
        REQUIRE (throughFooter != nullptr);
        CHECK ((throughFooter == bottom || bottom->isParentOf (throughFooter)));
        auto* throughHeader = editor.getComponentAt (header->getWidth() / 2, 1);
        REQUIRE (throughHeader != nullptr);
        CHECK ((throughHeader == graph || graph->isParentOf (throughHeader)));
        CHECK (editor.getLocalBounds().contains (panel->getBounds()));
        CHECK (panel->getBottom() <= bottom->getY());
        CHECK_FALSE (header->getBounds().intersects (panel->getBounds()));
        for (auto* bar : { header, bottom })
            for (auto* child : bar->getChildren())
                CHECK (bar->getLocalBounds().contains (child->getBounds()));
    };
    checkLayout();
    editor.setSize (openq::EditorSize::maximumWidth, openq::EditorSize::minimumHeight);
    checkLayout();
    const auto headerGrowth = static_cast<float> (header->getHeight()) / static_cast<float> (baseHeaderHeight);
    const auto bottomGrowth = static_cast<float> (bottom->getHeight()) / static_cast<float> (baseBottomHeight);
    CHECK (bottomGrowth > 1.0f);
    CHECK (bottomGrowth < headerGrowth);
    CHECK (headerGrowth < processor.getEditorSize().scale());
    const auto enlargedFont = analyzer->getLookAndFeel().getTextButtonFont (*analyzer, analyzer->getHeight()).getHeight();
    CHECK (enlargedFont > baseFont);
    CHECK (enlargedFont < baseFont * headerGrowth);
    const auto headerBounds = header->getBounds();
    const auto bottomHeight = bottom->getHeight();
    const auto graphHeight = graph->getHeight();
    editor.setSize (editor.getWidth(), openq::EditorSize::maximumHeight);
    checkLayout();
    CHECK (header->getBounds() == headerBounds);
    CHECK (bottom->getHeight() == bottomHeight);
    CHECK (graph->getHeight() > graphHeight);
    CHECK (juce::exactlyEqual (analyzer->getLookAndFeel().getTextButtonFont (*analyzer, analyzer->getHeight()).getHeight(), enlargedFont));
    editor.setSize (1440, 900);
    checkLayout();
    juce::MemoryOutputStream image;
    REQUIRE (juce::PNGImageFormat().writeImageToStream (editor.createComponentSnapshot (editor.getLocalBounds()), image));
    auto* artwork = graph->findChildWithID ("response.artwork");
    REQUIRE (artwork != nullptr);
    for (auto* label : artwork->getChildren())
        if (label->isVisible())
        {
            const auto bounds = editor.getLocalArea (label, label->getLocalBounds());
            CHECK_FALSE (bounds.intersects (header->getBounds()));
            CHECK_FALSE (bounds.intersects (bottom->getBounds()));
        }
    REQUIRE (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("openq-editor-bars.png").replaceWithData (image.getData(), image.getDataSize()));
}
