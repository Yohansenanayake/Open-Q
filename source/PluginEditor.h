#pragma once

#include "BinaryData.h"
#include "PluginProcessor.h"
#include "melatonin_inspector/melatonin_inspector.h"
#include "ui/AnalyzerPanel.h"
#include "ui/BandControlCard.h"
#include "ui/ResponseGraph.h"
#include "ui/HeaderBar.h"
#include "ui/BottomBar.h"

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

private:
    void selectBand (size_t band);
    void positionBandCard (bool resizing = false);
    void positionAnalyzerPanel();
    struct BoundsConstrainer final : juce::ComponentBoundsConstrainer
    {
        void checkBounds (juce::Rectangle<int>&, const juce::Rectangle<int>&, const juce::Rectangle<int>&, bool, bool, bool, bool) override;
    } boundsConstrainer;
    openq::UiLookAndFeel lookAndFeel;
    PluginProcessor& processor;
    float uiScale = 1.0f;
    void timerCallback() override;
    openq::AnalyzerTransport& analyzerTransport;
    openq::HeaderBar headerBar;
    openq::BottomBar bottomBar;
    std::unique_ptr<openq::AnalyzerPanel> analyzerPanel;
    openq::ResponseGraph responseGraph;
    openq::BandControlCard bandCard;
    bool knobDragging = false;
    std::unique_ptr<melatonin::Inspector> inspector;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};
