#pragma once
#include <JuceHeader.h>
#include "RetroLookAndFeel.h"
#include "Hardware3DKit.h"

class ReferenceRegion final : public juce::Component, private juce::ChangeListener
{
public:
    ReferenceRegion() : thumbnail (256, formats, cache)
    {
        formats.registerBasicFormats();
        thumbnail.addChangeListener (this);
        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
        setTitle ("Reference start and end handles. Double-click or use OPEN LARGE EDITOR for detailed editing.");
    }

    ~ReferenceRegion() override { thumbnail.removeChangeListener (this); }

    std::function<void(double, double, bool)> onRegion;
    std::function<void()> onOpenEditor;

    void update (const juce::File& file, double start, double end)
    {
        if (file != source)
        {
            source = file;
            thumbnail.setSource (file.existsAsFile() ? new juce::FileInputSource (file) : nullptr);
        }
        if (dragging == 0) { first = start; last = end; }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto primary = findColour (RetroLookAndFeel::primaryLed);
        const auto secondary = findColour (RetroLookAndFeel::secondaryLed);
        const auto tertiary = findColour (RetroLookAndFeel::tertiaryLed);
        const RetroHardware3D::Palette palette { primary, secondary, tertiary };

        g.fillAll (juce::Colour (0xff071012));
        auto full = getLocalBounds().toFloat().reduced (1.0f);
        RetroHardware3D::drawPhosphorDisplay (g, full, palette);

        auto header = juce::Rectangle<float> (10.0f, 4.0f, (float) getWidth() - 20.0f, 18.0f);
        auto open = openButtonBounds();
        auto r = getLocalBounds().toFloat().reduced (10.0f, 24.0f);
        const double duration = thumbnail.getTotalLength();

        g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
        g.setColour (juce::Colour (0xffc6d8d2));
        g.drawText (duration > 0.0 ? "REFERENCE OVERVIEW  /  DRAG HANDLES OR SELECTION  /  " + juce::String (duration, 2) + " s"
                                   : "LOAD A REFERENCE TO SELECT A REGION",
                    header.withTrimmedRight (open.getWidth() + 10.0f), juce::Justification::centredLeft, true);

        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff283436), open.getTopLeft(),
                                                 juce::Colour (0xff101719), open.getBottomRight(), false));
        g.fillRoundedRectangle (open, 4.0f);
        g.setColour (tertiary.withAlpha (0.95f));
        g.drawRoundedRectangle (open, 4.0f, 1.2f);
        g.setColour (tertiary);
        g.setFont (juce::Font (juce::FontOptions (8.5f, juce::Font::bold)));
        g.drawText ("OPEN LARGE EDITOR", open, juce::Justification::centred);

        if (duration <= 0.0)
        {
            g.setColour (juce::Colour (0xff6c817a));
            g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
            g.drawText ("REFERENCE AUDIO", r, juce::Justification::centred);
            return;
        }

        // Base waveform stays visible but intentionally subdued.
        g.setColour (primary.withAlpha (0.34f));
        thumbnail.drawChannels (g, r.toNearestInt(), 0.0, duration, 1.0f);
        g.setColour (primary.withAlpha (0.08f));
        g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());

        const float left = timeToX (first, r, duration);
        const float right = timeToX (last, r, duration);
        const auto selection = juce::Rectangle<float> (left, r.getY(), juce::jmax (0.0f, right - left), r.getHeight());

        // Strong darkening outside the selection makes the chosen range readable
        // even on very dense/compressed full-song waveforms.
        g.setColour (juce::Colours::black.withAlpha (0.58f));
        if (left > r.getX()) g.fillRect (r.getX(), r.getY(), left - r.getX(), r.getHeight());
        if (right < r.getRight()) g.fillRect (right, r.getY(), r.getRight() - right, r.getHeight());

        g.setGradientFill (juce::ColourGradient (primary.withAlpha (0.30f), selection.getTopLeft(),
                                                 secondary.withAlpha (0.13f), selection.getBottomRight(), false));
        g.fillRect (selection);
        g.setColour (primary.withAlpha (0.95f));
        g.drawRect (selection, 1.4f);

        g.saveState();
        g.reduceClipRegion (selection.toNearestInt());
        g.setColour (primary.brighter (0.55f));
        thumbnail.drawChannels (g, r.toNearestInt(), 0.0, duration, 1.0f);
        g.restoreState();

        for (int i = 0; i < 2; ++i)
        {
            const float x = i == 0 ? left : right;
            const auto colour = i == 0 ? secondary : tertiary;
            g.setColour (colour.withAlpha (0.25f));
            g.fillRect (x - 4.0f, r.getY(), 8.0f, r.getHeight());
            g.setColour (colour);
            g.fillRect (x - 1.5f, r.getY(), 3.0f, r.getHeight());
            g.fillRoundedRectangle (x - 6.5f, r.getY() + 1.0f, 13.0f, 18.0f, 3.0f);
            g.setColour (juce::Colour (0xff101719));
            g.drawVerticalLine ((int) x, r.getY() + 5.0f, r.getY() + 14.0f);
        }

        auto info = juce::Rectangle<float> (r.getX() + 4.0f, r.getBottom() - 21.0f, r.getWidth() - 8.0f, 18.0f);
        g.setColour (juce::Colour (0xd5091113));
        g.fillRoundedRectangle (info, 4.0f);
        g.setColour (secondary);
        g.setFont (juce::Font (juce::FontOptions (9.0f, juce::Font::bold)));
        g.drawText ("START  " + juce::String (first, 3) + " s", info.removeFromLeft (150.0f), juce::Justification::centredLeft);
        g.setColour (primary);
        g.drawText ("SELECTED  " + juce::String (juce::jmax (0.0, last - first), 3) + " s", info, juce::Justification::centred);
        g.setColour (tertiary);
        g.drawText ("END  " + juce::String (last, 3) + " s", info.removeFromRight (150.0f), juce::Justification::centredRight);
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        if (onOpenEditor) onOpenEditor();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (openButtonBounds().contains (e.position)) { dragging = 0; return; }
        const auto duration = thumbnail.getTotalLength();
        if (duration <= 0.0) return;
        const auto time = timeAt (e.x);
        const auto r = getLocalBounds().toFloat().reduced (10.0f, 24.0f);
        const float firstX = timeToX (first, r, duration), lastX = timeToX (last, r, duration);
        if (std::abs (e.position.x - firstX) <= 9.0f) dragging = 1;
        else if (std::abs (e.position.x - lastX) <= 9.0f) dragging = 2;
        else if (e.position.x > firstX && e.position.x < lastX)
        {
            dragging = 3;
            dragAnchorTime = time;
            dragAnchorFirst = first;
            dragAnchorLast = last;
        }
        else dragging = std::abs (time - first) <= std::abs (time - last) ? 1 : 2;
        mouseDrag (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging == 0) return;
        const double duration = thumbnail.getTotalLength();
        const double gap = juce::jmin (0.002, duration);
        if (dragging == 1)
            first = juce::jlimit (0.0, juce::jmax (0.0, last - gap), timeAt (e.x));
        else if (dragging == 2)
            last = juce::jlimit (juce::jmin (duration, first + gap), duration, timeAt (e.x));
        else
        {
            const double length = dragAnchorLast - dragAnchorFirst;
            const double delta = timeAt (e.x) - dragAnchorTime;
            first = juce::jlimit (0.0, juce::jmax (0.0, duration - length), dragAnchorFirst + delta);
            last = juce::jmin (duration, first + length);
        }
        if (onRegion) onRegion (first, last, false);
        repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (openButtonBounds().contains (e.position))
        {
            dragging = 0;
            if (onOpenEditor) onOpenEditor();
            return;
        }
        if (dragging != 0 && onRegion) onRegion (first, last, true);
        dragging = 0;
    }

private:
    juce::AudioFormatManager formats;
    juce::AudioThumbnailCache cache { 2 };
    juce::AudioThumbnail thumbnail;
    juce::File source;
    double first = 0.0, last = 0.0;
    double dragAnchorTime = 0.0, dragAnchorFirst = 0.0, dragAnchorLast = 0.0;
    int dragging = 0;

    juce::Rectangle<float> openButtonBounds() const
    {
        return { juce::jmax (10.0f, (float) getWidth() - 140.0f), 3.0f, 130.0f, 18.0f };
    }

    static float timeToX (double value, juce::Rectangle<float> r, double duration)
    {
        return r.getX() + r.getWidth() * (float) (juce::jlimit (0.0, duration, value) / juce::jmax (0.001, duration));
    }

    double timeAt (int x) const
    {
        const auto r = getLocalBounds().toFloat().reduced (10.0f, 24.0f);
        return juce::jlimit (0.0, 1.0, (double) (x - r.getX()) / (double) juce::jmax (1.0f, r.getWidth())) * thumbnail.getTotalLength();
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
};