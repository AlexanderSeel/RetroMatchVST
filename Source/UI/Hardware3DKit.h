#pragma once
#include <JuceHeader.h>
#include <cmath>
#include <algorithm>

namespace RetroHardware3D
{
struct Palette
{
    juce::Colour primary;
    juce::Colour secondary;
    juce::Colour tertiary;
};

inline void drawSoftEllipseShadow (juce::Graphics& g, juce::Rectangle<float> bounds,
                                   float yOffset, int layers = 6)
{
    for (int i = layers; i >= 1; --i)
    {
        const float spread = (float) i * 1.35f;
        g.setColour (juce::Colours::black.withAlpha (0.045f + 0.024f * (float) i));
        g.fillEllipse (bounds.expanded (spread).translated (0.0f, yOffset + spread * 0.28f));
    }
}

inline void drawSoftRoundedShadow (juce::Graphics& g, juce::Rectangle<float> bounds,
                                   float radius, float yOffset, int layers = 5)
{
    for (int i = layers; i >= 1; --i)
    {
        const float spread = (float) i * 1.15f;
        g.setColour (juce::Colours::black.withAlpha (0.045f + 0.028f * (float) i));
        g.fillRoundedRectangle (bounds.expanded (spread).translated (0.0f, yOffset + spread * 0.25f),
                                radius + spread * 0.45f);
    }
}

inline void drawMachinedScrew (juce::Graphics& g, juce::Rectangle<float> bounds, float angle = -0.72f)
{
    drawSoftEllipseShadow (g, bounds.reduced (2.0f), 2.5f, 3);
    auto head = bounds.reduced (2.5f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff9aa2a4), head.getTopLeft(),
                                             juce::Colour (0xff252c2e), head.getBottomRight(), false));
    g.fillEllipse (head);
    g.setColour (juce::Colour (0xff090c0d));
    g.drawEllipse (head, 1.25f);
    g.setColour (juce::Colours::white.withAlpha (0.22f));
    g.drawEllipse (head.reduced (1.0f), 0.8f);

    const auto c = head.getCentre();
    const float r = head.getWidth() * 0.26f;
    const auto dx = std::cos (angle) * r;
    const auto dy = std::sin (angle) * r;
    g.setColour (juce::Colour (0xff111617));
    g.drawLine (c.x - dx, c.y - dy, c.x + dx, c.y + dy, 2.2f);
    g.setColour (juce::Colours::white.withAlpha (0.16f));
    g.drawLine (c.x - dx, c.y - dy - 0.8f, c.x + dx, c.y + dy - 0.8f, 0.8f);
}

inline void drawWalnutCheek (juce::Graphics& g, juce::Rectangle<float> bounds, bool rightSide)
{
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff75513b), bounds.getTopLeft(),
                                             juce::Colour (0xff2b1d18), bounds.getBottomRight(), false));
    g.fillRoundedRectangle (bounds, 4.0f);

    for (int i = 0; i < 18; ++i)
    {
        const float x = bounds.getX() + 3.0f + (float) i * bounds.getWidth() / 18.0f;
        juce::Path grain;
        grain.startNewSubPath (x, bounds.getY());
        for (int y = 0; y <= (int) bounds.getHeight(); y += 16)
        {
            const float wobble = std::sin ((float) y * 0.035f + (float) i * 0.71f) * (1.0f + (float) (i % 3));
            grain.lineTo (x + wobble, bounds.getY() + (float) y);
        }
        g.setColour ((i % 3 == 0 ? juce::Colour (0xffd1a078) : juce::Colour (0xff120d0b))
                         .withAlpha (i % 3 == 0 ? 0.10f : 0.18f));
        g.strokePath (grain, juce::PathStrokeType (i % 4 == 0 ? 1.4f : 0.8f));
    }

    g.setColour (juce::Colours::white.withAlpha (rightSide ? 0.035f : 0.10f));
    g.drawVerticalLine ((int) (rightSide ? bounds.getRight() - 2.0f : bounds.getX() + 2.0f),
                        bounds.getY() + 5.0f, bounds.getBottom() - 5.0f);
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.0f);
}

