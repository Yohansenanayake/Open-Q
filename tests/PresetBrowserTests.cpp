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
    child<juce::Button> (browser, "preset.default").onClick();
    CHECK (p.getAppliedPresetName() == "Default");
    CHECK_THAT (p.parameters.getRawParameterValue ("band.0.gain")->load(), Catch::Matchers::WithinAbs (0.0, 1e-6));
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
        CHECK (std::abs (entry.getBounds().getCentreX() - header.getWidth() / 2) <= 8);
        for (auto* other : header.getChildren())
            if (other != &entry && other->isVisible())
                CHECK_FALSE (other->getBounds().intersects (entry.getBounds()));
    }
    CHECK (editor.findChildWithID ("preset.browser") == nullptr);
}

TEST_CASE ("Default preset resets sound while preserving comparison and display preferences", "[preset][state]")
{
    PluginProcessor p;
    CHECK (p.getAppliedPresetName() == "Default");
    parameter (p, "band.0.gain", 12.0f);
    parameter (p, "band.1.status", 2.0f);
    parameter (p, "band.1.type", 3.0f);
    parameter (p, "input.gain", 8.0f);
    parameter (p, "output.gain", -4.0f);
    parameter (p, openq::bypassParameterId, 1.0f);
    REQUIRE (p.setAppliedPresetName ("Mastering 1"));
    p.setEditorSize ({ 1440, 900 });
    p.getAnalyzerTransport().setFillEnabled (false);
    const auto options = p.getAnalyzerTransport().getSettings();
    REQUIRE (p.loadDefaultPreset());
    CHECK (p.getAppliedPresetName() == "Default");
    for (auto* original : p.getParameters())
        if (original != p.getBypassParameter())
            CHECK_THAT (original->getValue(), Catch::Matchers::WithinAbs (original->getDefaultValue(), 1e-6));
    CHECK (juce::exactlyEqual (p.getBypassParameter()->getValue(), 1.0f));
    CHECK (p.getEditorSize() == openq::EditorSize { 1440, 900 });
    CHECK (p.getAnalyzerTransport().getSettings().encode() == options.encode());
    CHECK_FALSE (p.getAnalyzerTransport().getFillEnabled());
    parameter (p, "input.gain", 2.0f);
    CHECK (p.getAppliedPresetName() == "Default");
}

TEST_CASE ("Preset Save As and file loads update identity only after success and recall needs no file", "[preset][state][ui]")
{
    juce::MemoryBlock session;
    {
        Library library;
        PluginProcessor p;
        openq::presets::PresetBrowser browser (p, library.directory);
        browser.setSize (browser.getIdealWidth(), browser.getIdealHeight());
        browser.setVisible (true);
        auto& name = child<juce::TextEditor> (browser, "preset.name");
        auto& list = child<openq::presets::PresetList> (browser, "preset.list");
        name.setText ("Mastering 1", false);
        name.onReturnKey();
        CHECK (p.getAppliedPresetName() == "Mastering 1");
        parameter (p, "band.0.gain", 6.0f);
        CHECK (p.getAppliedPresetName() == "Mastering 1");
        name.setText ("Mastering 2", false);
        name.onReturnKey();
        CHECK (p.getAppliedPresetName() == "Mastering 2");
        const auto first = library.directory.getChildFile ("User/Mastering 1.json");
        const auto second = library.directory.getChildFile ("User/Mastering 2.json");
        REQUIRE (first.existsAsFile());
        REQUIRE (second.existsAsFile());
        CHECK_FALSE (second.loadFileAsString().contains ("presetName"));
        list.onPresetSelected (first);
        CHECK (p.getAppliedPresetName() == "Mastering 2");
        REQUIRE (first.replaceWithText ("{broken"));
        const auto unchanged = p.parameters.copyState();
        list.onPresetLoad (first);
        CHECK (p.getAppliedPresetName() == "Mastering 2");
        CHECK (p.parameters.copyState().isEquivalentTo (unchanged));
        auto& warning = child<juce::Component> (browser, "preset.warning");
        child<juce::Button> (warning, "preset.confirm").onClick();
        // A failed Save As must not change the identity either.
        const auto blocked = library.directory.getChildFile ("User/Blocked.json");
        REQUIRE (blocked.createDirectory().wasOk());
        REQUIRE (blocked.getChildFile ("Keep.txt").replaceWithText ("keep"));
        name.setText ("Blocked", false);
        name.onReturnKey();
        CHECK (p.getAppliedPresetName() == "Mastering 2");
        CHECK (blocked.getChildFile ("Keep.txt").existsAsFile());
        child<juce::Button> (warning, "preset.confirm").onClick();
        // External renaming is observed only when that file is explicitly loaded.
        const auto renamed = library.directory.getChildFile ("User/Renamed.json");
        REQUIRE (second.moveFileTo (renamed));
        CHECK (p.getAppliedPresetName() == "Mastering 2");
        browser.refresh();
        list.onPresetLoad (renamed);
        CHECK (p.getAppliedPresetName() == "Renamed");
        browser.setVisible (false);
        CHECK (p.getAppliedPresetName() == "Renamed");
        p.getStateInformation (session);
    }
    PluginProcessor restored;
    REQUIRE (restored.restoreStateInformation (session.getData(), static_cast<int> (session.getSize())));
    CHECK (restored.getAppliedPresetName() == "Renamed");
    for (int reopening = 0; reopening < 2; ++reopening)
    {
        PluginEditor editor (restored);
        auto& header = child<juce::Component> (editor, "editor.header");
        CHECK (child<juce::Button> (header, "preset.open").getButtonText() == "Renamed");
    }
}

TEST_CASE ("Preset name metadata is optional validated and cannot partially restore invalid state", "[preset][state]")
{
    PluginProcessor p;
    REQUIRE (p.setAppliedPresetName ("Session Name"));
    auto saved = state (p);
    const auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), static_cast<int> (saved.getSize()));
    REQUIRE (xml != nullptr);
    const auto original = juce::ValueTree::fromXml (*xml);
    CHECK (original.getProperty ("presetName").toString() == "Session Name");
    const auto unchanged = p.parameters.copyState();
    for (const auto& invalid : std::array<juce::var, 5> { juce::var ("bad\tname"), juce::var (""), juce::var (" padded "), juce::var ("bad\nname"), juce::var (juce::String::repeatedString ("x", 257)) })
    {
        auto broken = original.createCopy();
        broken.setProperty ("presetName", invalid, nullptr);
        broken.getChildWithProperty ("id", "band.0.gain").setProperty ("value", 9.0f, nullptr);
        juce::MemoryBlock bytes;
        juce::AudioProcessor::copyXmlToBinary (*broken.createXml(), bytes);
        CHECK_FALSE (p.restoreStateInformation (bytes.getData(), static_cast<int> (bytes.getSize())));
        CHECK (p.getAppliedPresetName() == "Session Name");
        CHECK (p.parameters.copyState().isEquivalentTo (unchanged));
    }
    REQUIRE (p.setAppliedPresetName ("12"));
    auto numeric = state (p);
    REQUIRE (p.restoreStateInformation (numeric.getData(), static_cast<int> (numeric.getSize())));
    CHECK (p.getAppliedPresetName() == "12");
    auto unnamed = original.createCopy();
    unnamed.removeProperty ("presetName", nullptr);
    juce::AudioProcessor::copyXmlToBinary (*unnamed.createXml(), saved);
    REQUIRE (p.restoreStateInformation (saved.getData(), static_cast<int> (saved.getSize())));
    CHECK (p.getAppliedPresetName() == "Default");
}
