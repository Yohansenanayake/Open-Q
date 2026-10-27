#pragma once

#include "BandParameters.h"
#include "EditorSize.h"
#include "dsp/BandFilter.h"
#include "dsp/analyzer/AnalyzerTransport.h"
#include <juce_audio_processors/juce_audio_processors.h>

class PluginProcessor : public juce::AudioProcessor
{
public:
    PluginProcessor();
    ~PluginProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    using juce::AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState parameters;

    const std::atomic<double>& getResponseSampleRate() const { return responseSampleRate; }
    openq::EditorSize getEditorSize() const
    {
        const auto packed = editorSize.load (std::memory_order_relaxed);
        return { static_cast<int> (packed >> 32), static_cast<int> (packed & 0xffffffffu) };
    }
    void setEditorSize (openq::EditorSize size)
    {
        size = size.constrained();
        editorSize.store ((static_cast<std::uint64_t> (size.width) << 32) | static_cast<unsigned> (size.height), std::memory_order_relaxed);
    }
    openq::AnalyzerTransport& getAnalyzerTransport() { return analyzer; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    std::array<openq::BandParameterPointers, openq::bandCount> bandParameters;
    std::array<openq::BandFilter, openq::bandCount> bands;
    std::atomic<double> responseSampleRate { 0.0 };
    openq::AnalyzerTransport analyzer;
    std::atomic<std::uint64_t> editorSize { (static_cast<std::uint64_t> (openq::EditorSize::defaultWidth) << 32) | openq::EditorSize::defaultHeight };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
};
