// SPDX-License-Identifier: AGPL-3.0-only
#include "AnalyzerPanel.h"
#include "BinaryData.h"
#include "Palette.h"
#include "PanelDrawing.h"

namespace openq
{
    AnalyzerPanel::AnalyzerPanel (AnalyzerTransport& source) : transport (source)
    {
        setComponentID ("analyzer.panel");
        setTitle ("Analyzer settings");
        const std::array<juce::String, 4> names { "Quality", "Smoothing", "Amount", "Decay" };
        const std::array<CompactComboBox*, 4> boxes { &quality, &smoothing, &amount, &decay };
        const std::array<juce::String, 4> ids { "quality", "smoothing", "amount", "decay" };
        for (size_t i = 0; i < labels.size(); ++i)
        {
            labels[i].setText (names[i], juce::dontSendNotification);
            addAndMakeVisible (labels[i]);
            boxes[i]->setComponentID ("analyzer." + ids[i]);
            boxes[i]->setTitle (names[i]);
            addAndMakeVisible (*boxes[i]);
        }
        quality.addItemList ({ "Normal", "High" }, 1);
        smoothing.addItemList ({ "ERB", "Octave" }, 1);
        decay.addItemList ({ "Very Fast", "Fast", "Medium", "Slow", "Very Slow" }, 1);
        quality.setTooltip ("Analyzer resolution only; does not change audio or plugin latency.");
        smoothing.setTooltip ("Average nearby frequencies in linear power using ERB or octave bands.");
        amount.setTooltip ("Smoothing bandwidth. Each mode remembers its own amount.");
        decay.setTooltip ("Controls how quickly spectrum peaks fall. Rising levels respond immediately. Speed follows the analyzer dB range.");
        quality.onChange = [this] {
            auto settings = transport.getSettings();
            settings.quality = quality.getSelectedId() == 2 ? AnalyzerQuality::High : AnalyzerQuality::Normal;
            transport.setSettings (settings);
        };
        smoothing.onChange = [this] {
            auto settings = transport.getSettings();
            settings.smoothing = smoothing.getSelectedId() == 2 ? AnalyzerSmoothing::Octave : AnalyzerSmoothing::Erb;
            transport.setSettings (settings);
            refresh();
        };
        amount.onChange = [this] {
            auto settings = transport.getSettings();
            auto& index = settings.smoothing == AnalyzerSmoothing::Erb ? settings.erbAmount : settings.octaveAmount;
            if (amount.getSelectedId() > 0)
                index = static_cast<unsigned> (amount.getSelectedId() - 1);
            transport.setSettings (settings);
        };
        decay.onChange = [this] {
            auto settings = transport.getSettings();
            settings.decay = static_cast<AnalyzerDecay> (decay.getSelectedItemIndex());
            transport.setSettings (settings);
        };
        close.setComponentID ("analyzer.close");
        close.setTitle ("Close analyzer settings");
        close.setTooltip ("Hide analyzer settings");
        addAndMakeVisible (close);
        close.onClick = [this] { if (onClose) onClose(); };
        refresh();
    }

    void AnalyzerPanel::refresh()
    {
        const auto settings = transport.getSettings();
        // An unchanged model must not overwrite a selection whose asynchronous onChange is still queued.
        if (!shownSettings || shownSettings->quality != settings.quality)
            quality.setSelectedId (settings.quality == AnalyzerQuality::High ? 2 : 1, juce::dontSendNotification);
        const bool modeChanged = !shownSettings || shownSettings->smoothing != settings.smoothing;
        if (modeChanged)
        {
            smoothing.setSelectedId (settings.smoothing == AnalyzerSmoothing::Erb ? 1 : 2, juce::dontSendNotification);
            amount.closePopup();
            amount.clear (juce::dontSendNotification);
            if (settings.smoothing == AnalyzerSmoothing::Erb)
                amount.addItemList ({ "1/4 ERB", "1/2 ERB", "1 ERB" }, 1);
            else
                amount.addItemList ({ "1/24 octave", "1/12 octave", "1/6 octave", "1/3 octave" }, 1);
        }
        if (modeChanged || (settings.smoothing == AnalyzerSmoothing::Erb ? shownSettings->erbAmount != settings.erbAmount : shownSettings->octaveAmount != settings.octaveAmount))
            amount.setSelectedId (static_cast<int> (settings.smoothing == AnalyzerSmoothing::Erb ? settings.erbAmount : settings.octaveAmount) + 1, juce::dontSendNotification);
        if (!shownSettings || shownSettings->decay != settings.decay)
            decay.setSelectedId (static_cast<int> (settings.decay) + 1, juce::dontSendNotification);
        shownSettings = settings;
    }

    void AnalyzerPanel::dismissPopups()
    {
        for (auto* box : { &quality, &smoothing, &amount, &decay })
            box->closePopup();
    }
    bool AnalyzerPanel::isPopupActive() const
    {
        return quality.isPopupActive() || smoothing.isPopupActive() || amount.isPopupActive() || decay.isPopupActive();
    }
    void AnalyzerPanel::visibilityChanged()
    {
        if (!isVisible())
            dismissPopups();
    }
    bool AnalyzerPanel::keyPressed (const juce::KeyPress& key)
    {
        if (key != juce::KeyPress::escapeKey)
            return false;
        if (onClose)
            onClose();
        return true;
    }
    void AnalyzerPanel::paint (juce::Graphics& g)
    {
        const auto scale = static_cast<float> (getWidth()) / preferredWidth;
        drawPanelBackground (g, getLocalBounds().toFloat(), scale);
        g.setColour (palette::text);
        g.setFont (16.0f * scale);
        g.drawText ("Analyzer", juce::Rectangle<float> (12.0f, 8.0f, 180.0f, 28.0f) * scale, juce::Justification::centredLeft);
    }
    void AnalyzerPanel::resized()
    {
        dismissPopups();
        const auto scale = static_cast<float> (getWidth()) / preferredWidth;
        const auto scaled = [scale] (int value) { return juce::roundToInt (static_cast<float> (value) * scale); };
        auto area = getLocalBounds().reduced (scaled (12));
        auto header = area.removeFromTop (scaled (28));
        close.setBounds (header.removeFromRight (header.getHeight()));
        const std::array<CompactComboBox*, 4> boxes { &quality, &smoothing, &amount, &decay };
        for (size_t i = 0; i < labels.size(); ++i)
        {
            area.removeFromTop (scaled (8));
            auto row = area.removeFromTop (scaled (30));
            labels[i].setFont (juce::FontOptions (15.0f * scale));
            labels[i].setBounds (row.removeFromLeft (scaled (100)));
            boxes[i]->setBounds (row);
        }
    }
}
