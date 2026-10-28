// Copyright (C) 2026 - zsliu98
// This file is part of ZLEqualizer
//
// ZLEqualizer is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// ZLEqualizer is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with ZLEqualizer. If not, see <https://www.gnu.org/licenses/>.
// Open-Q adaptation: Yohan Senanayake, 2026-10-28; pinned origin in THIRD_PARTY_NOTICES.md.

#include "preset_json.hpp"

#include "../../PluginProcessor.h"

namespace openq::presets
{
    namespace
    {
        constexpr auto kPresetFormat = "OpenQPreset";
        constexpr juce::int64 maximumPresetFileBytes = 1024 * 1024;
        constexpr int maximumStateTreeDepth = 8;

        juce::Result validateProcessorState (const juce::ValueTree& state)
        {
            if (!state.isValid() || !state.hasType ("OpenQState"))
                return juce::Result::fail ("Preset does not contain Open-Q state");
            return juce::Result::ok();
        }
    }

    juce::Result PresetJson::write (const juce::File& file, const juce::MemoryBlock& processor_state)
    {
        if (processor_state.isEmpty())
        {
            return juce::Result::fail ("Processor state is empty");
        }

        const auto xml = juce::AudioProcessor::getXmlFromBinary (processor_state.getData(),
            static_cast<int> (processor_state.getSize()));
        if (xml == nullptr || !xml->hasTagName ("OpenQState"))
        {
            return juce::Result::fail ("Processor state is invalid");
        }

        auto state = juce::ValueTree::fromXml (*xml);
        if (state.getNumChildren() != static_cast<int> (parameterCount) || !state.getChildWithProperty ("id", bypassParameterId).isValid())
            return juce::Result::fail ("Processor state is incomplete");
        state.removeChild (state.getChildWithProperty ("id", bypassParameterId), nullptr);
        for (const auto* name : { "analyzerSettings", "analyzerConfiguration", "analyzerFill", "editorWidth", "editorHeight" })
            state.removeProperty (name, nullptr);
        if (const auto result = validateProcessorState (state); result.failed())
        {
            return juce::Result::fail ("Processor state is incomplete");
        }

        auto* document = new juce::DynamicObject();
        document->setProperty ("format", kPresetFormat);
        document->setProperty ("version", 1);
        document->setProperty ("state", valueTreeToJson (state));

        juce::TemporaryFile temporary (file);
        if (!temporary.getFile().replaceWithText (juce::JSON::toString (juce::var { document }, false)) || !temporary.overwriteTargetFileWithTemporary())
        {
            return juce::Result::fail ("Preset file could not be written");
        }
        return juce::Result::ok();
    }

    juce::Result PresetJson::read (const juce::File& file, juce::MemoryBlock& processor_state)
    {
        if (!file.existsAsFile() || file.getSize() > maximumPresetFileBytes)
            return juce::Result::fail ("Preset file is missing or too large");
        juce::var document;
        if (const auto result = juce::JSON::parse (file.loadFileAsString(), document); result.failed())
        {
            return juce::Result::fail ("Preset is not valid JSON");
        }

        const auto* object = document.getDynamicObject();
        if (object == nullptr || object->getProperty ("format").toString() != kPresetFormat || object->getProperty ("version").toString() != "1")
        {
            return juce::Result::fail ("Preset format is not supported");
        }

        juce::ValueTree state;
        if (const auto result = jsonToValueTree (object->getProperty ("state"), state); result.failed())
        {
            return result;
        }
        if (const auto result = validateProcessorState (state); result.failed())
        {
            return result;
        }

        const auto xml = state.createXml();
        if (xml == nullptr)
        {
            return juce::Result::fail ("Preset state could not be restored");
        }
        processor_state.reset();
        juce::AudioProcessor::copyXmlToBinary (*xml, processor_state);
        return processor_state.isEmpty() ? juce::Result::fail ("Preset state is empty")
                                         : juce::Result::ok();
    }

    bool PresetJson::apply (PluginProcessor& processor, const juce::MemoryBlock& data)
    {
        const auto xml = juce::AudioProcessor::getXmlFromBinary (data.getData(), static_cast<int> (data.getSize()));
        if (xml == nullptr)
            return false;
        auto state = juce::ValueTree::fromXml (*xml);
        if (validateProcessorState (state).failed() || state.getNumChildren() != static_cast<int> (parameterCount - 1)
            || state.getChildWithProperty ("id", bypassParameterId).isValid())
            return false;
        juce::MemoryBlock current;
        processor.getStateInformation (current);
        const auto currentXml = juce::AudioProcessor::getXmlFromBinary (current.getData(), static_cast<int> (current.getSize()));
        if (currentXml == nullptr)
            return false;
        const auto preferences = juce::ValueTree::fromXml (*currentXml);
        state.addChild (preferences.getChildWithProperty ("id", bypassParameterId).createCopy(), -1, nullptr);
        for (const auto* name : { "analyzerSettings", "analyzerConfiguration", "analyzerFill", "editorWidth", "editorHeight" })
            state.setProperty (name, preferences.getProperty (name), nullptr);
        juce::MemoryBlock merged;
        juce::AudioProcessor::copyXmlToBinary (*state.createXml(), merged);
        return processor.restoreStateInformation (merged.getData(), static_cast<int> (merged.getSize()));
    }

    juce::var PresetJson::valueTreeToJson (const juce::ValueTree& tree)
    {
        auto* object = new juce::DynamicObject();
        object->setProperty ("type", tree.getType().toString());

        auto* properties = new juce::DynamicObject();
        for (auto index = 0; index < tree.getNumProperties(); ++index)
        {
            const auto name = tree.getPropertyName (index);
            properties->setProperty (name, tree.getProperty (name));
        }
        object->setProperty ("properties", juce::var { properties });

        juce::Array<juce::var> children;
        children.ensureStorageAllocated (tree.getNumChildren());
        for (const auto& child : tree)
        {
            children.add (valueTreeToJson (child));
        }
        object->setProperty ("children", juce::var { std::move (children) });
        return juce::var { object };
    }

    juce::Result PresetJson::jsonToValueTree (const juce::var& value, juce::ValueTree& tree, int depth)
    {
        if (depth > maximumStateTreeDepth)
            return juce::Result::fail ("Preset tree is too deep");
        const auto* object = value.getDynamicObject();
        if (object == nullptr)
        {
            return juce::Result::fail ("Preset contains an invalid state node");
        }

        const auto type = object->getProperty ("type").toString();
        if (!juce::Identifier::isValidIdentifier (type))
        {
            return juce::Result::fail ("Preset state node has no type");
        }
        tree = juce::ValueTree { juce::Identifier { type } };

        const auto properties_value = object->getProperty ("properties");
        const auto* properties_object = properties_value.getDynamicObject();
        if (properties_object == nullptr)
        {
            return juce::Result::fail ("Preset state node has invalid properties");
        }
        const auto& properties = properties_object->getProperties();
        for (auto index = 0; index < properties.size(); ++index)
        {
            tree.setProperty (properties.getName (index), properties.getValueAt (index), nullptr);
        }

        const auto children_value = object->getProperty ("children");
        const auto* children = children_value.getArray();
        if (children == nullptr)
        {
            return juce::Result::fail ("Preset state node has invalid children");
        }
        for (const auto& child_value : *children)
        {
            juce::ValueTree child;
            if (const auto result = jsonToValueTree (child_value, child, depth + 1); result.failed())
            {
                return result;
            }
            tree.appendChild (child, nullptr);
        }
        return juce::Result::ok();
    }
}
