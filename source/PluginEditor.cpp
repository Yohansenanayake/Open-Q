#include "PluginEditor.h"

PluginEditor::PluginEditor (PluginProcessor& p)
    : AudioProcessorEditor (&p), responseGraph (p.parameters, p.getResponseSampleRate()), frequencyAttachment (p.parameters, "bell.frequency", controls[0]), gainAttachment (p.parameters, "bell.gain", controls[1]), qAttachment (p.parameters, "bell.q", controls[2])
{
    addAndMakeVisible (responseGraph);
    const std::array<juce::String, 3> names { "Frequency", "Gain", "Q" };
    for (size_t i = 0; i < controls.size(); ++i)
    {
        auto& control = controls[i];
        control.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        control.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 110, 26);
        control.setTitle (names[i]);
        control.textFromValueFunction = nullptr;
        control.setNumDecimalPlacesToDisplay (i == 0 ? 1 : 2);
        control.setDoubleClickReturnValue (true, i == 0 ? 1000.0 : i == 1 ? 0.0
                                                                          : 1.0);
        addAndMakeVisible (control);
        labels[i].setText (names[i], juce::dontSendNotification);
        labels[i].setJustificationType (juce::Justification::centred);
        addAndMakeVisible (labels[i]);
    }
    controls[0].setTextValueSuffix (" Hz");
    controls[0].setTooltip ("Bell center frequency. Limited below Nyquist at low sample rates.");
    controls[1].setTextValueSuffix (" dB");
    controls[1].setTooltip ("Boost or cut. Zero dB leaves the signal unchanged.");
    controls[2].setTooltip ("Bandwidth: lower Q is wider, higher Q is narrower.");

    addAndMakeVisible (inspectButton);

    // this chunk of code instantiates and opens the melatonin inspector
    inspectButton.onClick = [&] {
        if (!inspector)
        {
            inspector = std::make_unique<melatonin::Inspector> (*this);
            inspector->onClose = [this]() { inspector.reset(); };
        }

        inspector->setVisible (true);
    };

    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.
    setSize (720, 540);
}

PluginEditor::~PluginEditor()
{
}

void PluginEditor::paint (juce::Graphics& g)
{
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    g.setColour (juce::Colours::white);
    g.setFont (24.0f);
    g.drawText ("Open-Q", getLocalBounds().removeFromTop (50), juce::Justification::centred, false);
}

void PluginEditor::resized()
{
    auto area = getLocalBounds().reduced (16);
    area.removeFromTop (40);
    responseGraph.setBounds (area.removeFromTop (280));
    area.removeFromTop (12);
    inspectButton.setBounds (area.removeFromBottom (32).removeFromLeft (130));
    const auto width = area.getWidth() / 3;
    for (size_t i = 0; i < controls.size(); ++i)
    {
        auto column = area.removeFromLeft (width).reduced (6);
        labels[i].setBounds (column.removeFromTop (24));
        controls[i].setBounds (column);
    }
}
