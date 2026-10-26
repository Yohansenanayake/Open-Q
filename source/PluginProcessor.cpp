#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <locale>
#include <sstream>

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
    parameters.state.setProperty ("schemaVersion", 4, nullptr);
    parameters.state.setProperty ("bandCount", static_cast<int> (openq::bandCount), nullptr);
    for (size_t i = 0; i < openq::bandCount; ++i)
        bandParameters[i] = openq::getBandParameterPointers (parameters, i);
    parameters.state.setProperty ("bellDesign", "zle-ivantsov-v1", nullptr);
    parameters.state.setProperty ("shelfDesign", "zle-ivantsov-shelf-v1", nullptr);
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
    return 5.0 * static_cast<double> (openq::bandCount);
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
    if (static_cast<int> (state.getProperty ("schemaVersion")) != 4
        || static_cast<int> (state.getProperty ("bandCount")) != static_cast<int> (openq::bandCount)
        || state.getProperty ("bellDesign").toString() != "zle-ivantsov-v1"
        || state.getProperty ("shelfDesign").toString() != "zle-ivantsov-shelf-v1"
        || state.getNumChildren() != static_cast<int> (openq::bandCount * 5))
        return;

    for (size_t band = 0; band < openq::bandCount; ++band)
    {
        for (const auto* name : { "frequency", "gain", "q", "status", "type" })
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
    juce::NormalisableRange<float> frequencyRange (20.0f, 20000.0f);
    frequencyRange.setSkewForCentre (1000.0f);
    juce::NormalisableRange<float> qRange (0.1f, 10.0f);
    qRange.setSkewForCentre (1.0f);
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (size_t band = 0; band < openq::bandCount; ++band)
    {
        const auto prefix = "Band " + juce::String (band + 1) + " ";
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { openq::bandParameterId (band, "frequency"), 1 }, prefix + "Frequency", frequencyRange, 1000.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { openq::bandParameterId (band, "gain"), 1 }, prefix + "Gain", juce::NormalisableRange<float> (-24.0f, 24.0f), 0.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { openq::bandParameterId (band, "q"), 1 }, prefix + "Q", qRange, 1.0f));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { openq::bandParameterId (band, "status"), 1 }, prefix + "Status", juce::StringArray { "Off", "Bypass", "On" }, static_cast<int> (band == 0 ? openq::BandStatus::On : openq::BandStatus::Off)));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { openq::bandParameterId (band, "type"), 1 }, prefix + "Type", juce::StringArray { "Bell", "Low Shelf", "High Shelf" }, 0));
    }
    return layout;
}
