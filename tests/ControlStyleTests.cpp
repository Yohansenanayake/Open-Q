// SPDX-License-Identifier: AGPL-3.0-only
#include "ui/ControlDrawing.h"
#include "ui/UiLookAndFeel.h"
#include "ui/IconButton.h"
#include "BinaryData.h"
#include "EditorSize.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

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

TEST_CASE ("Rotary artwork scales within its bounds and follows position without editing the slider", "[ui][controls][rotary]")
{
    openq::UiLookAndFeel lookAndFeel;
    juce::Slider slider;
    slider.setRange (0.0, 1.0);
    slider.setValue (0.37, juce::dontSendNotification);
    const auto accent = openq::palette::bandColour (0);
    slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    slider.setColour (juce::Slider::rotarySliderOutlineColourId, openq::palette::knobTrack);
    const auto countColour = [] (const juce::Image& image, juce::Colour colour) {
        int count = 0;
        for (int y = 0; y < image.getHeight(); ++y)
            for (int x = 0; x < image.getWidth(); ++x)
                if (image.getPixelAt (x, y) == colour)
                    ++count;
        return count;
    };
    for (const auto scale : { 1.0f, openq::EditorSize::scaleForWidth (openq::EditorSize::maximumWidth) })
    {
        lookAndFeel.scale = scale;
        const auto scaled = [scale] (int value) { return juce::roundToInt (static_cast<float> (value) * scale); };
        const juce::Rectangle<int> bounds { scaled (7), scaled (11), scaled (94), scaled (88) };
        const auto render = [&] (float position) {
            juce::Image image (juce::Image::ARGB, scaled (120), scaled (120), true);
            juce::Graphics g (image);
            lookAndFeel.drawRotarySlider (g, bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(), position,
                juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, slider);
            return image;
        };
        slider.setEnabled (true);
        const auto minimum = render (0.0f);
        const auto middle = render (0.5f);
        const auto maximum = render (1.0f);
        CHECK (countColour (minimum, accent) == 0);
        CHECK (countColour (middle, accent) > 0);
        CHECK (countColour (middle, openq::palette::knobIndicator) > 0);
        CHECK (countColour (maximum, accent) > countColour (middle, accent));
        CHECK (middle.getPixelAt (bounds.getCentreX(), bounds.getCentreY()) == openq::palette::knobFace);
        bool outsidePainted = false;
        for (int y = 0; y < middle.getHeight(); ++y)
            for (int x = 0; x < middle.getWidth(); ++x)
                if (!bounds.contains (x, y) && !middle.getPixelAt (x, y).isTransparent())
                    outsidePainted = true;
        CHECK_FALSE (outsidePainted);
        slider.setEnabled (false);
        const auto disabled = render (0.5f);
        CHECK (countColour (disabled, accent) == 0);
        CHECK (disabled.getPixelAt (bounds.getCentreX(), bounds.getCentreY()).getAlpha() < 255);
        CHECK_THAT (slider.getValue(), Catch::Matchers::WithinAbs (0.37, 1e-12));
    }
    juce::Image tiny (juce::Image::ARGB, 4, 4, true);
    {
        juce::Graphics g (tiny);
        lookAndFeel.drawRotarySlider (g, 0, 0, tiny.getWidth(), tiny.getHeight(), 0.5f, 0.0f, juce::MathConstants<float>::pi, slider);
    }
    CHECK (tiny.getPixelAt (2, 2).isTransparent());
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
                    {
                        juce::Graphics g (image);
                        lookAndFeel.drawDrawableButton (g, button, hovered, pressed);
                    }
                    const auto centre = image.getHeight() / 2;
                    CHECK (image.getPixelAt (centre, centre).isTransparent());
                    CHECK (image.getPixelAt (0, centre).isTransparent());
                }
            }
        }
    }
}
