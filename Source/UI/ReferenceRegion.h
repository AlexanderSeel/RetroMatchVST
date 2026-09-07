#pragma once
#include <JuceHeader.h>

class ReferenceRegion final : public juce::Component, private juce::ChangeListener
{
public:
    ReferenceRegion() : thumbnail (256, formats, cache)
    {
        formats.registerBasicFormats(); thumbnail.addChangeListener (this);
        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
        setTitle ("Reference start and end handles");
    }
    ~ReferenceRegion() override { thumbnail.removeChangeListener (this); }
    std::function<void(double, double, bool)> onRegion;
    void update (const juce::File& file, double start, double end)
    {
        if (file != source) { source = file; thumbnail.setSource (file.existsAsFile() ? new juce::FileInputSource (file) : nullptr); }
        if (dragging == 0) { first = start; last = end; }
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (8, 22);
        g.fillAll (juce::Colour (0xff08141b));
        const double duration = thumbnail.getTotalLength();
        g.setFont (12); g.setColour (juce::Colour (0xffa5beca));
        if (duration <= 0) { g.drawText ("Load a reference to select a region", getLocalBounds(), juce::Justification::centred); return; }
        g.drawText ("DRAG START / END  |  " + juce::String (duration, 2) + " s", 8, 2, getWidth() - 16, 18, juce::Justification::centredLeft);
        g.setColour (juce::Colour (0xff427c91)); thumbnail.drawChannels (g, r.toNearestInt(), 0, duration, 1);
        const float left = r.getX() + r.getWidth() * (float) (first / duration);
        const float right = r.getX() + r.getWidth() * (float) (last / duration);
        g.setColour (juce::Colour (0xff73d8ff).withAlpha (0.17f)); g.fillRect (left, r.getY(), juce::jmax (0.0f, right - left), r.getHeight());
        for (const float x : { left, right })
        {
            g.setColour (juce::Colour (0xff73d8ff)); g.fillRect (x - 1, r.getY(), 2.0f, r.getHeight());
            g.fillRoundedRectangle (x - 5, r.getY(), 10, 16, 3);
        }
        g.drawText (juce::String (first, 3) + " s", 8, getHeight() - 20, 120, 18, juce::Justification::centredLeft);
        g.drawText (juce::String (last, 3) + " s", getWidth() - 128, getHeight() - 20, 120, 18, juce::Justification::centredRight);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto duration = thumbnail.getTotalLength(); if (duration <= 0) return;
        const auto time = timeAt (e.x);
        dragging = std::abs (time - first) <= std::abs (time - last) ? 1 : 2;
        mouseDrag (e);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging == 0) return;
        const double duration = thumbnail.getTotalLength(), gap = juce::jmin (0.002, duration);
        if (dragging == 1) first = juce::jlimit (0.0, juce::jmax (0.0, last - gap), timeAt (e.x));
        else last = juce::jlimit (juce::jmin (duration, first + gap), duration, timeAt (e.x));
        if (onRegion) onRegion (first, last, false); repaint();
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging != 0 && onRegion) onRegion (first, last, true);
        dragging = 0;
    }
private:
    juce::AudioFormatManager formats; juce::AudioThumbnailCache cache { 2 }; juce::AudioThumbnail thumbnail;
    juce::File source; double first = 0, last = 0; int dragging = 0;
    double timeAt (int x) const { return juce::jlimit (0.0, 1.0, (x - 8.0) / juce::jmax (1, getWidth() - 16)) * thumbnail.getTotalLength(); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
};
