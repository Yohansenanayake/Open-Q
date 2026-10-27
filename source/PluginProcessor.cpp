#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <locale>
#include <sstream>

namespace
{
    constexpr int stateSchemaVersion = openq::stateSchemaVersion;
    constexpr double tailSecondsPerBand = 5.0;
    constexpr auto bellDesignRevision = "zle-ivantsov-v1";
    constexpr auto shelfDesignRevision = "zle-ivantsov-shelf-v1";
    constexpr auto cutDesignRevision = "zle-ivantsov-cut-v1";
}

//==============================================================================
PluginProcessor::PluginProcessor()
    : AudioProcessor (BusesProperties()
#if !JucePlugin_IsMidiEffect
    #if !JucePlugin_IsSynth
              .withInput ("Input", juce::AudioChannelSet::stereo(), true)
    #endif
              .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
#endif
              ),
      parameters (*this, nullptr, "OpenQState", createParameterLayout())
{
    inputGainParameter = parameters.getRawParameterValue (openq::gainParameterIds[0]);
    outputGainParameter = parameters.getRawParameterValue (openq::gainParameterIds[1]);
    parameters.state.setProperty ("schemaVersion", stateSchemaVersion, nullptr);
    parameters.state.setProperty ("bandCount", static_cast<int> (openq::bandCount), nullptr);
    for (size_t i = 0; i < openq::bandCount; ++i)
        bandParameters[i] = openq::getBandParameterPointers (parameters, i);
    parameters.state.setProperty ("bellDesign", bellDesignRevision, nullptr);
    parameters.state.setProperty ("shelfDesign", shelfDesignRevision, nullptr);
    parameters.state.setProperty ("cutDesign", cutDesignRevision, nullptr);
}

PluginProcessor::~PluginProcessor()
{
}

//==============================================================================
const juce::String PluginProcessor::getName() const
{
    return JucePlugin_Name;
}

bool PluginProcessor::acceptsMidi() const
{
#if JucePlugin_WantsMidiInput
    return true;
#else
    return false;
#endif
}

bool PluginProcessor::producesMidi() const
{
#if JucePlugin_ProducesMidiOutput
    return true;
#else
    return false;
#endif
}

bool PluginProcessor::isMidiEffect() const
{
#if JucePlugin_IsMidiEffect
    return true;
#else
    return false;
#endif
}

double PluginProcessor::getTailLengthSeconds() const
{
    // Cascaded low-frequency, high-Q boosts can ring far longer than a single band.
    return tailSecondsPerBand * static_cast<double> (openq::bandCount);
}

int PluginProcessor::getNumPrograms()
{
    return 1; // NB: some hosts don't cope very well if you tell them there are 0 programs,
    // so this should be at least 1, even if you're not really implementing programs.
}

int PluginProcessor::getCurrentProgram()
{
    return 0;
}

void PluginProcessor::setCurrentProgram (int index)
{
    juce::ignoreUnused (index);
}

const juce::String PluginProcessor::getProgramName (int index)
{
    juce::ignoreUnused (index);
    // Steinberg's VST3 validator fails plugins whose single default program has no name
    return "Default";
}

void PluginProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused (index, newName);
}

//==============================================================================
void PluginProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);
    for (size_t i = 0; i < bands.size(); ++i)
    {
        const auto target = bandParameters[i].load();
        bands[i].prepare (sampleRate, target.frequency, target.gain, target.q, target.status, target.type, target.slope);
    }
    inputGain.prepare (sampleRate, inputGainParameter->load (std::memory_order_relaxed));
    outputGain.prepare (sampleRate, outputGainParameter->load (std::memory_order_relaxed));
    levelMeters.prepare (getTotalNumInputChannels());
    responseSampleRate.store (sampleRate, std::memory_order_relaxed);
    analyzer.prepare (sampleRate);
}

void PluginProcessor::releaseResources()
{
    for (auto& band : bands)
        band.reset();
}

bool PluginProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
#if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
#else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
    #if !JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
    #endif

    return true;
#endif
}