inline void drawRecessedPanel (juce::Graphics& g, juce::Rectangle<float> bounds,
                               const Palette& palette, float radius = 9.0f)
{
    drawSoftRoundedShadow (g, bounds.reduced (1.5f), radius, 4.0f, 4);

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff30383a), bounds.getTopLeft(),
                                             juce::Colour (0xff0b0f11), bounds.getBottomRight(), false));
    g.fillRoundedRectangle (bounds, radius);

    auto inset = bounds.reduced (4.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0a0e10), inset.getTopLeft(),
                                             juce::Colour (0xff1a2224), inset.getBottomRight(), false));
    g.fillRoundedRectangle (inset, juce::jmax (2.0f, radius - 2.0f));

    g.setColour (juce::Colours::black.withAlpha (0.92f));
    g.drawRoundedRectangle (inset, juce::jmax (2.0f, radius - 2.0f), 2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.085f));
    g.drawLine (inset.getX() + 8.0f, inset.getY() + 1.0f,
                inset.getRight() - 8.0f, inset.getY() + 1.0f, 1.0f);
    g.setColour (palette.primary.withAlpha (0.06f));
    g.drawRoundedRectangle (inset.reduced (2.0f), juce::jmax (2.0f, radius - 3.0f), 1.0f);
}

inline void drawSectionPlate (juce::Graphics& g, juce::Rectangle<float> bounds,
                              const Palette& palette)
{
    drawSoftRoundedShadow (g, bounds.reduced (1.0f), 5.0f, 2.5f, 3);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2c3436), bounds.getTopLeft(),
                                             juce::Colour (0xff101719), bounds.getBottomLeft(), false));
    g.fillRoundedRectangle (bounds, 5.0f);
    g.setColour (juce::Colours::black.withAlpha (0.72f));
    g.drawRoundedRectangle (bounds.reduced (1.0f), 4.0f, 1.5f);
    g.setColour (juce::Colours::white.withAlpha (0.09f));
    g.drawLine (bounds.getX() + 7.0f, bounds.getY() + 1.0f,
                bounds.getRight() - 7.0f, bounds.getY() + 1.0f, 1.0f);
    g.setColour (palette.secondary.withAlpha (0.16f));
    g.drawLine (bounds.getX() + 8.0f, bounds.getBottom() - 2.0f,
                bounds.getRight() - 8.0f, bounds.getBottom() - 2.0f, 1.0f);
}

inline void drawPhosphorDisplay (juce::Graphics& g, juce::Rectangle<float> bounds,
                                 const Palette& palette)
{
    drawSoftRoundedShadow (g, bounds, 8.0f, 5.0f, 5);

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff697174), bounds.getTopLeft(),
                                             juce::Colour (0xff171c1e), bounds.getBottomRight(), false));
    g.fillRoundedRectangle (bounds, 8.0f);

    auto glass = bounds.reduced (5.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff061411), glass.getTopLeft(),
                                             juce::Colour (0xff020809), glass.getBottomRight(), false));
    g.fillRoundedRectangle (glass, 6.0f);

    for (int y = (int) glass.getY() + 3; y < (int) glass.getBottom(); y += 4)
    {
        g.setColour (juce::Colours::black.withAlpha (0.12f));
        g.drawHorizontalLine (y, glass.getX() + 2.0f, glass.getRight() - 2.0f);
    }

    g.setColour (palette.primary.withAlpha (0.08f));
    g.fillRoundedRectangle (glass.reduced (2.0f), 5.0f);
    g.setColour (palette.primary.withAlpha (0.25f));
    g.drawRoundedRectangle (glass.reduced (1.5f), 5.0f, 1.0f);

    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawLine (glass.getX() + 10.0f, glass.getY() + 2.0f,
                glass.getRight() - 10.0f, glass.getY() + 2.0f, 1.0f);
}

