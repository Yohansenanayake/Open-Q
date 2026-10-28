// SPDX-License-Identifier: AGPL-3.0-only
#include "ui/ControlDrawing.h"
#include "ui/UiLookAndFeel.h"
#include "ui/IconButton.h"
#include "BinaryData.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE ("Flat control borders stay neutral on hover and focus while disabled controls stay quiet", "[ui][controls]")
{
    const auto render = [] (bool enabled, bool hovered, bool focused) {
        juce::Image image (juce::Image::ARGB, 120, openq::controlStyle::height, true);
        juce::Graphics g (image);
        openq::drawControlBackground (g, image.getBounds().toFloat(), 1.0f, enabled, hovered, focused);
        return image;
    };
    const auto edge = juce::Point<int> (0, openq::controlStyle::height / 2);
    CHECK (render (true, false, false).getPixelAt (edge.x, edge.y) == openq::palette::controlBorder);
    CHECK (render (true, true, false).getPixelAt (edge.x, edge.y) == openq::palette::controlHoverBorder);
    CHECK (render (true, false, true).getPixelAt (edge.x, edge.y) == openq::palette::controlHoverBorder);
    const auto disabled = render (false, false, false);
    const auto disabledHover = render (false, true, true);
    for (int y = 0; y < disabled.getHeight(); ++y)
        for (int x = 0; x < disabled.getWidth(); ++x)
            REQUIRE (disabled.getPixelAt (x, y) == disabledHover.getPixelAt (x, y));
}

TEST_CASE ("Icon buttons stay borderless with transparent On and Off backgrounds", "[ui][controls]")
{
    openq::UiLookAndFeel lookAndFeel;
    openq::IconButton button ("Power", BinaryData::bypass_svg, BinaryData::bypass_svgSize);
    button.setBounds (0, 0, openq::controlStyle::height, openq::controlStyle::height);
    for (bool on : { false, true })
    {
        button.setToggleState (on, juce::dontSendNotification);
        for (bool enabled : { false, true })
        {
            button.setEnabled (enabled);
            for (bool hovered : { false, true })
            {
                for (bool pressed : { false, true })
                {
                    juce::Image image (juce::Image::ARGB, button.getWidth(), button.getHeight(), true);
                    juce::Graphics g (image);
                    lookAndFeel.drawDrawableButton (g, button, hovered, pressed);
                    const auto centre = image.getHeight() / 2;
                    CHECK (image.getPixelAt (centre, centre).isTransparent());
                    CHECK (image.getPixelAt (0, centre).isTransparent());
                }
            }
        }
    }
}