void PluginProcessor::processBlock (juce::AudioBuffer<float>& buffer,
    juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused (midiMessages);
    juce::ScopedNoDenormals noDenormals;
    const auto inputs = std::min (getTotalNumInputChannels(), buffer.getNumChannels());
    for (int channel = inputs; channel < buffer.getNumChannels(); ++channel)
        buffer.clear (channel, 0, buffer.getNumSamples());

    inputGain.setGainDb (inputGainParameter->load (std::memory_order_relaxed));
    outputGain.setGainDb (outputGainParameter->load (std::memory_order_relaxed));
    const auto inputPeaks = inputGain.process (buffer, inputs);
    analyzer.beginBlock (buffer, inputs);
    for (size_t i = 0; i < bands.size(); ++i)
    {
        const auto target = bandParameters[i].load();
        bands[i].setParameters (target.frequency, target.gain, target.q, target.status, target.type, target.slope);
        bands[i].process (buffer, inputs);
    }
    const auto outputPeaks = outputGain.process (buffer, inputs);
    levelMeters.publish (inputPeaks, outputPeaks);
    // Keep these output taps after the eventual global warm-bypass output mix.
    analyzer.endBlock (buffer);
}

//==============================================================================
bool PluginProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor (*this);
}

//==============================================================================
void PluginProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    const auto analyzerSettings = analyzer.getSettings();
    state.setProperty ("analyzerSettings", static_cast<int> (analyzerSettings.flags()), nullptr);
    state.setProperty ("analyzerConfiguration", static_cast<int> (analyzerSettings.configuration()), nullptr);
    state.setProperty ("analyzerFill", analyzer.getFillEnabled() ? 1 : 0, nullptr);
    const auto size = getEditorSize();
    state.setProperty ("editorWidth", size.width, nullptr);
    state.setProperty ("editorHeight", size.height, nullptr);
    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void PluginProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || !xml->hasTagName ("OpenQState"))
        return;
    const auto state = juce::ValueTree::fromXml (*xml);
    if (state.getProperty ("schemaVersion").toString() != juce::String (stateSchemaVersion)
        || state.getProperty ("bandCount").toString() != juce::String (static_cast<int> (openq::bandCount))
        || state.getProperty ("bellDesign").toString() != bellDesignRevision
        || state.getProperty ("shelfDesign").toString() != shelfDesignRevision
        || state.getProperty ("cutDesign").toString() != cutDesignRevision
        || state.getNumChildren() != static_cast<int> (openq::parameterCount))
        return;

    for (auto* parameter : getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter);
        if (ranged == nullptr)
            return;
        const auto child = state.getChildWithProperty ("id", ranged->getParameterID());
        if (!child.isValid() || !child.hasType ("PARAM"))
            return;
        const auto text = child.getProperty ("value").toString().trim().toStdString();
        double value = 0.0;
        // Xcode 15's libc++ does not provide floating-point std::from_chars.
        std::istringstream input (text);
        input.imbue (std::locale::classic());
        input >> std::noskipws >> value;
        const auto range = ranged->getNormalisableRange();
        if (input.fail() || !input.eof() || !std::isfinite (value) || value < range.start || value > range.end
            || (dynamic_cast<juce::AudioParameterChoice*> (parameter) != nullptr && !juce::exactlyEqual (value, std::round (value))))
            return;
    }
    // Optional display preferences belong to schema 6 and are not host automation parameters.
    double settings = 3.0;
    std::istringstream settingsInput (state.getProperty ("analyzerSettings", 3).toString().trim().toStdString());
    settingsInput.imbue (std::locale::classic());
    settingsInput >> std::noskipws >> settings;
    if (settingsInput.fail() || !settingsInput.eof() || !std::isfinite (settings)
        || settings < 0.0 || settings > 7.0 || !juce::exactlyEqual (settings, std::round (settings)))
        return;
    double configuration = openq::AnalyzerSettings {}.configuration();
    std::istringstream configurationInput (state.getProperty ("analyzerConfiguration", static_cast<int> (configuration)).toString().trim().toStdString());
    configurationInput.imbue (std::locale::classic());
    configurationInput >> std::noskipws >> configuration;
    if (configurationInput.fail() || !configurationInput.eof() || !std::isfinite (configuration)
        || configuration < 0.0 || configuration > openq::AnalyzerSettings::configurationMask || !juce::exactlyEqual (configuration, std::round (configuration)))
        return;
    const auto analyzerSettings = openq::AnalyzerSettings::decode ((static_cast<unsigned> (configuration) << 3) | static_cast<unsigned> (settings));
    if (!analyzerSettings.isValid())
        return;
    const auto fill = state.getProperty ("analyzerFill", 1).toString().trim();
    if (fill != "0" && fill != "1")
        return;
    openq::EditorSize size;
    if (state.hasProperty ("editorWidth") != state.hasProperty ("editorHeight"))
        return;
    if (state.hasProperty ("editorWidth"))
    {
        auto readDimension = [&] (const char* name, int& result) {
            std::istringstream input (state.getProperty (name).toString().trim().toStdString());
            input.imbue (std::locale::classic());
            input >> std::noskipws >> result;
            return !input.fail() && input.eof();
        };
        if (!readDimension ("editorWidth", size.width) || !readDimension ("editorHeight", size.height) || !size.isValid())
            return;
    }
    parameters.replaceState (state);
    analyzer.setSettings (analyzerSettings);
    analyzer.setFillEnabled (fill == "1");
    setEditorSize (size);
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PluginProcessor();
}

