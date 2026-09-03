#pragma once
#include <JuceHeader.h>

class RetroLookAndFeel : public juce::LookAndFeel_V4
{
public:
    RetroLookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, juce::Colour (0xff0b0e10));
        setColour (juce::Slider::textBoxTextColourId, juce::Colour (0xffdce5dc));
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0xff111719));
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colour (0xff344248));
        setColour (juce::TextButton::buttonColourId, juce::Colour (0xff20282b));
        setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff36564e));
        setColour (juce::TextButton::textColourOffId, juce::Colour (0xffd6c08b));
        setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff12191b));
        setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff3d4a4e));
        setColour (juce::ComboBox::textColourId, juce::Colour (0xffd8e2da));
        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff121719));
        setColour (juce::PopupMenu::textColourId, juce::Colour (0xffdde5de));
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float start, float end, juce::Slider&) override
    {
        auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (9.0f);
        const auto radius = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
        const auto centre = b.getCentre();
        const float angle = start + pos * (end - start);

        g.setColour (juce::Colour (0xff050708));
        g.fillEllipse (b.translated (1.5f, 2.5f));
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff343b3e), centre.x, b.getY(),
                                                 juce::Colour (0xff0c0f11), centre.x, b.getBottom(), false));
        g.fillEllipse (b);
        g.setColour (juce::Colour (0xff586266));
        g.drawEllipse (b, 1.25f);

        auto arcBounds = b.reduced (3.5f);
        juce::Path backgroundArc;
        backgroundArc.addCentredArc (arcBounds.getCentreX(), arcBounds.getCentreY(),
                                     arcBounds.getWidth() * 0.5f, arcBounds.getHeight() * 0.5f,
                                     0.0f, start, end, true);
        g.setColour (juce::Colour (0xff171d20));
        g.strokePath (backgroundArc, juce::PathStrokeType (3.2f));

        juce::Path valueArc;
        valueArc.addCentredArc (arcBounds.getCentreX(), arcBounds.getCentreY(),
                                arcBounds.getWidth() * 0.5f, arcBounds.getHeight() * 0.5f,
                                0.0f, start, angle, true);
        g.setColour (juce::Colour (0xffc9a45d));
        g.strokePath (valueArc, juce::PathStrokeType (3.2f));

        juce::Path pointer;
        pointer.addRoundedRectangle (-1.5f, -radius + 7.0f, 3.0f, radius * 0.40f, 1.5f);
        g.setColour (juce::Colour (0xfff0e5c7));
        g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));

        g.setColour (juce::Colour (0xff171c1e));
        g.fillEllipse (b.reduced (radius * 0.48f));
        g.setColour (juce::Colour (0xff3f474a));
        g.drawEllipse (b.reduced (radius * 0.48f), 0.8f);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& background,
                               bool highlighted, bool down) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced (1.0f);
        auto c = background;
        if (highlighted) c = c.brighter (0.08f);
        if (down) c = c.darker (0.18f);
        g.setColour (juce::Colour (0xff07090a));
        g.fillRoundedRectangle (bounds.translated (0, 2), 4.0f);
        g.setColour (c);
        g.fillRoundedRectangle (bounds, 4.0f);
        g.setColour (juce::Colour (0xff586367));
        g.drawRoundedRectangle (bounds, 4.0f, 1.0f);
    }
};