inline void drawRotaryKnob (juce::Graphics& g, juce::Rectangle<float> available, float pos,
                            float startAngle, float endAngle, const Palette& palette,
                            bool active)
{
    const float diameter = juce::jmax (30.0f, juce::jmin (available.getWidth(), available.getHeight()));
    auto outer = juce::Rectangle<float> (diameter, diameter).withCentre (available.getCentre());
    const auto centre = outer.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);
    const auto ledAccent = active ? palette.secondary : palette.primary;

    drawSoftEllipseShadow (g, outer.reduced (2.0f), 5.5f, 7);

    auto socket = outer.reduced (0.8f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff9ca3a5), socket.getTopLeft(),
                                             juce::Colour (0xff1b2224), socket.getBottomRight(), false));
    g.fillEllipse (socket);
    g.setColour (juce::Colour (0xff050708));
    g.drawEllipse (socket, 1.8f);
    g.setColour (juce::Colours::white.withAlpha (0.20f));
    g.drawEllipse (socket.reduced (1.4f), 0.9f);

    auto bevel = socket.reduced (4.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff596366), bevel.getTopLeft(),
                                             juce::Colour (0xff0d1112), bevel.getBottomRight(), false));
    g.fillEllipse (bevel);

    auto trench = bevel.reduced (3.7f);
    g.setColour (juce::Colour (0xff050a0b));
    g.fillEllipse (trench);
    g.setColour (juce::Colour (0xff314044));
    g.drawEllipse (trench, 1.0f);

    constexpr int segments = 30;
    const float ledRadius = trench.getWidth() * 0.5f - 2.5f;
    const int lit = juce::jlimit (0, segments, (int) std::lround (pos * (float) segments));

    for (int i = 0; i < segments; ++i)
    {
        const float t0 = (float) i / (float) segments;
        const float t1 = ((float) i + 0.62f) / (float) segments;
        juce::Path segment;
        segment.addCentredArc (centre.x, centre.y, ledRadius, ledRadius, 0.0f,
                               startAngle + t0 * (endAngle - startAngle),
                               startAngle + t1 * (endAngle - startAngle), true);

        if (i < lit)
        {
            g.setColour (ledAccent.withAlpha (0.18f));
            g.strokePath (segment, juce::PathStrokeType (5.8f, juce::PathStrokeType::curved,
                                                         juce::PathStrokeType::rounded));
        }

        g.setColour (i < lit ? ledAccent : juce::Colour (0xff253234));
        g.strokePath (segment, juce::PathStrokeType (2.15f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
    }

    auto body = trench.reduced (8.0f);
    g.setColour (juce::Colours::black.withAlpha (0.75f));
    g.fillEllipse (body.translated (0.0f, 4.0f).expanded (1.0f));

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff899194),
                                             body.getX() + body.getWidth() * 0.24f,
                                             body.getY() + body.getHeight() * 0.18f,
                                             juce::Colour (0xff121719),
                                             body.getRight() - body.getWidth() * 0.12f,
                                             body.getBottom(), true));
    g.fillEllipse (body);

    for (int i = 0; i < 48; ++i)
    {
        const float a = (float) i * juce::MathConstants<float>::twoPi / 48.0f;
        const float outerRadius = body.getWidth() * 0.49f;
        const float innerRadius = body.getWidth() * 0.43f;
        const auto light = std::sin (a - 0.65f) < 0.0f ? 0.13f : 0.035f;
        g.setColour (juce::Colours::white.withAlpha (light));
        g.drawLine (centre.x + std::sin (a) * outerRadius,
                    centre.y + std::cos (a) * outerRadius,
                    centre.x + std::sin (a) * innerRadius,
                    centre.y + std::cos (a) * innerRadius, 0.8f);
    }

    g.setColour (juce::Colour (0xff939b9d).withAlpha (0.70f));
    g.drawEllipse (body, 1.25f);
    g.setColour (juce::Colour (0xff050708).withAlpha (0.86f));
    g.drawEllipse (body.reduced (2.1f), 1.3f);

    auto face = body.reduced (body.getWidth() * 0.13f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff677174),
                                             face.getX() + face.getWidth() * 0.28f,
                                             face.getY() + face.getHeight() * 0.22f,
                                             juce::Colour (0xff0d1214),
                                             face.getRight(), face.getBottom(), true));
    g.fillEllipse (face);

    auto highlight = face.reduced (face.getWidth() * 0.08f);
    juce::Path shine;
    shine.addCentredArc (highlight.getCentreX(), highlight.getCentreY(),
                         highlight.getWidth() * 0.5f, highlight.getHeight() * 0.5f,
                         0.0f, -2.65f, -0.62f, true);
    g.setColour (juce::Colours::white.withAlpha (active ? 0.25f : 0.16f));
    g.strokePath (shine, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    const float bodyRadius = face.getWidth() * 0.5f;
    juce::Path pointerGroove;
    pointerGroove.addRoundedRectangle (-2.8f, -bodyRadius * 0.90f, 5.6f, bodyRadius * 0.49f, 2.0f);
    const auto transform = juce::AffineTransform::rotation (angle).translated (centre.x, centre.y);
    g.setColour (juce::Colour (0xff020405));
    g.fillPath (pointerGroove, transform);

    juce::Path pointer;
    pointer.addRoundedRectangle (-1.15f, -bodyRadius * 0.85f, 2.3f, bodyRadius * 0.38f, 1.1f);
    if (active)
    {
        g.setColour (ledAccent.withAlpha (0.24f));
        g.strokePath (pointer, juce::PathStrokeType (5.2f), transform);
    }
    g.setColour (juce::Colour (0xffffefd0));
    g.fillPath (pointer, transform);

    auto cap = face.reduced (face.getWidth() * 0.35f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff374044), cap.getTopLeft(),
                                             juce::Colour (0xff080c0d), cap.getBottomRight(), false));
    g.fillEllipse (cap);
    g.setColour (juce::Colour (0xff667174));
    g.drawEllipse (cap, 0.9f);
    g.setColour (juce::Colours::white.withAlpha (0.38f));
    g.fillEllipse (cap.withSizeKeepingCentre (juce::jmax (2.0f, cap.getWidth() * 0.11f),
                                              juce::jmax (2.0f, cap.getHeight() * 0.11f))
                      .translated (-0.8f, -1.0f));
}

