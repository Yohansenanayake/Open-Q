#include "PluginEditor.h"
#include "ui/Palette.h"

PluginEditor::PluginEditor (PluginProcessor& p)
    : AudioProcessorEditor (&p), processor (p), analyzerTransport (p.getAnalyzerTransport()), responseGraph (p.parameters, p.getResponseSampleRate()), bandCard (p.parameters)
{
    setLookAndFeel (&lookAndFeel);
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
    boundsConstrainer.setSizeLimits (openq::EditorSize::minimumWidth, openq::EditorSize::minimumHeight, openq::EditorSize::maximumWidth, openq::EditorSize::maximumHeight);
    setConstrainer (&boundsConstrainer);
    setResizable (true, p.wrapperType != juce::AudioProcessor::wrapperType_AudioUnitv3);
    const auto size = p.getEditorSize();
    setSize (size.width, size.height);
}

void PluginEditor::selectBand (size_t band)
{
    if (band > openq::bandCount)
        return;
    responseGraph.setSelectedBand (band);
    bandCard.setBand (band);
    positionBandCard();
}

void PluginEditor::positionBandCard (bool resizing)
{
    if (!bandCard.isVisible() || (knobDragging && !resizing))
        return;
    const auto scaled = [this] (int value) { return juce::roundToInt (static_cast<float> (value) * uiScale); };
    const int cardAreaHorizontalInset = scaled (18), cardAreaVerticalInset = scaled (44);
    const int nodeClearance = scaled (12);
    const auto graph = responseGraph.getBounds().reduced (cardAreaHorizontalInset, cardAreaVerticalInset);
    const auto node = responseGraph.getSelectedNodeBounds().translated (responseGraph.getX(), responseGraph.getY()).expanded (nodeClearance);
    const int width = scaled (openq::BandControlCard::preferredWidth), height = scaled (openq::BandControlCard::preferredHeight);
    const std::array<juce::Rectangle<int>, 5> positions {
        bandCard.getBounds().withSize (width, height),
        juce::Rectangle<int> (graph.getX(), graph.getBottom() - height, width, height),
        juce::Rectangle<int> (graph.getRight() - width, graph.getBottom() - height, width, height),
        juce::Rectangle<int> (graph.getX(), graph.getY(), width, height),
        juce::Rectangle<int> (graph.getRight() - width, graph.getY(), width, height)
    };
    auto fallback = positions[1];
    auto overlap = std::numeric_limits<int>::max();
    for (const auto& position : positions)
    {
        if (!position.isEmpty() && graph.contains (position) && !position.intersects (node))
        {
            bandCard.setBounds (position);
            bandCard.toFront (false);
            return;
        }
        if (graph.contains (position))
        {
            const auto intersection = position.getIntersection (node);
            const auto area = intersection.getWidth() * intersection.getHeight();
            if (area < overlap)
            {
                fallback = position;
                overlap = area;
            }
        }
    }
    // Extremely short windows may offer no clear corner; keep the card inside the graph.
    bandCard.setBounds (fallback);
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
    setLookAndFeel (nullptr);
    setConstrainer (nullptr);
}

void PluginEditor::timerCallback()
{
    const auto size = processor.getEditorSize();
    if (getWidth() > 0 && openq::EditorSize { getWidth(), getHeight() } != size)
        setSize (size.width, size.height);
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
    g.setFont (24.0f * uiScale);
    g.drawText ("Open-Q", juce::Rectangle<float> (20.0f, 8.0f, 200.0f, 32.0f) * uiScale, juce::Justification::centredLeft);
}

void PluginEditor::resized()
{
    const auto size = openq::EditorSize { getWidth(), getHeight() }.constrained();
    if (size != openq::EditorSize { getWidth(), getHeight() })
    {
        setSize (size.width, size.height);
        return;
    }
    uiScale = size.scale();
    const auto scaled = [this] (int value) { return juce::roundToInt (static_cast<float> (value) * uiScale); };
    bandCard.dismissPopups();
    analyzerQuality.hidePopup();
    if (!juce::exactlyEqual (lookAndFeel.scale, uiScale))
    {
        lookAndFeel.scale = uiScale;
        sendLookAndFeelChange();
    }
    responseGraph.setUiScale (uiScale);
    auto area = getLocalBounds().reduced (scaled (12));
    auto header = area.removeFromTop (scaled (36));
    inspectButton.setBounds (header.removeFromRight (scaled (130)).reduced (0, scaled (4)));
    header.removeFromRight (scaled (8));
    analyzerQuality.setBounds (header.removeFromRight (scaled (100)).reduced (0, scaled (4)));
    postTrace.setBounds (header.removeFromRight (scaled (66)));
    preTrace.setBounds (header.removeFromRight (scaled (60)));
    spectrumFill.setBounds (header.removeFromRight (scaled (60)));
    area.removeFromTop (scaled (4));
    responseGraph.setBounds (area);
    positionBandCard (true);
    processor.setEditorSize (size);
}

void PluginEditor::BoundsConstrainer::checkBounds (juce::Rectangle<int>& bounds, const juce::Rectangle<int>& previous, const juce::Rectangle<int>& limits, bool top, bool left, bool bottom, bool right)
{
    juce::ComponentBoundsConstrainer::checkBounds (bounds, previous, limits, top, left, bottom, right);
    const auto size = openq::EditorSize { bounds.getWidth(), bounds.getHeight() }.constrained();
    if (top)
        bounds.setTop (bounds.getBottom() - size.height);
    bounds.setSize (size.width, size.height);
}

juce::Font PluginEditor::LookAndFeel::getTextButtonFont (juce::TextButton&, int)
{
    return juce::Font (juce::FontOptions (16.0f * scale));
}
juce::Font PluginEditor::LookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return juce::Font (juce::FontOptions (15.0f * scale));
}
juce::Font PluginEditor::LookAndFeel::getPopupMenuFont()
{
    return juce::Font (juce::FontOptions (15.0f * scale));
}
juce::Label* PluginEditor::LookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* label = juce::LookAndFeel_V4::createSliderTextBox (slider);
    label->setFont (juce::FontOptions (14.0f * scale));
    return label;
}
void PluginEditor::LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float position, float start, float end, juce::Slider& slider)
{
    juce::Graphics::ScopedSaveState save (g);
    g.addTransform (juce::AffineTransform::scale (scale));
    // Scale JUCE's capped stroke/padding metrics along with the knob bounds.
    juce::LookAndFeel_V4::drawRotarySlider (g, juce::roundToInt (x / scale), juce::roundToInt (y / scale), juce::roundToInt (width / scale), juce::roundToInt (height / scale), position, start, end, slider);
}
void PluginEditor::LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool down)
{
    const auto tickSize = 18.0f * scale;
    const auto y = (static_cast<float> (button.getHeight()) - tickSize) * 0.5f;
    drawTickBox (g, button, 2.0f * scale, y, tickSize, tickSize, button.getToggleState(), button.isEnabled(), highlighted, down);
    g.setColour (button.findColour (juce::ToggleButton::textColourId).withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f));
    g.setFont (15.0f * scale);
    g.drawText (button.getButtonText(), button.getLocalBounds().withTrimmedLeft (juce::roundToInt (24.0f * scale)), juce::Justification::centredLeft);
}
