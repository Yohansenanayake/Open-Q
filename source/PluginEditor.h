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
    void positionBandCard (bool resizing = false);
    struct BoundsConstrainer final : juce::ComponentBoundsConstrainer
    {
        void checkBounds (juce::Rectangle<int>&, const juce::Rectangle<int>&, const juce::Rectangle<int>&, bool, bool, bool, bool) override;
    } boundsConstrainer;
    struct LookAndFeel final : juce::LookAndFeel_V4
    {
        float scale = 1.0f;
        juce::Font getTextButtonFont (juce::TextButton&, int) override;
        juce::Font getComboBoxFont (juce::ComboBox&) override;
        juce::Font getPopupMenuFont() override;
        juce::Label* createSliderTextBox (juce::Slider&) override;
        void drawRotarySlider (juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
        void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool, bool) override;
    } lookAndFeel;
    PluginProcessor& processor;
    float uiScale = 1.0f;
    void timerCallback() override;
    openq::AnalyzerTransport& analyzerTransport;
    juce::ToggleButton preTrace { "Pre" }, postTrace { "Post" }, spectrumFill { "Fill" };
    juce::ComboBox analyzerQuality;
    openq::ResponseGraph responseGraph;
    openq::BandControlCard bandCard;
    bool knobDragging = false;
    std::unique_ptr<melatonin::Inspector> inspector;
    juce::TextButton inspectButton { "Inspect the UI" };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};