inline void drawRaisedButton (juce::Graphics& g, juce::Rectangle<float> bounds,
                              const Palette& palette, bool toggled,
                              bool highlighted, bool down, bool enabled)
{
    const float radius = 5.5f;
    drawSoftRoundedShadow (g, bounds.reduced (1.0f), radius, down ? 2.0f : 4.5f, 5);

    auto socket = bounds.reduced (0.5f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff5b6467), socket.getTopLeft(),
                                             juce::Colour (0xff0b0f10), socket.getBottomRight(), false));
    g.fillRoundedRectangle (socket, radius);
    g.setColour (juce::Colour (0xff040607));
    g.drawRoundedRectangle (socket, radius, 1.4f);

    auto face = socket.reduced (2.5f).translated (0.0f, down ? 1.8f : 0.0f);
    const auto top = toggled ? juce::Colour (0xff26423d) : juce::Colour (0xff3c4649);
    const auto bottom = toggled ? juce::Colour (0xff0d211e) : juce::Colour (0xff121719);
    g.setGradientFill (juce::ColourGradient (top, face.getTopLeft(), bottom, face.getBottomLeft(), false));
    g.fillRoundedRectangle (face, juce::jmax (2.0f, radius - 2.0f));

    if (! down)
    {
        g.setColour (juce::Colours::black.withAlpha (0.65f));
        g.fillRoundedRectangle (face.withTop (face.getBottom() - 3.0f), 2.0f);
    }

    g.setColour (juce::Colours::white.withAlpha (down ? 0.045f : 0.14f));
    g.drawLine (face.getX() + 7.0f, face.getY() + 1.0f,
                face.getRight() - 7.0f, face.getY() + 1.0f, 1.0f);

    const auto accent = toggled ? palette.primary : palette.secondary;
    g.setColour (accent.withAlpha (toggled ? 0.74f : highlighted ? 0.34f : 0.13f));
    g.drawRoundedRectangle (face.reduced (0.5f), juce::jmax (2.0f, radius - 2.0f),
                            toggled ? 1.35f : 0.8f);

    if (toggled || highlighted)
    {
        g.setColour (accent.withAlpha (toggled ? 0.10f : 0.05f));
        g.fillRoundedRectangle (face.reduced (2.0f), juce::jmax (2.0f, radius - 3.0f));
    }

    if (! enabled)
    {
        g.setColour (juce::Colour (0x8c050809));
        g.fillRoundedRectangle (socket, radius);
    }
}

