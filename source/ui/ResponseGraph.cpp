// SPDX-License-Identifier: AGPL-3.0-only
// Node interaction follows ZLEqualizer (C) 2026 zsliu98, revision 3ce598ee065b3d3847121e76e196feda7ed8f56d.
// Open-Q adaptation by Yohan Senanayake; see THIRD_PARTY_NOTICES.md.
#include "ResponseGraph.h"
#include "Palette.h"
#include "ResponseMapping.h"

namespace openq
{
    namespace
    {
        constexpr int visibilityPollMilliseconds = 250;
        constexpr int nodeSize = 24;
        constexpr float fineDragScale = 0.15f;
        constexpr double qWheelSensitivity = 2.0;
        constexpr double fineQWheelSensitivity = 0.3;
        constexpr float bypassNodeOpacity = 0.4f;
    }

    class ResponseGraph::Node final : public juce::TextButton
    {
    public:
        Node (ResponseGraph& owner, size_t index) : graph (owner), band (index)
        {
            setButtonText (juce::String (band + 1));
            setTitle ("Band " + getButtonText());
            setComponentID ("node." + juce::String (band));
            setToggleable (true);
            onClick = [this] { graph.chooseBand (band); };
            setMouseCursor (juce::MouseCursor::DraggingHandCursor);
            setTooltip ("Drag frequency/gain; Shift for fine edits; Ctrl/Command locks frequency with left drag or gain with right drag. Wheel edits Q. Alt-click bypasses; Alt-right-double-click or Delete removes. Space toggles bypass.");
        }
        void paintButton (juce::Graphics& g, bool highlighted, bool) override
        {
            const auto circle = getLocalBounds().toFloat().reduced (4.0f);
            g.setColour (palette::background);
            g.fillEllipse (circle);
            g.setColour (palette::accent);
            g.drawEllipse (circle, graph.selectedBand == band || highlighted || hasKeyboardFocus (true) ? 2.5f : 1.0f);
            g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred);
        }
        void mouseDown (const juce::MouseEvent& event) override
        {
            graph.chooseBand (band);
            if (getPeer() != nullptr)
                grabKeyboardFocus();
            if (event.mods.isAltDown() && event.mods.isLeftButtonDown())
                graph.toggleBypass (band);
            else if (!event.mods.isAltDown())
                graph.beginDrag (band, event.getEventRelativeTo (&graph));
        }
        void mouseDrag (const juce::MouseEvent& event) override { graph.drag (event.getEventRelativeTo (&graph)); }
        void mouseUp (const juce::MouseEvent&) override { graph.cancelDrag(); }
        void mouseDoubleClick (const juce::MouseEvent& event) override
        {
            if (event.mods.isAltDown() && event.mods.isRightButtonDown())
                graph.deleteBand (band);
        }
        void mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& details) override { graph.wheel (band, details, event.mods); }
        void focusGained (FocusChangeType) override { graph.chooseBand (band); }
        void focusLost (FocusChangeType) override
        {
            if (graph.draggedBand == band)
                graph.cancelDrag();
        }
        bool keyPressed (const juce::KeyPress& key) override
        {
            if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
                graph.deleteBand (band);
            else if (key == juce::KeyPress::spaceKey)
                graph.toggleBypass (band);
            else if (key == juce::KeyPress::escapeKey)
                graph.cancelDrag();
            else
                return juce::TextButton::keyPressed (key);
            return true;
        }

    private:
        ResponseGraph& graph;
        size_t band;
    };

    class ResponseGraph::ResponseLayer final : public juce::Component
    {
    public:
        explicit ResponseLayer (ResponseGraph& owner) : graph (owner)
        {
            setComponentID ("response.curves");
            setInterceptsMouseClicks (false, false);
        }
        void paint (juce::Graphics& g) override { graph.paintResponse (g); }

    private:
        ResponseGraph& graph;
    };

    ResponseGraph::ResponseGraph (juce::AudioProcessorValueTreeState& state, const std::atomic<double>& rate)
        : parameters (state), sampleRate (rate), worker (state, rate)
    {
        setTitle ("Frequency response");
        setComponentID ("response.graph");
        setDescription ("Combined digital EQ response. Drag band nodes to edit frequency and gain. Bypassed nodes remain dimmed and editable; Off nodes are hidden.");
        setOpaque (true);
        addAndMakeVisible (artwork);
        responseLayer = std::make_unique<ResponseLayer> (*this);
        addAndMakeVisible (*responseLayer);
        for (size_t band = 0; band < bandCount; ++band)
        {
            bandParameters[band] = getBandParameterPointers (parameters, band);
            nodes[band] = std::make_unique<Node> (*this, band);
            addChildComponent (*nodes[band]);
        }
        startTimer (visibilityPollMilliseconds);
    }

    ResponseGraph::~ResponseGraph()
    {
        cancelDrag();
        stopTimer();
        vblank.reset();
        worker.stop();
        if (analyzer)
            analyzer->stop();
    }

    void ResponseGraph::setAnalyzerTransport (AnalyzerTransport& transport)
    {
        analyzer = std::make_unique<AnalyzerWorker> (transport);
        analyzerTransport = &transport;
        const auto bounds = plotBounds();
        analyzer->setGeometry ({ std::max (0, bounds.getWidth()), std::max (0, bounds.getHeight()) });
        updateVisibility();
    }

    juce::Rectangle<int> ResponseGraph::plotBounds() const
    {
        return getLocalBounds().withTrimmedLeft (52).withTrimmedRight (24).withTrimmedTop (36).withTrimmedBottom (32);
    }

    void ResponseGraph::resized()
    {
        cancelDrag();
        artwork.setBounds (getLocalBounds());
        responseLayer->setBounds (getLocalBounds());
        const auto bounds = plotBounds();
        worker.setGeometry ({ std::max (0, bounds.getWidth()), std::max (0, bounds.getHeight()) });
        if (analyzer)
            analyzer->setGeometry ({ std::max (0, bounds.getWidth()), std::max (0, bounds.getHeight()) });
        updateNodes();
    }

    void ResponseGraph::visibilityChanged() { updateVisibility(); }
    void ResponseGraph::parentHierarchyChanged() { updateVisibility(); }
    void ResponseGraph::timerCallback()
    {
        updateVisibility();
        updateNodes();
    }
    void ResponseGraph::enablementChanged()
    {
        if (!isEnabled())
            cancelDrag();
    }

    void ResponseGraph::updateVisibility()
    {
        if (isShowing() && vblank == nullptr)
        {
            if (worker.start())
            {
                refreshLimiter.reset();
                vblank = std::make_unique<juce::VBlankAttachment> (this, [this] (double timestamp) {
                    // Limit software painting independently of the monitor's refresh rate, as ZLE does.
                    if (refreshLimiter.tick (timestamp))
                    {
                        updateNodes();
                        repaint();
                    }
                });
                worker.requestUpdate();
                repaint();
            }
        }
        else if (!isShowing() && vblank != nullptr)
        {
            cancelDrag();
            vblank.reset();
            worker.stop();
        }
        if (analyzer)
        {
            if (isShowing())
                analyzer->start();
            else
                analyzer->stop();
        }
    }

    double ResponseGraph::maximumFrequency() const
    {
        const auto rate = displaySampleRate;
        return std::isfinite (rate) && rate >= bandLimits::minimumSampleRateHz ? responseMapping::maximumFrequency (rate) : 0.0;
    }

    juce::Point<float> ResponseGraph::nodePosition (const BandSettings& settings) const
    {
        const auto plot = plotBounds().toFloat();
        const auto frequency = std::clamp (settings.frequency, bandLimits::minimumFrequencyHz, bandLimits::effectiveMaximumFrequency (displaySampleRate));
        return { plot.getX() + static_cast<float> (responseMapping::frequencyToProportion (frequency, maximumFrequency())) * plot.getWidth(),
            plot.getY() + static_cast<float> (responseMapping::dbToProportion (settings.gain * responseMapping::nodeGainScale (settings.type))) * plot.getHeight() };
    }

    juce::Point<float> ResponseGraph::constrainPosition (juce::Point<float> position, FilterType type) const
    {
        auto plot = plotBounds().toFloat();
        const auto max = bandLimits::effectiveMaximumFrequency (displaySampleRate);
        plot.setWidth (plot.getWidth() * static_cast<float> (responseMapping::frequencyToProportion (max, maximumFrequency())));
        if (type != FilterType::Bell)
            plot = plot.withSizeKeepingCentre (plot.getWidth(), plot.getHeight() * static_cast<float> (responseMapping::nodeGainScale (type)));
        return plot.getConstrainedPoint (position);
    }

    void ResponseGraph::updateNodes()
    {
        const auto previousBounds = getSelectedNodeBounds();
        displaySampleRate = sampleRate.load (std::memory_order_relaxed);
        const bool valid = maximumFrequency() > bandLimits::minimumFrequencyHz && !plotBounds().isEmpty();
        if (draggedBand < bandCount && (!valid || !juce::exactlyEqual (dragSampleRate, displaySampleRate) || bandParameters[draggedBand].load().status == BandStatus::Off || bandParameters[draggedBand].load().type != dragType))
            cancelDrag();
        for (size_t band = 0; band < bandCount; ++band)
        {
            const auto settings = bandParameters[band].load();
            nodes[band]->setVisible (valid && settings.status != BandStatus::Off);
            nodes[band]->setToggleState (band == selectedBand, juce::dontSendNotification);
            nodes[band]->setAlpha (settings.status == BandStatus::Bypass ? bypassNodeOpacity : 1.0f);
            if (valid)
            {
                const auto point = band == draggedBand ? dragPosition : nodePosition (settings);
                nodes[band]->setBounds (juce::Rectangle<int> (nodeSize, nodeSize).withCentre (point.roundToInt()));
            }
        }
        if (selectedBand < bandCount)
            nodes[selectedBand]->toFront (false);
        if (getSelectedNodeBounds() != previousBounds && onNodeLayoutChanged)
            onNodeLayoutChanged();
    }

    juce::Rectangle<int> ResponseGraph::getSelectedNodeBounds() const
    {
        return selectedBand < bandCount && nodes[selectedBand] != nullptr && nodes[selectedBand]->isVisible() ? nodes[selectedBand]->getBounds() : juce::Rectangle<int>();
    }

    void ResponseGraph::deleteSelectedBand()
    {
        if (selectedBand < bandCount)
            deleteBand (selectedBand);
    }

    void ResponseGraph::setSelectedBand (size_t band)
    {
        band = std::min (band, bandCount);
        if (band != selectedBand)
            cancelDrag();
        selectedBand = band;
        updateNodes();
        repaint();
    }

    void ResponseGraph::chooseBand (size_t band)
    {
        if (band == selectedBand)
        {
            updateNodes();
            return;
        }
        setSelectedBand (band);
        if (onBandSelected)
            onBandSelected (band);
    }

    void ResponseGraph::setParameter (size_t band, const char* name, float value)
    {
        auto* parameter = parameters.getParameter (bandParameterId (band, name));
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
        parameter->endChangeGesture();
    }

    void ResponseGraph::beginDrag (size_t band, const juce::MouseEvent& event)
    {
        cancelDrag();
        if (maximumFrequency() <= bandLimits::minimumFrequencyHz || plotBounds().isEmpty() || bandParameters[band].load().status == BandStatus::Off)
            return;
        draggedBand = band;
        dragPosition = nodePosition (bandParameters[band].load());
        dragType = bandParameters[band].load().type;
        lastPointer = event.position;
        dragSampleRate = displaySampleRate;
        dragParameters = { parameters.getParameter (bandParameterId (band, "frequency")), parameters.getParameter (bandParameterId (band, "gain")) };
        if (isCut (dragType))
            dragParameters[1] = nullptr;
        for (auto* parameter : dragParameters)
            if (parameter != nullptr)
                parameter->beginChangeGesture();
        if (onDragStateChanged)
            onDragStateChanged (true);
    }

    void ResponseGraph::cancelDrag()
    {
        if (draggedBand == bandCount)
            return;
        draggedBand = bandCount;
        for (auto*& parameter : dragParameters)
        {
            auto* previous = std::exchange (parameter, nullptr);
            if (previous != nullptr)
                previous->endChangeGesture();
        }
        if (onDragStateChanged)
            onDragStateChanged (false);
    }

    void ResponseGraph::drag (const juce::MouseEvent& event)
    {
        updateNodes();
        if (draggedBand == bandCount)
            return;
        const auto motion = event.position - lastPointer;
        lastPointer = event.position;
        const auto scale = event.mods.isShiftDown() ? fineDragScale : 1.0f;
        const bool lockX = event.mods.isCommandDown() && event.mods.isLeftButtonDown();
        const bool lockY = event.mods.isCommandDown() && !event.mods.isLeftButtonDown();
        dragPosition = constrainPosition (dragPosition + juce::Point<float> (lockX ? 0.0f : motion.x * scale, lockY ? 0.0f : motion.y * scale), dragType);
        const auto plot = plotBounds().toFloat();
        if (!lockX && !juce::exactlyEqual (motion.x, 0.0f))
        {
            const auto frequency = bandLimits::effectiveMaximumFrequency (displaySampleRate);
            const auto target = std::clamp (responseMapping::proportionToFrequency ((dragPosition.x - plot.getX()) / plot.getWidth(), maximumFrequency()), bandLimits::minimumFrequencyHz, frequency);
            dragParameters[0]->setValueNotifyingHost (dragParameters[0]->convertTo0to1 (static_cast<float> (target)));
        }
        if (!isCut (dragType) && !lockY && !juce::exactlyEqual (motion.y, 0.0f))
            dragParameters[1]->setValueNotifyingHost (dragParameters[1]->convertTo0to1 (static_cast<float> (responseMapping::proportionToDb (static_cast<double> ((dragPosition.y - plot.getY()) / plot.getHeight())) / responseMapping::nodeGainScale (dragType))));
        updateNodes();
        repaint();
    }

    void ResponseGraph::toggleBypass (size_t band)
    {
        cancelDrag();
        const auto status = bandParameters[band].load().status;
        if (status != BandStatus::Off)
            setParameter (band, "status", static_cast<float> (status == BandStatus::On ? BandStatus::Bypass : BandStatus::On));
        updateNodes();
        repaint();
    }

    void ResponseGraph::deleteBand (size_t band)
    {
        cancelDrag();
        const auto frequency = bandParameters[band].load().frequency;
        setParameter (band, "status", static_cast<float> (BandStatus::Off));
        size_t next = bandCount;
        double distance = std::numeric_limits<double>::max();
        for (size_t candidate = 0; candidate < bandCount; ++candidate)
        {
            const auto settings = bandParameters[candidate].load();
            const auto delta = std::abs (std::log (settings.frequency / frequency));
            if (settings.status != BandStatus::Off && delta < distance)
            {
                next = candidate;
                distance = delta;
            }
        }
        chooseBand (next);
    }

    void ResponseGraph::wheel (size_t band, const juce::MouseWheelDetails& details, const juce::ModifierKeys& modifiers)
    {
        if (draggedBand < bandCount || bandParameters[band].load().status == BandStatus::Off)
            return;
        const auto delta = !juce::exactlyEqual (details.deltaY, 0.0f) ? details.deltaY : details.deltaX;
        if (!std::isfinite (delta) || juce::exactlyEqual (delta, 0.0f))
            return;
        const auto settings = bandParameters[band].load();
        if (isCut (settings.type) && settings.slope == CutSlope::Db6)
            return;
        const auto q = settings.q;
        setParameter (band, "q", static_cast<float> (std::clamp (q * std::exp2 (delta * (modifiers.isShiftDown() ? fineQWheelSensitivity : qWheelSensitivity)), bandLimits::minimumQ, bandLimits::maximumQ)));
        repaint();
    }

    void ResponseGraph::mouseDown (const juce::MouseEvent& event)
    {
        if (plotBounds().contains (event.position.roundToInt()))
            chooseBand (bandCount);
    }

    void ResponseGraph::mouseDoubleClick (const juce::MouseEvent& event)
    {
        updateNodes();
        if (!event.mods.isLeftButtonDown() || maximumFrequency() <= bandLimits::minimumFrequencyHz || !plotBounds().contains (event.position.roundToInt()))
            return;
        for (size_t band = 0; band < bandCount; ++band)
        {
            if (bandParameters[band].load().status != BandStatus::Off)
                continue;
            const auto plot = plotBounds().toFloat();
            const auto point = constrainPosition (event.position);
            setParameter (band, "frequency", static_cast<float> (responseMapping::proportionToFrequency ((point.x - plot.getX()) / plot.getWidth(), maximumFrequency())));
            setParameter (band, "gain", responseMapping::proportionToDb ((point.y - plot.getY()) / plot.getHeight()));
            setParameter (band, "q", static_cast<float> (bandLimits::defaultQ));
            setParameter (band, "type", static_cast<float> (FilterType::Bell));
            setParameter (band, "slope", static_cast<float> (defaultCutSlope));
            setParameter (band, "status", static_cast<float> (BandStatus::On));
            chooseBand (band);
            break;
        }
    }

    void ResponseGraph::paintOverChildren (juce::Graphics&)
    {
        worker.requestUpdate();
    }

    void ResponseGraph::paint (juce::Graphics& g)
    {
        worker.pullFrame();
        const auto& frame = worker.getFrame();
        const auto plot = plotBounds();
        artwork.setDisplay (plot, frame.valid ? frame.response.sampleRate : 0.0, analyzer != nullptr);
        g.fillAll (palette::background);
        if (plot.isEmpty())
            return;

        if (analyzer)
        {
            analyzer->pullFrame();
            const auto& spectrum = analyzer->getFrame();
            if (spectrum.valid && spectrum.geometry == ResponseGeometry { plot.getWidth(), plot.getHeight() }
                && spectrum.session == analyzerTransport->session()
                && spectrum.settings.encode() == analyzerTransport->getSettings().encode()
                && juce::exactlyEqual (spectrum.sampleRate, sampleRate.load (std::memory_order_relaxed)))
            {
                juce::Graphics::ScopedSaveState save (g);
                g.reduceClipRegion (plot);
                const auto translation = juce::AffineTransform::translation (static_cast<float> (plot.getX()), static_cast<float> (plot.getY()));
                for (size_t trace = 0; trace < spectrum.fills.size(); ++trace)
                {
                    const auto color = trace == 0 ? palette::analyzerPre : palette::analyzerPost;
                    if (analyzerTransport->getFillEnabled())
                    {
                        g.setColour (color.withAlpha (0.16f));
                        g.fillPath (spectrum.fills[trace], translation);
                    }
                    g.setColour (color.withAlpha (0.6f));
                    g.strokePath (spectrum.outlines[trace], juce::PathStrokeType (1.0f), translation);
                }
            }
        }
    }

    void ResponseGraph::paintResponse (juce::Graphics& g)
    {
        const auto& frame = worker.getFrame();
        const auto plot = plotBounds();
        if (plot.isEmpty())
            return;
        if (!frame.valid)
        {
            g.setColour (palette::secondaryText);
            g.drawText ("Waiting for audio preparation", plot, juce::Justification::centred);
        }
        else if (frame.geometry == ResponseGeometry { plot.getWidth(), plot.getHeight() })
        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (plot.expanded (1));
            if (selectedBand < bandCount && bandParameters[selectedBand].load().status != BandStatus::Off
                && frame.settings[selectedBand] == bandParameters[selectedBand].load())
            {
                g.setColour (palette::accent.withAlpha (frame.settings[selectedBand].status == BandStatus::Bypass ? 0.2f : 0.45f));
                g.strokePath (frame.bandCurves[selectedBand], juce::PathStrokeType (1.0f), juce::AffineTransform::translation (static_cast<float> (plot.getX()), static_cast<float> (plot.getY())));
            }
            g.setColour (palette::accent);
            g.strokePath (frame.curve, juce::PathStrokeType (2.0f), juce::AffineTransform::translation (static_cast<float> (plot.getX()), static_cast<float> (plot.getY())));
        }
    }
}
