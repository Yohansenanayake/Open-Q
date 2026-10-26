#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <locale>
#include <sstream>

namespace
{
    constexpr int stateSchemaVersion = 4;
    constexpr double tailSecondsPerBand = 5.0;
    constexpr auto bellDesignRevision = "zle-ivantsov-v1";
    constexpr auto shelfDesignRevision = "zle-ivantsov-shelf-v1";
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
    parameters.state.setProperty ("schemaVersion", stateSchemaVersion, nullptr);
    parameters.state.setProperty ("bandCount", static_cast<int> (openq::bandCount), nullptr);
    for (size_t i = 0; i < openq::bandCount; ++i)
        bandParameters[i] = openq::getBandParameterPointers (parameters, i);
    parameters.state.setProperty ("bellDesign", bellDesignRevision, nullptr);
    parameters.state.setProperty ("shelfDesign", shelfDesignRevision, nullptr);
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
        bands[i].prepare (sampleRate, target.frequency, target.gain, target.q, target.status, target.type);
    }
    responseSampleRate.store (sampleRate, std::memory_order_relaxed);
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

    for (size_t i = 0; i < bands.size(); ++i)
    {
        const auto target = bandParameters[i].load();
        bands[i].setParameters (target.frequency, target.gain, target.q, target.status, target.type);
        bands[i].process (buffer, inputs);
    }
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
    const auto state = parameters.copyState();
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
    if (static_cast<int> (state.getProperty ("schemaVersion")) != stateSchemaVersion
        || static_cast<int> (state.getProperty ("bandCount")) != static_cast<int> (openq::bandCount)
        || state.getProperty ("bellDesign").toString() != bellDesignRevision
        || state.getProperty ("shelfDesign").toString() != shelfDesignRevision
        || state.getNumChildren() != static_cast<int> (openq::bandCount * openq::bandParameterNames.size()))
        return;

    for (size_t band = 0; band < openq::bandCount; ++band)
    {
        for (const auto* name : openq::bandParameterNames)
        {
            const auto id = openq::bandParameterId (band, name);
            const auto child = state.getChildWithProperty ("id", id);
            if (!child.isValid() || !child.hasType ("PARAM"))
                return;
            const auto text = child.getProperty ("value").toString().trim().toStdString();
            double value = 0.0;
            // Xcode 15's libc++ does not provide floating-point std::from_chars.
            std::istringstream input (text);
            input.imbue (std::locale::classic());
            input >> std::noskipws >> value;
            const auto range = parameters.getParameter (id)->getNormalisableRange();
            if (input.fail() || !input.eof()
                || !std::isfinite (value) || value < range.start || value > range.end
                || ((juce::String (name) == "status" || juce::String (name) == "type") && !juce::exactlyEqual (value, std::round (value))))
                return;
        }
    }
    parameters.replaceState (state);
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
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { openq::bandParameterId (band, "type"), 1 }, prefix + "Type", juce::StringArray { "Bell", "Low Shelf", "High Shelf" }, static_cast<int> (openq::FilterType::Bell)));
    }
    return layout;
}
