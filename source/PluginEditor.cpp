#include "PluginEditor.h"
#include "ui/presets/preset_browser.hpp"
#include "ui/GainTimingTrace.h"
#include "ui/Palette.h"
#include "ui/UiScale.h"

PluginEditor::PluginEditor (PluginProcessor& p)
    : AudioProcessorEditor (&p), processor (p), analyzerTransport (p.getAnalyzerTransport()), headerBar (p.getAnalyzerTransport(), p.parameters), bottomBar (p.parameters), responseGraph (p.parameters, p.getResponseSampleRate()), bandCard (p.parameters)
{
    setLookAndFeel (&lookAndFeel);
    // Panel dismissal returns focus here instead of selecting a graph node.
    setWantsKeyboardFocus (true);
    responseGraph.setAnalyzerTransport (analyzerTransport);
    addAndMakeVisible (responseGraph);
    addAndMakeVisible (headerBar);
    addAndMakeVisible (bottomBar);
    inputMeter.setComponentID ("meter.input");
    outputMeter.setComponentID ("meter.output");
    addAndMakeVisible (inputMeter);
    addAndMakeVisible (outputMeter);
    responseGraph.onFrameRefresh = [this, bypass = p.parameters.getRawParameterValue (openq::bypassParameterId)] {
        const bool bypassed = bypass->load (std::memory_order_relaxed) > 0.5f;
        responseGraph.setGloballyBypassed (bypassed);
        inputMeter.setGloballyBypassed (bypassed);
        outputMeter.setGloballyBypassed (bypassed);
        refreshMeters();
        if (presetBrowser != nullptr && presetBrowser->isVisible())
            presetBrowser->flushPendingScroll();
    };
    responseGraph.onFrameRefresh();
    responseGraph.hasPendingControlFeedback = [this] { return bottomBar.hasPendingGainFeedback(); };
    headerBar.onFillChanged = [this] { responseGraph.repaint(); };
    bottomBar.analyzer.onClick = [this] {
        // Hosts may create probe editors before their event loop can drain startup notifications.
        if (bottomBar.analyzer.getToggleState() && analyzerPanel == nullptr)
        {
            analyzerPanel = std::make_unique<openq::AnalyzerPanel> (analyzerTransport);
            addChildComponent (*analyzerPanel);
            analyzerPanel->onClose = [this] {
                analyzerPanel->dismissPopups();
                analyzerPanel->setVisible (false);
                bottomBar.analyzer.setToggleState (false, juce::dontSendNotification);
            };
            positionAnalyzerPanel();
        }
        if (analyzerPanel != nullptr)
        {
            if (!bottomBar.analyzer.getToggleState())
            {
                analyzerPanel->onClose();
                return;
            }
            analyzerPanel->refresh();
            analyzerPanel->setVisible (bottomBar.analyzer.getToggleState());
            if (analyzerPanel != nullptr && analyzerPanel->isVisible())
                analyzerPanel->toFront (false);
            if (presetBrowser != nullptr && presetBrowser->isVisible())
                presetBrowser->toFront (false);
        }
    };
    timerCallback();
    startTimerHz (30);
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
    headerBar.onPresets = [this] {
        if (!headerBar.presets.getToggleState())
        {
            closePresetBrowser();
            return;
        }
        responseGraph.cancelDrag();
        if (analyzerPanel != nullptr && analyzerPanel->isVisible() && analyzerPanel->onClose)
            analyzerPanel->onClose();
        responseGraph.setSelectedBand (openq::bandCount);
        bandCard.setBand (openq::bandCount);
        if (presetBrowser == nullptr)
        {
            presetBrowser = std::make_unique<openq::presets::PresetBrowser> (processor);
            addChildComponent (*presetBrowser);
            presetBrowser->onClose = [this] { closePresetBrowser(); };
        }
        positionPresetBrowser();
        presetBrowser->setVisible (true);
        presetBrowser->toFront (false);
    };
    headerBar.onInspect = [this] {
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
    addMouseListener (this, true);
    const auto size = p.getEditorSize();
    setSize (size.width, size.height);
}

void PluginEditor::mouseDown (const juce::MouseEvent& event)
{
    auto* target = event.originalComponent;
    if (presetBrowser != nullptr && presetBrowser->isVisible() && target != nullptr
        && target != presetBrowser.get() && !presetBrowser->isParentOf (target)
        && target != &headerBar.presets && !headerBar.presets.isParentOf (target))
        closePresetBrowser();
    if (analyzerPanel == nullptr || !analyzerPanel->isVisible() || target == nullptr || target == analyzerPanel.get() || analyzerPanel->isParentOf (target)
        || target == &bottomBar.analyzer || bottomBar.analyzer.isParentOf (target))
        return;
    // Selector menus are editor siblings, so treat their active menu as part of the panel.
    if (auto* popup = juce::Component::getCurrentlyModalComponent(); analyzerPanel->isPopupActive() && popup != nullptr
                                                                     && (target == popup || popup->isParentOf (target)))
        return;
    if (analyzerPanel->onClose)
        analyzerPanel->onClose();
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
    const auto graph = responseGraph.getBounds().withLeft (inputMeter.getRight()).withRight (outputMeter.getX()).withTop (headerBar.getBottom()).withBottom (bottomBar.getY()).reduced (cardAreaHorizontalInset, cardAreaVerticalInset);
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
            if (analyzerPanel != nullptr && analyzerPanel->isVisible())
                analyzerPanel->toFront (false);
            if (presetBrowser != nullptr && presetBrowser->isVisible())
                presetBrowser->toFront (false);
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
    if (analyzerPanel != nullptr && analyzerPanel->isVisible())
        analyzerPanel->toFront (false);
    if (presetBrowser != nullptr && presetBrowser->isVisible())
        presetBrowser->toFront (false);
}

PluginEditor::~PluginEditor()
{
    if (presetBrowser != nullptr)
        presetBrowser->onClose = nullptr;
    headerBar.onPresets = nullptr;
    removeMouseListener (this);
    stopTimer();
    responseGraph.cancelDrag();
    responseGraph.onBandSelected = nullptr;
    responseGraph.onDragStateChanged = nullptr;
    responseGraph.onNodeLayoutChanged = nullptr;
    responseGraph.onFrameRefresh = nullptr;
    responseGraph.hasPendingControlFeedback = nullptr;
    bandCard.onDragStateChanged = nullptr;
    bandCard.onDelete = nullptr;
    bandCard.onClose = nullptr;
    if (analyzerPanel != nullptr)
    {
        analyzerPanel->onClose = nullptr;
        analyzerPanel->dismissPopups();
    }
    setLookAndFeel (nullptr);
    setConstrainer (nullptr);
}

void PluginEditor::timerCallback()
{
    const auto size = processor.getEditorSize();
    if (getWidth() > 0 && openq::EditorSize { getWidth(), getHeight() } != size)
        setSize (size.width, size.height);
    const auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    bottomBar.messageThreadTick (now);
    if (!isShowing())
    {
        (void) processor.getLevelMeters().read();
        lastMeterTime = 0.0;
    }
    headerBar.refresh();
    headerBar.setPresetName (processor.getAppliedPresetName());
    if (analyzerPanel != nullptr)
        analyzerPanel->refresh();
}

void PluginEditor::refreshMeters()
{
    const auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const auto reading = processor.getLevelMeters().read();
    if ((isShowing() && lastMeterTime <= 0.0) || (!isShowing() && lastMeterTime > 0.0) || reading.generation != meterGeneration)
    {
        inputMeter.reset();
        outputMeter.reset();
        meterGeneration = reading.generation;
    }
    if (isShowing())
    {
        const auto elapsed = lastMeterTime > 0.0 ? now - lastMeterTime : 0.0;
        inputMeter.update (reading.peaks[0], reading.channels, elapsed);
        outputMeter.update (reading.peaks[1], reading.channels, elapsed);
        lastMeterTime = now;
    }
    else
        lastMeterTime = 0.0;
}

void PluginEditor::paint (juce::Graphics& g)
{
    g.fillAll (openq::palette::background);
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
    if (analyzerPanel != nullptr)
        analyzerPanel->dismissPopups();
    if (!juce::exactlyEqual (lookAndFeel.scale, uiScale))
    {
        lookAndFeel.scale = uiScale;
        sendLookAndFeelChange();
    }
    const openq::UiScale scales (size.width);
    headerBar.setUiScale (scales.header());
    bottomBar.setUiScale (scales.bottom());
    responseGraph.setUiScale (scales.graph());
    const auto area = getLocalBounds();
    headerBar.setBounds (area.withHeight (juce::roundToInt (openq::HeaderBar::preferredHeight * scales.header())));
    bottomBar.setBounds (area.withTop (area.getBottom() - juce::roundToInt (openq::BottomBar::preferredHeight * scales.bottom())));
    const auto meterWidth = juce::roundToInt (openq::LevelMeter::preferredWidth * scales.bottom());
    const auto meterHeight = bottomBar.getY() - headerBar.getBottom();
    inputMeter.setBounds (0, headerBar.getBottom(), meterWidth, meterHeight);
    outputMeter.setBounds (getWidth() - meterWidth, headerBar.getBottom(), meterWidth, meterHeight);
    responseGraph.setContentInsets ({ headerBar.getHeight() + scaled (4), meterWidth + scaled (4), bottomBar.getHeight() + scaled (4), meterWidth + scaled (6) });
    responseGraph.setBounds (area);
    positionBandCard (true);
    positionAnalyzerPanel();
    positionPresetBrowser();
    processor.setEditorSize (size);
}

void PluginEditor::closePresetBrowser()
{
    if (getPeer() != nullptr)
        grabKeyboardFocus();
    if (presetBrowser != nullptr)
        presetBrowser->setVisible (false);
    headerBar.presets.setToggleState (false, juce::dontSendNotification);
}

void PluginEditor::positionPresetBrowser()
{
    if (presetBrowser == nullptr)
        return;
    presetBrowser->setUiScale (uiScale);
    auto area = getLocalBounds().withTop (headerBar.getBottom()).withBottom (bottomBar.getY()).reduced (12);
    presetBrowser->setBounds (area.withSizeKeepingCentre (std::min (area.getWidth(), presetBrowser->getIdealWidth()), std::min (area.getHeight(), presetBrowser->getIdealHeight())));
}

void PluginEditor::positionAnalyzerPanel()
{
    if (analyzerPanel == nullptr)
        return;
    const auto scaled = [this] (int value) { return juce::roundToInt (static_cast<float> (value) * uiScale); };
    const auto panelWidth = scaled (openq::AnalyzerPanel::preferredWidth);
    const auto anchor = getLocalArea (&bottomBar.analyzer, bottomBar.analyzer.getLocalBounds());
    const auto panelHeight = scaled (openq::AnalyzerPanel::preferredHeight);
    analyzerPanel->setBounds (std::clamp (anchor.getX(), scaled (12), getWidth() - panelWidth - scaled (12)),
        std::max (headerBar.getBottom(), anchor.getY() - scaled (8) - panelHeight),
        panelWidth,
        panelHeight);
}

void PluginEditor::BoundsConstrainer::checkBounds (juce::Rectangle<int>& bounds, const juce::Rectangle<int>& previous, const juce::Rectangle<int>& limits, bool top, bool left, bool bottom, bool right)
{
    juce::ComponentBoundsConstrainer::checkBounds (bounds, previous, limits, top, left, bottom, right);
    const auto size = openq::EditorSize { bounds.getWidth(), bounds.getHeight() }.constrained();
    if (top)
        bounds.setTop (bounds.getBottom() - size.height);
    bounds.setSize (size.width, size.height);
}

bool PluginEditor::keyPressed (const juce::KeyPress& key)
{
    if (key.getKeyCode() != 'T' || !key.getModifiers().isCtrlDown() || !key.getModifiers().isShiftDown())
        return false;
    auto& trace = openq::GainTimingTrace::instance();
    if (!trace.isRecording())
    {
        trace.start();
        juce::Logger::writeToLog ("Open-Q gain timing capture started; Ctrl+Shift+T stops and exports.");
    }
    else
    {
        trace.stop();
        const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getChildFile ("Open-Q-gain-timing.csv")
                              .getNonexistentSibling();
        const bool saved = file.replaceWithText (trace.toCsv());
        juce::Logger::writeToLog (saved ? "Open-Q gain timing saved: " + file.getFullPathName() : "Open-Q gain timing export failed: " + file.getFullPathName());
    }
    return true;
}