juce::AudioProcessorValueTreeState::ParameterLayout PluginProcessor::createParameterLayout()
{
    juce::NormalisableRange<float> frequencyRange (static_cast<float> (openq::bandLimits::minimumFrequencyHz), static_cast<float> (openq::bandLimits::maximumFrequencyHz));
    frequencyRange.setSkewForCentre (static_cast<float> (openq::bandLimits::frequencySkewCentreHz));
    juce::NormalisableRange<float> qRange (static_cast<float> (openq::bandLimits::minimumQ), static_cast<float> (openq::bandLimits::maximumQ));
    qRange.setSkewForCentre (static_cast<float> (openq::bandLimits::qSkewCentre));
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (size_t band = 0; band < openq::bandCount; ++band)
    {
        const auto prefix = "Band " + juce::String (band + 1) + " ";
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { openq::bandParameterId (band, "frequency"), 1 }, prefix + "Frequency", frequencyRange, static_cast<float> (openq::bandLimits::defaultFrequencyHz)));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { openq::bandParameterId (band, "gain"), 1 }, prefix + "Gain", juce::NormalisableRange<float> (static_cast<float> (openq::bandLimits::minimumGainDb), static_cast<float> (openq::bandLimits::maximumGainDb)), static_cast<float> (openq::bandLimits::defaultGainDb)));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { openq::bandParameterId (band, "q"), 1 }, prefix + "Q", qRange, static_cast<float> (openq::bandLimits::defaultQ)));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { openq::bandParameterId (band, "status"), 1 }, prefix + "Status", juce::StringArray { "Off", "Bypass", "On" }, static_cast<int> (band == 0 ? openq::BandStatus::On : openq::BandStatus::Off)));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { openq::bandParameterId (band, "type"), 1 }, prefix + "Type", juce::StringArray { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut" }, static_cast<int> (openq::FilterType::Bell)));
    }
    // Append new parameters so existing band parameter indices remain stable.
    juce::StringArray slopeNames;
    for (const auto* name : openq::cutSlopeNames)
        slopeNames.add (name);
    for (size_t band = 0; band < openq::bandCount; ++band)
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { openq::bandParameterId (band, "slope"), 1 }, "Band " + juce::String (band + 1) + " Slope", slopeNames, static_cast<int> (openq::defaultCutSlope)));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { openq::gainParameterIds[0], 1 }, "Input Gain", openq::gainLimits::minimumDb, openq::gainLimits::maximumDb, openq::gainLimits::defaultDb));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { openq::gainParameterIds[1], 1 }, "Output Gain", openq::gainLimits::minimumDb, openq::gainLimits::maximumDb, openq::gainLimits::defaultDb));
    return layout;
}
