#pragma once
#include <JuceHeader.h>

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
        g.fillAll (juce::Colour (0xff08141b));
        auto header = juce::Rectangle<float> (8.0f, 2.0f, (float) getWidth() - 16.0f, 18.0f);
        auto open = openButtonBounds();
        auto r = getLocalBounds().toFloat().reduced (8.0f, 22.0f);
        const double duration = thumbnail.getTotalLength();

        g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
        g.setColour (juce::Colour (0xffa5beca));
        g.drawText (duration > 0.0 ? "DRAG HANDLES / DRAG SELECTION  |  " + juce::String (duration, 2) + " s"
                                   : "LOAD A REFERENCE TO SELECT A REGION",
                    header.withTrimmedRight (open.getWidth() + 10.0f), juce::Justification::centredLeft, true);

        g.setColour (juce::Colour (0xff17282e));
        g.fillRoundedRectangle (open, 4.0f);
        g.setColour (juce::Colour (0xff73d8ff));
        g.drawRoundedRectangle (open, 4.0f, 1.0f);
        g.setFont (juce::Font (juce::FontOptions (8.5f, juce::Font::bold)));
        g.drawText ("OPEN LARGE EDITOR", open, juce::Justification::centred);

        if (duration <= 0.0)
        {
            g.setColour (juce::Colour (0xff58746b));
            g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
            g.drawText ("Reference waveform", r, juce::Justification::centred);
            return;
        }

        g.setColour (juce::Colour (0xff427c91));
        thumbnail.drawChannels (g, r.toNearestInt(), 0.0, duration, 1.0f);
        const float left = timeToX (first, r, duration);
        const float right = timeToX (last, r, duration);
        g.setColour (juce::Colour (0xff73d8ff).withAlpha (0.17f));
        g.fillRect (left, r.getY(), juce::jmax (0.0f, right - left), r.getHeight());
        for (const float x : { left, right })
        {
            g.setColour (juce::Colour (0xff73d8ff));
            g.fillRect (x - 1.0f, r.getY(), 2.0f, r.getHeight());
            g.fillRoundedRectangle (x - 5.0f, r.getY(), 10.0f, 16.0f, 3.0f);
        }
        g.setColour (juce::Colour (0xffb7d7d0));
        g.setFont (juce::Font (juce::FontOptions (9.0f, juce::Font::bold)));
        g.drawText (juce::String (first, 3) + " s", 8, getHeight() - 20, 120, 18, juce::Justification::centredLeft);
        g.drawText (juce::String (last, 3) + " s", getWidth() - 128, getHeight() - 20, 120, 18, juce::Justification::centredRight);
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
        const auto r = getLocalBounds().toFloat().reduced (8.0f, 22.0f);
        const float firstX = timeToX (first, r, duration), lastX = timeToX (last, r, duration);
        if (std::abs (e.position.x - firstX) <= 8.0f) dragging = 1;
        else if (std::abs (e.position.x - lastX) <= 8.0f) dragging = 2;
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
        return { juce::jmax (8.0f, (float) getWidth() - 132.0f), 2.0f, 124.0f, 18.0f };
    }

    static float timeToX (double value, juce::Rectangle<float> r, double duration)
    {
        return r.getX() + r.getWidth() * (float) (juce::jlimit (0.0, duration, value) / juce::jmax (0.001, duration));
    }

    double timeAt (int x) const
    {
        const auto r = getLocalBounds().toFloat().reduced (8.0f, 22.0f);
        return juce::jlimit (0.0, 1.0, (double) (x - r.getX()) / (double) juce::jmax (1.0f, r.getWidth())) * thumbnail.getTotalLength();
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
};
