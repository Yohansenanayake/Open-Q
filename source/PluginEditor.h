#pragma once

#include "BinaryData.h"
#include "PluginProcessor.h"
#include "melatonin_inspector/melatonin_inspector.h"
#include "ui/BandControlCard.h"
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

private:
    void selectBand (size_t band);
    void positionBandCard();
    void timerCallback() override;
    openq::AnalyzerTransport& analyzerTransport;
    juce::ToggleButton preTrace { "Pre" }, postTrace { "Post" };
    juce::ComboBox analyzerQuality;
    openq::ResponseGraph responseGraph;
    openq::BandControlCard bandCard;
    bool knobDragging = false;
    std::unique_ptr<melatonin::Inspector> inspector;
    juce::TextButton inspectButton { "Inspect the UI" };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};