inline void drawLinearTrack (juce::Graphics& g, juce::Rectangle<float> area,
                             float sliderPos, const Palette& palette, bool active)
{
    auto track = area.reduced (4.0f, juce::jmax (3.0f, area.getHeight() * 0.34f));

    g.setColour (juce::Colours::black.withAlpha (0.78f));
    g.fillRoundedRectangle (track.translated (0.0f, 2.2f).expanded (1.0f), track.getHeight() * 0.5f);

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff020506), track.getTopLeft(),
                                             juce::Colour (0xff303c3f), track.getBottomLeft(), false));
    g.fillRoundedRectangle (track, track.getHeight() * 0.5f);
    g.setColour (juce::Colour (0xff59676a));
    g.drawRoundedRectangle (track, track.getHeight() * 0.5f, 1.0f);

    auto activeTrack = track.withRight (juce::jlimit (track.getX(), track.getRight(), sliderPos));
    g.setColour (palette.primary.withAlpha (0.16f));
    g.fillRoundedRectangle (activeTrack.expanded (0.0f, 2.0f), track.getHeight() * 0.5f);
    g.setColour (palette.primary);
    g.fillRoundedRectangle (activeTrack.reduced (0.0f, track.getHeight() * 0.30f), 2.0f);

    const float thumbDiameter = juce::jlimit (11.0f, 19.0f, area.getHeight() * 0.58f);
    auto thumb = juce::Rectangle<float> (thumbDiameter, thumbDiameter)
                     .withCentre ({ sliderPos, track.getCentreY() });

    drawSoftEllipseShadow (g, thumb, 3.0f, 3);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffa2a9aa), thumb.getTopLeft(),
                                             juce::Colour (0xff202729), thumb.getBottomRight(), false));
    g.fillEllipse (thumb);
    g.setColour (juce::Colour (0xff070a0b));
    g.drawEllipse (thumb, 1.4f);
    g.setColour (palette.primary.withAlpha (active ? 0.95f : 0.46f));
    g.drawEllipse (thumb.reduced (2.4f), 1.25f);
    juce::Path thumbShine;
    auto shineBounds = thumb.reduced (3.2f);
    thumbShine.addCentredArc (shineBounds.getCentreX(), shineBounds.getCentreY(),
                              shineBounds.getWidth() * 0.5f, shineBounds.getHeight() * 0.5f,
                              0.0f, -2.55f, -0.65f, true);
    g.setColour (juce::Colours::white.withAlpha (0.18f));
    g.strokePath (thumbShine, juce::PathStrokeType (1.0f));
}

inline void drawRocker (juce::Graphics& g, juce::Rectangle<float> bounds,
                        const Palette& palette, bool on, bool highlighted, bool down)
{
    drawSoftRoundedShadow (g, bounds.reduced (1.0f), 5.0f, 3.5f, 4);

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff5b6466), bounds.getTopLeft(),
                                             juce::Colour (0xff101516), bounds.getBottomRight(), false));
    g.fillRoundedRectangle (bounds, 5.0f);

    auto socket = bounds.reduced (2.2f);
    g.setColour (juce::Colour (0xff050809));
    g.fillRoundedRectangle (socket, 4.0f);

    const float half = socket.getWidth() * 0.5f;
    auto paddle = juce::Rectangle<float> (on ? socket.getCentreX() : socket.getX(),
                                          socket.getY(), half, socket.getHeight())
                      .reduced (1.6f)
                      .translated (0.0f, down ? 1.1f : 0.0f);

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff828b8d), paddle.getTopLeft(),
                                             juce::Colour (0xff202729), paddle.getBottomRight(), false));
    g.fillRoundedRectangle (paddle, 3.0f);

    if (on)
    {
        g.setColour (palette.primary.withAlpha (highlighted ? 0.30f : 0.20f));
        g.fillRoundedRectangle (paddle.reduced (1.5f), 2.0f);
        g.setColour (palette.primary);
        g.fillEllipse (paddle.getRight() - 9.0f, paddle.getCentreY() - 2.5f, 5.0f, 5.0f);
    }

    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawLine (paddle.getX() + 4.0f, paddle.getY() + 1.0f,
                paddle.getRight() - 4.0f, paddle.getY() + 1.0f, 1.0f);
}
}
