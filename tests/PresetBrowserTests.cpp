// SPDX-License-Identifier: AGPL-3.0-only
#include "PluginEditor.h"
#include "ui/presets/preset_browser.hpp"
#include "ui/presets/preset_json.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

namespace
{
    struct Library
    {
        juce::File directory = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("openq-preset-test", "", false);
        Library() { REQUIRE (directory.createDirectory().wasOk()); }
        ~Library() { (void) directory.deleteRecursively(); }
    };
    void parameter (PluginProcessor& p, const char* id, float value)
    {
        auto* target = p.parameters.getParameter (id);
        REQUIRE (target != nullptr);
        target->setValueNotifyingHost (target->convertTo0to1 (value));
    }
    juce::MemoryBlock state (PluginProcessor& p)
    {
        juce::MemoryBlock bytes;
        p.getStateInformation (bytes);
        return bytes;
    }
    template <typename T>
    T& child (juce::Component& parent, const char* id)
    {
        auto* result = dynamic_cast<T*> (parent.findChildWithID (id));
        REQUIRE (result != nullptr);
        return *result;
    }
    juce::MouseEvent mouse (juce::Component& component)
    {
        const auto time = juce::Time::getCurrentTime();
        return { juce::Desktop::getInstance().getMainMouseSource(), { 10.0f, 10.0f }, {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &component, &component, time, {}, time, 2, false };
    }
}

TEST_CASE ("Sound presets round trip bands and trims while preserving bypass and display preferences", "[preset][state]")
{
    Library library;
    PluginProcessor source, target;
    parameter (source, "band.0.gain", 9.0f);
    parameter (source, "band.0.type", 2.0f);
    parameter (source, "input.gain", -4.5f);
    parameter (source, "output.gain", 2.25f);
    const auto file = library.directory.getChildFile ("Test.json");
    REQUIRE (openq::presets::PresetJson::write (file, state (source)).wasOk());
    CHECK_FALSE (file.loadFileAsString().contains ("editorWidth"));
    CHECK_FALSE (file.loadFileAsString().contains (openq::bypassParameterId));
    parameter (target, openq::bypassParameterId, 1.0f);
    target.setEditorSize ({ 1440, 900 });
    auto options = target.getAnalyzerTransport().getSettings();
    options.pre = false;
    target.getAnalyzerTransport().setSettings (options);
    target.getAnalyzerTransport().setFillEnabled (false);
    juce::MemoryBlock data;
    REQUIRE (openq::presets::PresetJson::read (file, data).wasOk());
    REQUIRE (openq::presets::PresetJson::apply (target, data));
    for (auto* original : source.getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (original);
        REQUIRE (ranged != nullptr);
        if (ranged->getParameterID() != openq::bypassParameterId)
            CHECK_THAT (target.parameters.getParameter (ranged->getParameterID())->getValue(), Catch::Matchers::WithinAbs (original->getValue(), 1e-6));
    }
    CHECK (juce::exactlyEqual (target.getBypassParameter()->getValue(), 1.0f));
    CHECK (target.getEditorSize() == openq::EditorSize { 1440, 900 });
    CHECK_FALSE (target.getAnalyzerTransport().getSettings().pre);
    CHECK_FALSE (target.getAnalyzerTransport().getFillEnabled());
}

TEST_CASE ("Invalid sound presets fail without partially replacing state", "[preset][state]")
{
    Library library;
    PluginProcessor p;
    parameter (p, "band.0.gain", 4.0f);
    const auto unchanged = p.parameters.copyState();
    const auto file = library.directory.getChildFile ("Bad.json");
    REQUIRE (openq::presets::PresetJson::write (file, state (p)).wasOk());
    const auto valid = file.loadFileAsString();
    for (int corruption = 0; corruption < 6; ++corruption)
    {
        auto document = juce::JSON::parse (valid);
        auto* root = document.getDynamicObject();
        REQUIRE (root != nullptr);
        auto tree = root->getProperty ("state");
        auto* properties = tree.getDynamicObject()->getProperty ("properties").getDynamicObject();
        auto* children = tree.getDynamicObject()->getProperty ("children").getArray();
        REQUIRE (properties != nullptr);
        REQUIRE (children != nullptr);
        if (corruption == 0)
            root->setProperty ("format", "OtherPlugin");
        if (corruption == 1)
            properties->setProperty ("schemaVersion", 6);
        if (corruption == 2)
            properties->setProperty ("bellDesign", "unknown");
        if (corruption == 3)
            children->removeLast();
        if (corruption == 5)
            root->setProperty ("version", 1.5);
        if (corruption == 4)
            children->getReference (0).getDynamicObject()->getProperty ("properties").getDynamicObject()->setProperty ("value", "nan");
        REQUIRE (file.replaceWithText (juce::JSON::toString (document)));
        juce::MemoryBlock bytes;
        const auto read = openq::presets::PresetJson::read (file, bytes);
        CHECK_FALSE ((read.wasOk() && openq::presets::PresetJson::apply (p, bytes)));
        CHECK (p.parameters.copyState().isEquivalentTo (unchanged));
    }
    REQUIRE (file.replaceWithText ("{truncated"));
    juce::MemoryBlock bytes;
    CHECK (openq::presets::PresetJson::read (file, bytes).failed());
}

TEST_CASE ("Ported browser saves groups searches loads and confirms overwrite and deletion", "[preset][ui]")
{
    Library library;
    PluginProcessor p;
#if !JUCE_WINDOWS
    REQUIRE (library.directory.createSymbolicLink (library.directory.getChildFile ("Loop"), false));
#endif
    openq::presets::PresetBrowser browser (p, library.directory);
    browser.setSize (browser.getIdealWidth(), browser.getIdealHeight());
    browser.setVisible (true);
    CHECK (browser.isOpaque());
    auto& group = child<juce::TextEditor> (browser, "preset.group.name");
    auto& name = child<juce::TextEditor> (browser, "preset.name");
    auto& search = child<juce::TextEditor> (browser, "preset.search");
    auto& list = child<juce::Component> (browser, "preset.list");
    group.setText ("Bass", false);
    group.onReturnKey();
    parameter (p, "band.0.gain", 6.0f);
    name.setText ("Warm", false);
    name.onReturnKey();
    const auto file = library.directory.getChildFile ("Bass/Warm.json");
    REQUIRE (file.existsAsFile());
    const auto original = file.loadFileAsString();
    parameter (p, "band.0.gain", -9.0f);
    search.setText ("No match");
    juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
    list.mouseDoubleClick (mouse (list));
    CHECK_THAT (p.parameters.getRawParameterValue ("band.0.gain")->load(), Catch::Matchers::WithinAbs (-9.0, 1e-6));
    const auto beforeClose = p.parameters.copyState();
    browser.setVisible (false);
    CHECK (search.getText().isEmpty());
    browser.setVisible (true);
    CHECK (search.getText().isEmpty());
    CHECK (p.parameters.copyState().isEquivalentTo (beforeClose));
    list.mouseDoubleClick (mouse (list));
    CHECK_THAT (p.parameters.getRawParameterValue ("band.0.gain")->load(), Catch::Matchers::WithinAbs (6.0, 1e-6));
    parameter (p, "band.0.gain", -3.0f);
    name.setText ("Warm", false);
    name.onReturnKey();
    auto& warning = child<juce::Component> (browser, "preset.warning");
    REQUIRE (warning.isVisible());
    CHECK (file.loadFileAsString() == original);
    child<juce::Button> (warning, "preset.cancel").onClick();
    CHECK (file.loadFileAsString() == original);
    name.onReturnKey();
    child<juce::Button> (warning, "preset.confirm").onClick();
    CHECK (file.loadFileAsString() != original);
    child<juce::Button> (browser, "preset.delete").onClick();
    REQUIRE (warning.isVisible());
    child<juce::Button> (warning, "preset.confirm").onClick();
    CHECK_FALSE (file.exists());
    child<juce::Button> (browser, "preset.group.delete").onClick();
    REQUIRE (warning.isVisible());
    CHECK (warning.keyPressed (juce::KeyPress (juce::KeyPress::returnKey)));
    CHECK_FALSE (library.directory.getChildFile ("Bass").exists());
    CHECK (library.directory.getChildFile ("User").isDirectory());
    bool closed = false;
    browser.onClose = [&] { closed = true; browser.setVisible (false); };
    child<juce::Button> (browser, "preset.close").onClick();
    CHECK (closed);
    CHECK_FALSE (browser.isVisible());
}

TEST_CASE ("Preset entry is in the header and browser creation stays lazy", "[preset][ui]")
{
    PluginProcessor p;
    PluginEditor editor (p);
    CHECK (editor.findChildWithID ("preset.browser") == nullptr);
    auto& header = child<juce::Component> (editor, "editor.header");
    auto& entry = child<juce::Button> (header, "preset.open");
    for (int width : { openq::EditorSize::minimumWidth, openq::EditorSize::maximumWidth })
    {
        editor.setSize (width, openq::EditorSize::defaultHeight);
        CHECK (header.getLocalBounds().contains (entry.getBounds()));
        for (auto* other : header.getChildren())
            if (other != &entry && other->isVisible())
                CHECK_FALSE (other->getBounds().intersects (entry.getBounds()));
    }
    CHECK (editor.findChildWithID ("preset.browser") == nullptr);
}
