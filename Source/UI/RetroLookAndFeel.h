#pragma once
#include <JuceHeader.h>

class RetroLookAndFeel : public juce::LookAndFeel_V4
{
public:
    RetroLookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, juce::Colour (0xff080b0d));
        setColour (juce::Slider::textBoxTextColourId, juce::Colour (0xffe0e7e3));
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0xff0c1113));
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colour (0xff2f3b3e));
        setColour (juce::Slider::trackColourId, juce::Colour (0xff27383d));
        setColour (juce::Slider::thumbColourId, juce::Colour (0xff5bc1d9));
        setColour (juce::TextButton::buttonColourId, juce::Colour (0xff1d2629));
        setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff4f4329));
        setColour (juce::TextButton::textColourOffId, juce::Colour (0xffe0c97e));
        setColour (juce::TextButton::textColourOnId, juce::Colour (0xffffe8a3));
        setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff101719));
        setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff37464a));
        setColour (juce::ComboBox::textColourId, juce::Colour (0xffdce5df));
        setColour (juce::ComboBox::arrowColourId, juce::Colour (0xffb9c8c2));
        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff101719));
        setColour (juce::PopupMenu::textColourId, juce::Colour (0xffdde5de));
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float start, float end, juce::Slider&) override
    {
        auto available = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (5.0f);
        const float diameter = juce::jmax (30.0f, juce::jmin (available.getWidth(), available.getHeight()));
        auto b = juce::Rectangle<float> (diameter, diameter).withCentre (available.getCentre());
        const float radius = diameter * 0.5f;
        const auto centre = b.getCentre();
        const float angle = start + pos * (end - start);

        // Soft hardware-style shadow. The knob body is always derived from a square,
        // so it remains circular regardless of the cell aspect ratio.
        g.setColour (juce::Colour (0x90000000));
        g.fillEllipse (b.translated (1.5f, 3.0f));

        g.setColour (juce::Colour (0xff080a0b));
        g.fillEllipse (b.expanded (2.0f));
        g.setColour (juce::Colour (0xff596467));
        g.drawEllipse (b.expanded (1.0f), 1.0f);

        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff444c4f),
                                                 centre.x - radius * 0.35f, centre.y - radius * 0.42f,
                                                 juce::Colour (0xff111719),
                                                 centre.x + radius * 0.45f, centre.y + radius * 0.50f,
                                                 true));
        g.fillEllipse (b);
        g.setColour (juce::Colour (0xff697376));
        g.drawEllipse (b, 1.15f);

        auto arcBounds = b.reduced (4.0f);
        juce::Path backgroundArc;
        backgroundArc.addCentredArc (arcBounds.getCentreX(), arcBounds.getCentreY(),
                                     arcBounds.getWidth() * 0.5f, arcBounds.getHeight() * 0.5f,
                                     0.0f, start, end, true);
        g.setColour (juce::Colour (0xff20282b));
        g.strokePath (backgroundArc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path valueArc;
        valueArc.addCentredArc (arcBounds.getCentreX(), arcBounds.getCentreY(),
                                arcBounds.getWidth() * 0.5f, arcBounds.getHeight() * 0.5f,
                                0.0f, start, angle, true);
        g.setColour (juce::Colour (0xffd3ae58));
        g.strokePath (valueArc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path pointer;
        pointer.addRoundedRectangle (-1.6f, -radius * 0.72f, 3.2f, radius * 0.33f, 1.6f);
        g.setColour (juce::Colour (0xffffefd0));
        g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));

        auto cap = b.reduced (radius * 0.50f);
        g.setColour (juce::Colour (0xff151b1d));
        g.fillEllipse (cap);
        g.setColour (juce::Colour (0xff465154));
        g.drawEllipse (cap, 0.9f);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& background,
                               bool highlighted, bool down) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced (1.0f);
        auto c = background;
        if (highlighted) c = c.brighter (0.10f);
        if (down) c = c.darker (0.20f);

        g.setColour (juce::Colour (0x85000000));
        g.fillRoundedRectangle (bounds.translated (0.0f, 2.0f), 5.0f);
        g.setColour (c);
        g.fillRoundedRectangle (bounds, 5.0f);
        g.setColour (button.getToggleState() ? juce::Colour (0xffb9974f) : juce::Colour (0xff4a585c));
        g.drawRoundedRectangle (bounds, 5.0f, 1.0f);
    }
};
