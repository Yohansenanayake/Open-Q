#include "PluginEditor.h"

PluginEditor::PluginEditor (PluginProcessor& p)
    : AudioProcessorEditor (&p), responseGraph (p.parameters, p.getResponseSampleRate()), bandCard (p.parameters)
{
    addAndMakeVisible (responseGraph);
    addChildComponent (bandCard);
    responseGraph.onBandSelected = [this] (size_t band) { selectBand (band); };
    responseGraph.onNodeLayoutChanged = [this] { positionBandCard(); };
    responseGraph.onDragStateChanged = [this] (bool dragging) {
        bandCard.setEnabled (!dragging);
    };
    bandCard.onDragStateChanged = [this] (bool dragging) {
        knobDragging = dragging;
        responseGraph.setEnabled (!dragging);
        if (!dragging)
            positionBandCard();
    };
    bandCard.onDelete = [this] { responseGraph.deleteSelectedBand(); };
    bandCard.onClose = [this] { selectBand (openq::bandCount); };
    selectBand (openq::bandCount);
    addAndMakeVisible (inspectButton);
    inspectButton.onClick = [this] {
        if (!inspector)
        {
            inspector = std::make_unique<melatonin::Inspector> (*this);
            inspector->onClose = [this] { inspector.reset(); };
        }
        inspector->setVisible (true);
    };
    setSize (720, 580);
}

void PluginEditor::selectBand (size_t band)
{
    if (band > openq::bandCount)
        return;
    responseGraph.setSelectedBand (band);
    bandCard.setBand (band);
    positionBandCard();
}

void PluginEditor::positionBandCard()
{
    if (!bandCard.isVisible() || knobDragging)
        return;
    const auto graph = responseGraph.getBounds().reduced (18, 44);
    const auto node = responseGraph.getSelectedNodeBounds().translated (responseGraph.getX(), responseGraph.getY()).expanded (12);
    const int width = 350, height = 170;
    const std::array<juce::Rectangle<int>, 5> positions {
        bandCard.getBounds(),
        juce::Rectangle<int> (graph.getX(), graph.getBottom() - height, width, height),
        juce::Rectangle<int> (graph.getRight() - width, graph.getBottom() - height, width, height),
        juce::Rectangle<int> (graph.getX(), graph.getY(), width, height),
        juce::Rectangle<int> (graph.getRight() - width, graph.getY(), width, height)
    };
    for (const auto& position : positions)
    {
        if (!position.isEmpty() && graph.contains (position) && !position.intersects (node))
        {
            bandCard.setBounds (position);
            break;
        }
    }
    bandCard.toFront (false);
}

PluginEditor::~PluginEditor()
{
    responseGraph.cancelDrag();
    responseGraph.onBandSelected = nullptr;
    responseGraph.onDragStateChanged = nullptr;
    responseGraph.onNodeLayoutChanged = nullptr;
    bandCard.onDragStateChanged = nullptr;
    bandCard.onDelete = nullptr;
    bandCard.onClose = nullptr;
}

void PluginEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff161b23));
    g.setColour (juce::Colour (0xffe0e7ef));
    g.setFont (24.0f);
    g.drawText ("Open-Q", 20, 8, 200, 32, juce::Justification::centredLeft);
}

void PluginEditor::resized()
{
    auto area = getLocalBounds().reduced (12);
    auto header = area.removeFromTop (36);
    inspectButton.setBounds (header.removeFromRight (130).reduced (0, 4));
    area.removeFromTop (4);
    responseGraph.setBounds (area);
    positionBandCard();
}
