#include "PluginEditor.h"
#include "ui/Palette.h"

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
    constexpr int initialWidth = 720, initialHeight = 580;
    setSize (initialWidth, initialHeight);
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
    constexpr int cardAreaHorizontalInset = 18, cardAreaVerticalInset = 44;
    constexpr int nodeClearance = 12;
    const auto graph = responseGraph.getBounds().reduced (cardAreaHorizontalInset, cardAreaVerticalInset);
    const auto node = responseGraph.getSelectedNodeBounds().translated (responseGraph.getX(), responseGraph.getY()).expanded (nodeClearance);
    constexpr int width = openq::BandControlCard::preferredWidth, height = openq::BandControlCard::preferredHeight;
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
    g.fillAll (openq::palette::background);
    g.setColour (openq::palette::text);
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
