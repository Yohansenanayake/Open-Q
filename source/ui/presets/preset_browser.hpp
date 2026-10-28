// Copyright (C) 2026 - zsliu98
// This file is part of ZLSpectrumEqualizer
//
// ZLSpectrumEqualizer is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// ZLSpectrumEqualizer is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with ZLSpectrumEqualizer. If not, see <https://www.gnu.org/licenses/>.
// Open-Q adaptation: Yohan Senanayake, 2026-10-28; pinned origin in THIRD_PARTY_NOTICES.md.

#pragma once

#include "PresetStyle.h"

#include <vector>

#include "../../PluginProcessor.h"
#include "../IconButton.h"
#include "../UiLookAndFeel.h"

#include "../PanelDrawing.h"
#include "group_list.hpp"
#include "preset_entry.hpp"
#include "preset_json.hpp"
#include "preset_list.hpp"
#include "rounded_text_editor.hpp"
#include "warning_overlay.hpp"

namespace openq::presets
{
    class PresetBrowser final : public juce::Component
    {
    public:
        explicit PresetBrowser (PluginProcessor& processor, juce::File directory = defaultDirectory());
        static juce::File defaultDirectory();
        void paint (juce::Graphics&) override;
        void setUiScale (float);
        std::function<void()> onClose;

        ~PresetBrowser() override;

        void resized() override;

        void lookAndFeelChanged() override;

        void visibilityChanged() override;

        void refresh();

        void flushPendingScroll();

        int getIdealWidth() const;

        int getIdealHeight() const;

    private:
        PluginProcessor& processor_;
        PresetStyle base_;
        UiLookAndFeel look_and_feel_;
        const juce::File presets_directory_;

        IconButton delete_group_button_;
        IconButton delete_preset_button_;
        IconButton close_button_;
        juce::TextButton folder_open_button_ { juce::String::fromUTF8 ("\xe2\x86\x97") };

        juce::Label group_label_;
        juce::Label preset_label_;
        RoundedTextEditor search_editor_;
        RoundedTextEditor group_name_editor_;
        RoundedTextEditor preset_name_editor_;

        GroupList group_list_;
        PresetList preset_list_;
        WarningOverlay warning_overlay_;

        juce::StringArray groups_;
        std::vector<PresetEntry> all_presets_;
        std::vector<PresetEntry> presets_;
        juce::String selected_group_ { "All Presets" };
        juce::File selected_preset_file_;

        static constexpr auto kAllPresetsGroup = "All Presets";
        static constexpr auto kDefaultGroup = "User";
        static constexpr auto kPresetExtension = ".json";

        bool ensurePresetDirectory();

        void refreshGroups();

        void refreshPresetCache();

        void refreshPresets();

        void createGroup();

        void deleteSelectedGroup();

        void savePreset();

        void writePreset (const juce::File& file);

        void loadPreset (const juce::File& file);

        void deleteSelectedPreset();

        void selectGroup (const juce::String& group);

        void selectPreset (const juce::File& file, bool scroll_to_row = false);

        void configureEditor (RoundedTextEditor& editor, const juce::String& placeholder);

        void applyColours();

        void showError (const juce::String& message);

        void updateDeleteButtonStates();

        const PresetEntry* getSelectedPreset() const;

        juce::String getWritableGroup() const;

        void revealFolder();

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetBrowser)
    };
}
