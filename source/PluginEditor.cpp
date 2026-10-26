#include "PluginEditor.h"

PluginEditor::PluginEditor (PluginProcessor& p)
    : AudioProcessorEditor (&p), processor (p), responseGraph (p.parameters, p.getResponseSampleRate())
{
    addAndMakeVisible (responseGraph);
    responseGraph.onBandSelected = [this] (size_t band) { selectBand (band); };
    responseGraph.onDragStateChanged = [this] (bool dragging) {
        for (auto& button : bandButtons)
            button.setEnabled (!dragging);
        for (auto& control : controls)
            control.setEnabled (!dragging && responseGraph.getSelectedBand() < openq::bandCount);
        statusSelector.setEnabled (!dragging && responseGraph.getSelectedBand() < openq::bandCount);
    };
    for (size_t band = 0; band < bandButtons.size(); ++band)
    {
        auto& button = bandButtons[band];
        button.setButtonText (juce::String (band + 1));
        button.setTitle ("Select band " + juce::String (band + 1));
        button.setComponentID ("select.band." + juce::String (band));
        button.setClickingTogglesState (true);
        button.setRadioGroupId (1);
        button.onClick = [this, band] { selectBand (band); };
        addAndMakeVisible (button);
    }
    statusSelector.setComponentID ("band.status");
    statusSelector.addItemList ({ "Off", "Bypass", "On" }, 1);
    statusSelector.setTextWhenNothingSelected ("No band selected");
    statusSelector.setTooltip ("Off: unused slot. Bypass: keep the band without applying EQ. On: apply EQ. All states retain frequency, gain, and Q.");
    addAndMakeVisible (statusSelector);
    const std::array<juce::String, 3> names { "Frequency", "Gain", "Q" };
    for (size_t i = 0; i < controls.size(); ++i)
    {
        auto& control = controls[i];
        control.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        control.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 110, 26);
        control.setTitle (names[i]);
        control.setComponentID ("band." + names[i].toLowerCase());
        control.onDragStart = [this] {
            responseGraph.setEnabled (false);
            for (auto& button : bandButtons)
                button.setEnabled (false);
        };
        control.onDragEnd = [this] {
            responseGraph.setEnabled (true);
            for (auto& button : bandButtons)
                button.setEnabled (true);
        };
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
    selectBand (0);

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
    setSize (720, 580);
}

void PluginEditor::selectBand (size_t band)
{
    if (band > openq::bandCount)
        return;
    responseGraph.setSelectedBand (band);
    // Commit text edits to the original band before replacing attachments.
    for (auto& control : controls)
        control.hideTextBox (false);
    statusAttachment.reset();
    for (auto& attachment : controlAttachments)
        attachment.reset();
    for (auto& control : controls)
        control.setEnabled (band < openq::bandCount);
    statusSelector.setEnabled (band < openq::bandCount);
    if (band == openq::bandCount)
    {
        for (auto& button : bandButtons)
            button.setToggleState (false, juce::dontSendNotification);
        statusSelector.setSelectedId (0, juce::dontSendNotification);
        return;
    }
    const std::array<const char*, 3> names { "frequency", "gain", "q" };
    for (size_t i = 0; i < controls.size(); ++i)
    {
        controlAttachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.parameters, openq::bandParameterId (band, names[i]), controls[i]);
        controls[i].textFromValueFunction = nullptr;
        controls[i].setNumDecimalPlacesToDisplay (i == 0 ? 1 : 2);
    }
    statusAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.parameters, openq::bandParameterId (band, "status"), statusSelector);
    bandButtons[band].setToggleState (true, juce::dontSendNotification);
    statusSelector.setTitle ("Band " + juce::String (band + 1) + " status");
}

PluginEditor::~PluginEditor()
{
    responseGraph.cancelDrag();
    responseGraph.onBandSelected = nullptr;
    responseGraph.onDragStateChanged = nullptr;
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
    auto selector = area.removeFromTop (30);
    statusSelector.setBounds (selector.removeFromRight (150).reduced (4, 0));
    const auto buttonWidth = selector.getWidth() / static_cast<int> (bandButtons.size());
    for (auto& button : bandButtons)
        button.setBounds (selector.removeFromLeft (buttonWidth).reduced (2, 0));
    area.removeFromTop (8);
    inspectButton.setBounds (area.removeFromBottom (32).removeFromLeft (130));
    const auto width = area.getWidth() / 3;
    for (size_t i = 0; i < controls.size(); ++i)
    {
        auto column = area.removeFromLeft (width).reduced (6);
        labels[i].setBounds (column.removeFromTop (24));
        controls[i].setBounds (column);
    }
}
