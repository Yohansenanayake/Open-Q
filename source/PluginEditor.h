#pragma once

#include "BinaryData.h"
#include "PluginProcessor.h"
// #include "melatonin_inspector/melatonin_inspector.h"
#include "ui/AnalyzerPanel.h"
namespace openq::presets
{
    class PresetBrowser;
}
#include "ui/BandControlCard.h"
#include "ui/BottomBar.h"
#include "ui/HeaderBar.h"
#include "ui/LevelMeter.h"
#include "ui/ResponseGraph.h"

//==============================================================================
class PluginEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PluginEditor (PluginProcessor&);
    ~PluginEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void selectBand (size_t band);
    void positionBandCard (bool resizing = false);
    void positionAnalyzerPanel();
    void positionPresetBrowser();
    void closePresetBrowser();
    struct BoundsConstrainer final : juce::ComponentBoundsConstrainer
    {
        void checkBounds (juce::Rectangle<int>&, const juce::Rectangle<int>&, const juce::Rectangle<int>&, bool, bool, bool, bool) override;
    } boundsConstrainer;
    openq::UiLookAndFeel lookAndFeel;
    PluginProcessor& processor;
    float uiScale = 1.0f;
    void timerCallback() override;
    void refreshMeters();
    openq::AnalyzerTransport& analyzerTransport;
    openq::HeaderBar headerBar;
    openq::BottomBar bottomBar;
    std::unique_ptr<openq::AnalyzerPanel> analyzerPanel;
    std::unique_ptr<openq::presets::PresetBrowser> presetBrowser;
    openq::ResponseGraph responseGraph;
    openq::BandControlCard bandCard;
    openq::LevelMeter inputMeter { "IN" }, outputMeter { "OUT" };
    double lastMeterTime = 0.0;
    std::uint64_t meterGeneration = 0;
    bool knobDragging = false;
    // std::unique_ptr<melatonin::Inspector> inspector;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};
