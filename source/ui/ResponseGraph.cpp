// SPDX-License-Identifier: AGPL-3.0-only
// Node interaction follows ZLEqualizer (C) 2026 zsliu98, revision 3ce598ee065b3d3847121e76e196feda7ed8f56d.
// Open-Q adaptation by Yohan Senanayake; see THIRD_PARTY_NOTICES.md.
#include "ResponseGraph.h"

namespace openq
{
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
            g.setColour (juce::Colour (0xff161b23));
            g.fillEllipse (circle);
            g.setColour (juce::Colour (0xff6edbd4));
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

    ResponseGraph::ResponseGraph (juce::AudioProcessorValueTreeState& state, const std::atomic<double>& rate)
        : parameters (state), sampleRate (rate), worker (state, rate)
    {
        setTitle ("Frequency response");
        setComponentID ("response.graph");
        setDescription ("Combined digital EQ response. Drag band nodes to edit frequency and gain. Bypassed nodes remain dimmed and editable; Off nodes are hidden.");
        setOpaque (true);
        for (size_t band = 0; band < bandCount; ++band)
        {
            bandParameters[band] = getBandParameterPointers (parameters, band);
            nodes[band] = std::make_unique<Node> (*this, band);
            addChildComponent (*nodes[band]);
        }
        startTimer (250);
    }

    ResponseGraph::~ResponseGraph()
    {
        cancelDrag();
        stopTimer();
        vblank.reset();
        worker.stop();
    }

    juce::Rectangle<int> ResponseGraph::plotBounds() const
    {
        return getLocalBounds().withTrimmedLeft (52).withTrimmedRight (24).withTrimmedTop (36).withTrimmedBottom (32);
    }

    void ResponseGraph::resized()
    {
        cancelDrag();
        const auto bounds = plotBounds();
        worker.setGeometry ({ std::max (0, bounds.getWidth()), std::max (0, bounds.getHeight()) });
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
                vblank = std::make_unique<juce::VBlankAttachment> (this, [this] (double) { updateNodes(); repaint(); });
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
    }

    double ResponseGraph::maximumFrequency() const
    {
        const auto rate = displaySampleRate;
        return std::isfinite (rate) && rate >= 1000.0 ? std::min (20000.0, rate * 0.5) : 0.0;
    }

    juce::Point<float> ResponseGraph::nodePosition (const BandSettings& settings) const
    {
        const auto plot = plotBounds().toFloat();
        const auto frequency = std::clamp (settings.frequency, 20.0, std::min (20000.0, displaySampleRate * 0.49));
        return { plot.getX() + static_cast<float> (std::log (frequency / 20.0) / std::log (maximumFrequency() / 20.0)) * plot.getWidth(),
            plot.getY() + static_cast<float> ((24.0 - settings.gain * (settings.type == FilterType::Bell ? 1.0 : 0.5)) / 48.0) * plot.getHeight() };
    }

    juce::Point<float> ResponseGraph::constrainPosition (juce::Point<float> position, FilterType type) const
    {
        auto plot = plotBounds().toFloat();
        const auto max = std::min (20000.0, displaySampleRate * 0.49);
        plot.setWidth (plot.getWidth() * static_cast<float> (std::log (max / 20.0) / std::log (maximumFrequency() / 20.0)));
        if (type != FilterType::Bell)
            plot = plot.withSizeKeepingCentre (plot.getWidth(), plot.getHeight() * 0.5f);
        return plot.getConstrainedPoint (position);
    }

