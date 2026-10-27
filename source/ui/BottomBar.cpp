// SPDX-License-Identifier: AGPL-3.0-only
#include "BottomBar.h"
#include "Palette.h"
namespace openq
{
    BottomBar::BottomBar()
    {
        setComponentID ("editor.bottom");
        setLookAndFeel (&lookAndFeel);
        analyzer.setComponentID ("analyzer.settings");
        analyzer.setClickingTogglesState (true);
        addAndMakeVisible (analyzer);
    }
    BottomBar::~BottomBar() { setLookAndFeel (nullptr); }
    juce::Path BottomBar::backgroundShape() const
    {
        const auto scale = lookAndFeel.scale;
        const auto width = static_cast<float> (getWidth());
        const auto height = static_cast<float> (getHeight());
        const auto shoulder = 12.0f * scale;
        juce::Path path;
        path.startNewSubPath (0.0f, 0.0f);
        path.lineTo (116.0f * scale, 0.0f);
        path.lineTo (140.0f * scale, shoulder);
        path.lineTo (width, shoulder);
        path.lineTo (width, height);
        path.lineTo (0.0f, height);
        path.closeSubPath();
        return path;
    }
    void BottomBar::paint (juce::Graphics& g)
    {
        const auto shape = backgroundShape();
        g.setGradientFill (juce::ColourGradient (palette::bottomBarBackground.brighter (0.12f), 0.0f, 0.0f, palette::bottomBarBackground, 0.0f, static_cast<float> (getHeight()), false));
        g.fillPath (shape);
        g.setColour (palette::inactiveIcon.withAlpha (0.3f));
        g.strokePath (shape, juce::PathStrokeType (lookAndFeel.scale));
    }
    bool BottomBar::hitTest (int x, int y)
    {
        // Let the graph receive clicks above the toolbar's sloping edge.
        return backgroundShape().contains (static_cast<float> (x), static_cast<float> (y));
    }
    void BottomBar::setUiScale (float scale)
    {
        if (juce::exactlyEqual (lookAndFeel.scale, scale))
            return;
        lookAndFeel.scale = scale;
        sendLookAndFeelChange();
        resized();
        repaint();
    }
    void BottomBar::resized()
    {
        const auto scaled = [this] (float value) { return juce::roundToInt (value * lookAndFeel.scale); };
        analyzer.setBounds (scaled (8.0f), scaled (4.0f), scaled (100.0f), scaled (28.0f));
    }
}
