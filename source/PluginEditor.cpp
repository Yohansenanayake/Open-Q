#include "PluginEditor.h"
#include "ui/Palette.h"

PluginEditor::PluginEditor (PluginProcessor& p)
    : AudioProcessorEditor (&p), analyzerTransport (p.getAnalyzerTransport()), responseGraph (p.parameters, p.getResponseSampleRate()), bandCard (p.parameters)
{
    responseGraph.setAnalyzerTransport (analyzerTransport);
    addAndMakeVisible (responseGraph);
    addAndMakeVisible (preTrace);
    addAndMakeVisible (spectrumFill);
    spectrumFill.setComponentID ("analyzer.fill");
    spectrumFill.setTooltip ("Fill the spectra; turn off to show outlines only.");
    spectrumFill.onClick = [this] { analyzerTransport.setFillEnabled (spectrumFill.getToggleState()); responseGraph.repaint(); };
    addAndMakeVisible (postTrace);
    addAndMakeVisible (analyzerQuality);
    preTrace.setComponentID ("analyzer.pre");
    postTrace.setComponentID ("analyzer.post");
    analyzerQuality.setComponentID ("analyzer.quality");
    preTrace.setColour (juce::ToggleButton::tickColourId, openq::palette::analyzerPre);
    postTrace.setColour (juce::ToggleButton::tickColourId, openq::palette::analyzerPost);
    preTrace.setTooltip ("Show the input spectrum (averaged channel power).");
    postTrace.setTooltip ("Show the output spectrum (averaged channel power).");
    analyzerQuality.setTooltip ("Analyzer quality only; does not change audio or plugin latency. Display tilt: 4.5 dB/oct, anchored at 1 kHz.");
    analyzerQuality.addItem ("Normal", 1);
    analyzerQuality.addItem ("High", 2);
    preTrace.onClick = [this] {
        auto settings = analyzerTransport.getSettings();
        settings.pre = preTrace.getToggleState();
        analyzerTransport.setSettings (settings);
    };
    postTrace.onClick = [this] {
        auto settings = analyzerTransport.getSettings();
        settings.post = postTrace.getToggleState();
        analyzerTransport.setSettings (settings);
    };
    analyzerQuality.onChange = [this] {
        auto settings = analyzerTransport.getSettings();
        settings.quality = analyzerQuality.getSelectedId() == 2 ? openq::AnalyzerQuality::High : openq::AnalyzerQuality::Normal;
        analyzerTransport.setSettings (settings);
    };
    timerCallback();
    startTimerHz (10);
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
    stopTimer();
    responseGraph.cancelDrag();
    responseGraph.onBandSelected = nullptr;
    responseGraph.onDragStateChanged = nullptr;
    responseGraph.onNodeLayoutChanged = nullptr;
    bandCard.onDragStateChanged = nullptr;
    bandCard.onDelete = nullptr;
    bandCard.onClose = nullptr;
}

void PluginEditor::timerCallback()
{
    const auto settings = analyzerTransport.getSettings();
    spectrumFill.setToggleState (analyzerTransport.getFillEnabled(), juce::dontSendNotification);
    preTrace.setToggleState (settings.pre, juce::dontSendNotification);
    postTrace.setToggleState (settings.post, juce::dontSendNotification);
    analyzerQuality.setSelectedId (settings.quality == openq::AnalyzerQuality::High ? 2 : 1, juce::dontSendNotification);
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
    header.removeFromRight (8);
    analyzerQuality.setBounds (header.removeFromRight (100).reduced (0, 4));
    postTrace.setBounds (header.removeFromRight (66));
    preTrace.setBounds (header.removeFromRight (60));
    spectrumFill.setBounds (header.removeFromRight (60));
    area.removeFromTop (4);
    responseGraph.setBounds (area);
    positionBandCard();
}