    void ResponseGraph::updateNodes()
    {
        const auto previousBounds = getSelectedNodeBounds();
        displaySampleRate = sampleRate.load (std::memory_order_relaxed);
        const bool valid = maximumFrequency() > 20.0 && !plotBounds().isEmpty();
        if (draggedBand < bandCount && (!valid || !juce::exactlyEqual (dragSampleRate, displaySampleRate) || bandParameters[draggedBand].load().status == BandStatus::Off || bandParameters[draggedBand].load().type != dragType))
            cancelDrag();
        for (size_t band = 0; band < bandCount; ++band)
        {
            const auto settings = bandParameters[band].load();
            nodes[band]->setVisible (valid && settings.status != BandStatus::Off);
            nodes[band]->setToggleState (band == selectedBand, juce::dontSendNotification);
            nodes[band]->setAlpha (settings.status == BandStatus::Bypass ? 0.4f : 1.0f);
            if (valid)
            {
                const auto point = band == draggedBand ? dragPosition : nodePosition (settings);
                nodes[band]->setBounds (juce::Rectangle<int> (24, 24).withCentre (point.roundToInt()));
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
        if (maximumFrequency() <= 20.0 || plotBounds().isEmpty() || bandParameters[band].load().status == BandStatus::Off)
            return;
        draggedBand = band;
        dragPosition = nodePosition (bandParameters[band].load());
        dragType = bandParameters[band].load().type;
        lastPointer = event.position;
        dragSampleRate = displaySampleRate;
        dragParameters = { parameters.getParameter (bandParameterId (band, "frequency")), parameters.getParameter (bandParameterId (band, "gain")) };
        for (auto* parameter : dragParameters)
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
        const auto scale = event.mods.isShiftDown() ? 0.15f : 1.0f;
        const bool lockX = event.mods.isCommandDown() && event.mods.isLeftButtonDown();
        const bool lockY = event.mods.isCommandDown() && !event.mods.isLeftButtonDown();
        dragPosition = constrainPosition (dragPosition + juce::Point<float> (lockX ? 0.0f : motion.x * scale, lockY ? 0.0f : motion.y * scale), dragType);
        const auto plot = plotBounds().toFloat();
        if (!lockX && !juce::exactlyEqual (motion.x, 0.0f))
        {
            const auto frequency = std::min (20000.0, displaySampleRate * 0.49);
            const auto target = std::clamp (20.0 * std::pow (maximumFrequency() / 20.0, (dragPosition.x - plot.getX()) / plot.getWidth()), 20.0, frequency);
            dragParameters[0]->setValueNotifyingHost (dragParameters[0]->convertTo0to1 (static_cast<float> (target)));
        }
        if (!lockY && !juce::exactlyEqual (motion.y, 0.0f))
            dragParameters[1]->setValueNotifyingHost (dragParameters[1]->convertTo0to1 (static_cast<float> (((1.0 - (dragPosition.y - plot.getY()) / plot.getHeight()) * 48.0 - 24.0) * (dragType == FilterType::Bell ? 1.0 : 2.0))));
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
        const auto q = bandParameters[band].load().q;
        setParameter (band, "q", static_cast<float> (std::clamp (q * std::exp2 (delta * (modifiers.isShiftDown() ? 0.3 : 2.0)), 0.1, 10.0)));
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
        if (!event.mods.isLeftButtonDown() || maximumFrequency() <= 20.0 || !plotBounds().contains (event.position.roundToInt()))
            return;
        for (size_t band = 0; band < bandCount; ++band)
        {
            if (bandParameters[band].load().status != BandStatus::Off)
                continue;
            const auto plot = plotBounds().toFloat();
            const auto point = constrainPosition (event.position);
            setParameter (band, "frequency", static_cast<float> (20.0 * std::pow (maximumFrequency() / 20.0, (point.x - plot.getX()) / plot.getWidth())));
            setParameter (band, "gain", (1.0f - (point.y - plot.getY()) / plot.getHeight()) * 48.0f - 24.0f);
            setParameter (band, "q", 1.0f);
            setParameter (band, "type", static_cast<float> (FilterType::Bell));
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
        g.fillAll (juce::Colour (0xff161b23));
        g.setColour (juce::Colour (0xffb4bfce));
        g.setFont (13.0f);
        g.drawText ("Frequency response", 52, 6, 200, 24, juce::Justification::centredLeft);
        g.drawText ("dB", 2, 6, 42, 24, juce::Justification::centredRight);
        if (frame.valid)
            g.drawText (juce::String (frame.response.sampleRate / 1000.0, 1) + " kHz", getWidth() - 110, 6, 90, 24, juce::Justification::centredRight);
        if (plot.isEmpty())
            return;

        for (int db = -24; db <= 24; db += 6)
        {
            const auto y = plot.getY() + (24 - db) * plot.getHeight() / 48;
            g.setColour (juce::Colour (db == 0 ? 0xff566375 : 0xff2b3441));
            g.drawHorizontalLine (y, static_cast<float> (plot.getX()), static_cast<float> (plot.getRight()));
            g.setColour (juce::Colour (0xffb4bfce));
            g.drawText (juce::String (db), 2, y - 9, 42, 18, juce::Justification::centredRight);
        }
        const auto maximum = frame.valid ? std::min (20000.0, frame.response.sampleRate * 0.5) : 20000.0;
        for (const auto frequency : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
        {
            if (frequency > maximum)
                continue;
            const auto proportion = std::log (frequency / 20.0) / std::log (maximum / 20.0);
            const auto x = static_cast<int> (std::round (plot.getX() + proportion * plot.getWidth()));
            g.setColour (juce::Colour (0xff2b3441));
            g.drawVerticalLine (x, static_cast<float> (plot.getY()), static_cast<float> (plot.getBottom()));
            g.setColour (juce::Colour (0xffb4bfce));
            const auto label = frequency >= 1000.0 ? juce::String (frequency / 1000.0, 0) + "k" : juce::String (frequency, 0);
            g.drawText (label, x - 22, plot.getBottom() + 5, 44, 20, juce::Justification::centred);
        }
        if (!frame.valid)
        {
            g.setColour (juce::Colour (0xffb4bfce));
            g.drawText ("Waiting for audio preparation", plot, juce::Justification::centred);
        }
        else if (frame.geometry == ResponseGeometry { plot.getWidth(), plot.getHeight() })
        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (plot.expanded (1));
            if (selectedBand < bandCount && bandParameters[selectedBand].load().status != BandStatus::Off
                && frame.settings[selectedBand] == bandParameters[selectedBand].load())
            {
                g.setColour (juce::Colour (0xff6edbd4).withAlpha (frame.settings[selectedBand].status == BandStatus::Bypass ? 0.2f : 0.45f));
                g.strokePath (frame.bandCurves[selectedBand], juce::PathStrokeType (1.0f), juce::AffineTransform::translation (static_cast<float> (plot.getX()), static_cast<float> (plot.getY())));
            }
            g.setColour (juce::Colour (0xff6edbd4));
            g.strokePath (frame.curve, juce::PathStrokeType (2.0f), juce::AffineTransform::translation (static_cast<float> (plot.getX()), static_cast<float> (plot.getY())));
        }
    }
}
