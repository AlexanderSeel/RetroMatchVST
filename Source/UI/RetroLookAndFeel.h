#pragma once
#include <JuceHeader.h>

class RetroLookAndFeel : public juce::LookAndFeel_V4
{
public:
    RetroLookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, juce::Colour (0xff080b0d));
        setColour (juce::Slider::textBoxTextColourId, juce::Colour (0xffe0e7e3));
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0xff0a0f11));
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colour (0xff354246));
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
                           float start, float end, juce::Slider& slider) override
    {
        auto available = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
        const float diameter = juce::jmax (30.0f, juce::jmin (available.getWidth(), available.getHeight()));
        auto outer = juce::Rectangle<float> (diameter, diameter).withCentre (available.getCentre());
        const auto centre = outer.getCentre();
        const float radius = diameter * 0.5f;
        const float angle = start + pos * (end - start);
        const bool active = slider.isMouseOverOrDragging();
        const auto amber = juce::Colour (0xffe0b85e);
        const auto cyan = juce::Colour (0xff62cde0);
        const auto ledAccent = active ? cyan : amber;

        // Recessed socket shadow: several soft layers give the control physical depth
        // without relying on platform-specific shadow effects.
        for (int i = 5; i >= 1; --i)
        {
            const float spread = (float) i * 1.25f;
            g.setColour (juce::Colour (0x15000000 + (i * 0x06000000)).withAlpha (0.07f + 0.025f * (float) i));
            g.fillEllipse (outer.expanded (spread).translated (0.0f, 2.0f + spread * 0.24f));
        }

        // Machined bezel.
        auto bezel = outer.reduced (1.5f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff788185),
                                                 bezel.getX(), bezel.getY(),
                                                 juce::Colour (0xff1d2427),
                                                 bezel.getRight(), bezel.getBottom(), false));
        g.fillEllipse (bezel);
        g.setColour (juce::Colour (0xff090c0d));
        g.drawEllipse (bezel, 1.4f);
        g.setColour (juce::Colour (0xff9aa2a4).withAlpha (0.34f));
        g.drawEllipse (bezel.reduced (1.2f), 0.8f);

        // Dark LED trench between bezel and knob body.
        auto trench = bezel.reduced (4.0f);
        g.setColour (juce::Colour (0xff080d0f));
        g.fillEllipse (trench);
        g.setColour (juce::Colour (0xff29373a));
        g.drawEllipse (trench, 1.0f);

        // Segmented LED ring. Unlit segments remain visible like real hardware.
        constexpr int segments = 28;
        const float ledRadius = trench.getWidth() * 0.5f - 2.4f;
        const int litSegments = juce::jlimit (0, segments, (int) std::round (pos * (float) segments));
        for (int i = 0; i < segments; ++i)
        {
            const float t0 = (float) i / (float) segments;
            const float t1 = ((float) i + 0.64f) / (float) segments;
            const float a0 = start + t0 * (end - start);
            const float a1 = start + t1 * (end - start);
            juce::Path segment;
            segment.addCentredArc (centre.x, centre.y, ledRadius, ledRadius, 0.0f, a0, a1, true);
            const bool lit = i < litSegments;
            if (lit && active)
            {
                g.setColour (ledAccent.withAlpha (0.20f));
                g.strokePath (segment, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            g.setColour (lit ? ledAccent : juce::Colour (0xff263335));
            g.strokePath (segment, juce::PathStrokeType (2.25f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // Raised convex knob body.
        auto body = trench.reduced (8.2f);
        g.setColour (juce::Colour (0x80000000));
        g.fillEllipse (body.translated (0.0f, 2.2f));
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff596266),
                                                 body.getX() + body.getWidth() * 0.28f,
                                                 body.getY() + body.getHeight() * 0.20f,
                                                 juce::Colour (0xff121719),
                                                 body.getRight() - body.getWidth() * 0.16f,
                                                 body.getBottom() - body.getHeight() * 0.08f,
                                                 true));
        g.fillEllipse (body);
        g.setColour (juce::Colour (0xff747d80));
        g.drawEllipse (body, 1.15f);
        g.setColour (juce::Colour (0xff050708).withAlpha (0.72f));
        g.drawEllipse (body.reduced (2.2f), 1.1f);

        // Specular reflection along the upper-left quadrant.
        auto highlight = body.reduced (body.getWidth() * 0.14f);
        juce::Path shine;
        shine.addCentredArc (highlight.getCentreX(), highlight.getCentreY(),
                             highlight.getWidth() * 0.5f, highlight.getHeight() * 0.5f,
                             0.0f, -2.55f, -0.75f, true);
        g.setColour (juce::Colour (0xffffffff).withAlpha (active ? 0.20f : 0.12f));
        g.strokePath (shine, juce::PathStrokeType (1.35f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Pointer groove and illuminated insert.
        juce::Path pointerGroove;
        const float bodyRadius = body.getWidth() * 0.5f;
        pointerGroove.addRoundedRectangle (-2.5f, -bodyRadius * 0.82f, 5.0f, bodyRadius * 0.40f, 2.0f);
        const auto transform = juce::AffineTransform::rotation (angle).translated (centre.x, centre.y);
        g.setColour (juce::Colour (0xff050708));
        g.fillPath (pointerGroove, transform);

        juce::Path pointerLight;
        pointerLight.addRoundedRectangle (-1.15f, -bodyRadius * 0.77f, 2.3f, bodyRadius * 0.31f, 1.1f);
        if (active)
        {
            g.setColour (ledAccent.withAlpha (0.20f));
            g.strokePath (pointerLight, juce::PathStrokeType (4.0f), transform);
        }
        g.setColour (juce::Colour (0xffffefd0));
        g.fillPath (pointerLight, transform);

        // Centre cap makes the face read as a real manufactured part.
        auto cap = body.reduced (body.getWidth() * 0.35f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff30383b), cap.getTopLeft(),
                                                 juce::Colour (0xff0d1214), cap.getBottomRight(), false));
        g.fillEllipse (cap);
        g.setColour (juce::Colour (0xff525d60));
        g.drawEllipse (cap, 0.8f);
        auto pin = cap.withSizeKeepingCentre (juce::jmax (2.0f, cap.getWidth() * 0.12f), juce::jmax (2.0f, cap.getHeight() * 0.12f));
        g.setColour (juce::Colour (0xff899396).withAlpha (0.55f));
        g.fillEllipse (pin);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& background,
                               bool highlighted, bool down) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced (1.0f);
        auto c = background;
        if (highlighted) c = c.brighter (0.10f);
        if (down) c = c.darker (0.20f);

        // Raised hardware switch body.
        g.setColour (juce::Colour (0xa0000000));
        g.fillRoundedRectangle (bounds.translated (0.0f, 2.3f), 5.0f);
        g.setGradientFill (juce::ColourGradient (c.brighter (0.08f), bounds.getTopLeft(),
                                                 c.darker (0.18f), bounds.getBottomRight(), false));
        g.fillRoundedRectangle (bounds, 5.0f);
        g.setColour (juce::Colour (0xffffffff).withAlpha (0.08f));
        g.drawLine (bounds.getX() + 5.0f, bounds.getY() + 1.5f, bounds.getRight() - 5.0f, bounds.getY() + 1.5f, 1.0f);

        const bool active = button.getToggleState() || down;
        const auto ledColour = active ? juce::Colour (0xff61d8bd)
                                      : (highlighted ? juce::Colour (0xffe1b65d) : juce::Colour (0xff44514f));
        auto led = juce::Rectangle<float> (bounds.getX() + 10.0f, bounds.getCentreY() - 3.0f, 6.0f, 6.0f);
        if (active || highlighted)
        {
            g.setColour (ledColour.withAlpha (active ? 0.28f : 0.16f));
            g.fillEllipse (led.expanded (5.0f));
            g.setColour (ledColour.withAlpha (active ? 0.18f : 0.10f));
            g.fillEllipse (led.expanded (8.0f));
        }
        g.setColour (juce::Colour (0xff07100e));
        g.fillEllipse (led.expanded (1.5f));
        g.setColour (ledColour);
        g.fillEllipse (led);
        g.setColour (juce::Colour (0xffffffff).withAlpha (0.45f));
        g.fillEllipse (led.withSizeKeepingCentre (2.0f, 2.0f).translated (-1.0f, -1.0f));

        g.setColour (active ? juce::Colour (0xff78bda9) : (highlighted ? juce::Colour (0xff9d844b) : juce::Colour (0xff4a585c)));
        g.drawRoundedRectangle (bounds, 5.0f, active ? 1.5f : 1.0f);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& button,
                         bool highlighted, bool down) override
    {
        auto font = getTextButtonFont (button, button.getHeight());
        font.setHeight (juce::jlimit (9.0f, 13.0f, (float) button.getHeight() * 0.38f));
        font.setBold (true);
        g.setFont (font);

        auto textColour = button.findColour (button.getToggleState() ? juce::TextButton::textColourOnId
                                                                     : juce::TextButton::textColourOffId);
        if (! button.isEnabled()) textColour = textColour.withMultipliedAlpha (0.42f);
        else if (highlighted || down) textColour = textColour.brighter (0.10f);
        g.setColour (textColour);

        const int leftInset = button.getWidth() >= 76 ? 22 : 5;
        g.drawFittedText (button.getButtonText(), button.getLocalBounds().withTrimmedLeft (leftInset).reduced (4, 2),
                          juce::Justification::centred, 1);
    }
};
