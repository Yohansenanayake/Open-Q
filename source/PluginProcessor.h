#pragma once

#include "EditorSize.h"
#include "PluginParameters.h"
#include "dsp/BandFilter.h"
#include "dsp/GainStage.h"
#include "dsp/LevelMeterTransport.h"
#include "dsp/Oversampling.h"
#include "dsp/ProcessingConfiguration.h"
#include "dsp/analyzer/AnalyzerTransport.h"
#include <juce_audio_processors/juce_audio_processors.h>

class PluginProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    PluginProcessor();
    ~PluginProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    using juce::AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    using juce::AudioProcessor::processBlockBypassed;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorParameter* getBypassParameter() const override;

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
    bool restoreStateInformation (const void* data, int sizeInBytes);

    juce::String getAppliedPresetName() const;
    bool setAppliedPresetName (const juce::String&);
    static bool isValidPresetName (const juce::String&);
    bool loadDefaultPreset();
    juce::AudioProcessorValueTreeState parameters;

    const openq::ProcessingConfiguration& getProcessingConfiguration() const { return processingConfiguration; }
    bool setOversamplingMode (int mode);
    int getOversamplingMode() const { return requestedOversampling.load(); }
    int getActiveOversamplingMode() const { return activeOversampling.load(); }
    bool isOversamplingPending() const { return getOversamplingMode() != getActiveOversamplingMode() || switchReady.load() != 0; }
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
    openq::LevelMeterTransport& getLevelMeters() { return levelMeters; }

private:
    // UI/session metadata only; audio processing never accesses this lock or string.
    mutable juce::CriticalSection presetNameLock;
    juce::String appliedPresetName { "Default" };
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void processAudio (juce::AudioBuffer<float>&, bool forceBypass);
    void timerCallback() override;
    void activateOversampling (int mode);
    void updateOversampling();
    openq::Oversampling oversampling;
    openq::ProcessingConfiguration processingConfiguration;
    double processingSampleRate = 0.0;
    std::atomic<int> requestedOversampling { 0 }, activeOversampling { 0 };
    std::atomic<std::uint64_t> switchReady { 0 }, switchCommit { 0 }, switchGeneration { 0 };
    std::uint64_t switchToken = 0;
    juce::SmoothedValue<double> reconfigurationGain { 1.0 };
    static constexpr int bypassChunkSamples = openq::processingChunkSamples;
    std::array<std::array<float, bypassChunkSamples>, 2> drySamples {};
    juce::SmoothedValue<double, juce::ValueSmoothingTypes::Linear> bypassWet { 1.0 };
    std::atomic<float>* bypassParameter = nullptr;
    std::array<openq::BandParameterPointers, openq::bandCount> bandParameters;
    std::array<openq::BandFilter, openq::bandCount> bands;
    openq::AnalyzerTransport analyzer;
    openq::LevelMeterTransport levelMeters;
    openq::GainStage inputGain, outputGain;
    std::atomic<float>* inputGainParameter = nullptr;
    std::atomic<float>* outputGainParameter = nullptr;
    std::atomic<std::uint64_t> editorSize { (static_cast<std::uint64_t> (openq::EditorSize::defaultWidth) << 32) | openq::EditorSize::defaultHeight };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
};
