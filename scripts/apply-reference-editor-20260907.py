from pathlib import Path
import re

ROOT = Path('.')


def read(path):
    return (ROOT / path).read_text(encoding='utf-8')


def write(path, text):
    p = ROOT / path
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(text, encoding='utf-8')


def must_replace(path, old, new, count=1):
    text = read(path)
    found = text.count(old)
    if found < count:
        raise RuntimeError(f'{path}: expected at least {count} occurrence(s), found {found}: {old[:100]!r}')
    text = text.replace(old, new, count)
    write(path, text)


def replace_between(path, start_marker, end_marker, replacement):
    text = read(path)
    start = text.find(start_marker)
    if start < 0:
        raise RuntimeError(f'{path}: start marker not found: {start_marker}')
    end = text.find(end_marker, start)
    if end < 0:
        raise RuntimeError(f'{path}: end marker not found: {end_marker}')
    write(path, text[:start] + replacement.rstrip() + '\n\n' + text[end:])


reference_region = r'''#pragma once
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
        return juce::jlimit (0.0, 1.0, (x - r.getX()) / juce::jmax (1.0f, r.getWidth())) * thumbnail.getTotalLength();
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
};
'''
write('Source/UI/ReferenceRegion.h', reference_region)

reference_editor = r'''#pragma once
#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "RetroLookAndFeel.h"
#include "Hardware3DKit.h"

class ReferenceEditorDialog final : public juce::Component,
                                    private juce::Timer,
                                    private juce::ChangeListener
{
public:
    explicit ReferenceEditorDialog (RetroMatchSynthAudioProcessor& p)
        : proc (p), thumbnail (1024, formats, cache), waveform (*this), previousAuditionMode (p.getReferenceAuditionMode())
    {
        formats.registerBasicFormats();
        thumbnail.addChangeListener (this);
        const auto file = proc.getReferenceFile();
        if (file.existsAsFile()) thumbnail.setSource (new juce::FileInputSource (file));

        addAndMakeVisible (waveform);
        for (auto* c : std::array<juce::Component*, 18> {
                 &startLabel, &startTime, &endLabel, &endTime,
                 &zoomLabel, &zoom, &panLabel, &pan,
                 &normalize, &fadeInLabel, &fadeIn, &fadeOutLabel, &fadeOut,
                 &fullSelection, &fitSelection, &preview, &stopPreview, &status })
            addAndMakeVisible (*c);
        for (auto* b : { &applyResynth, &useForMidi, &cutToNewReference }) addAndMakeVisible (*b);

        setupTimeSlider (startTime);
        setupTimeSlider (endTime);
        setupFadeSlider (fadeIn);
        setupFadeSlider (fadeOut);
        startLabel.setText ("START", juce::dontSendNotification);
        endLabel.setText ("END", juce::dontSendNotification);
        zoomLabel.setText ("ZOOM", juce::dontSendNotification);
        panLabel.setText ("PAN", juce::dontSendNotification);
        fadeInLabel.setText ("FADE IN", juce::dontSendNotification);
        fadeOutLabel.setText ("FADE OUT", juce::dontSendNotification);
        for (auto* label : { &startLabel, &endLabel, &zoomLabel, &panLabel, &fadeInLabel, &fadeOutLabel })
        {
            label->setColour (juce::Label::textColourId, juce::Colour (0xffaec2bc));
            label->setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
            label->setJustificationType (juce::Justification::centredLeft);
        }

        zoom.setSliderStyle (juce::Slider::LinearHorizontal);
        zoom.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 22);
        zoom.setRange (1.0, 100.0, 0.01);
        zoom.setSkewFactorFromMidPoint (8.0);
        zoom.setValue (1.0, juce::dontSendNotification);
        zoom.setTextValueSuffix (" x");
        pan.setSliderStyle (juce::Slider::LinearHorizontal);
        pan.setTextBoxStyle (juce::Slider::TextBoxRight, false, 58, 22);
        pan.setRange (0.0, 1.0, 0.001);
        pan.setValue (0.0, juce::dontSendNotification);

        normalize.setTooltip ("Normalize the selected audio to -1 dBFS for preview and when creating a new cut reference.");
        fadeIn.setTooltip ("Non-destructive preview/export fade at the start of the selected range.");
        fadeOut.setTooltip ("Non-destructive preview/export fade at the end of the selected range.");
        fullSelection.setTooltip ("Select the complete source file. This does not force the resynth analyzer to use the complete song.");
        applyResynth.setTooltip ("Use this exact range for timbre/pitch analysis, reference wavetable extraction and resynthesis matching.");
        useForMidi.setTooltip ("Use this range for Melody Lab transcription. Long ranges are processed in chunks, so a full song can be transcribed.");
        cutToNewReference.setTooltip ("Render the selected range, including normalization and fades, to a WAV file and load that file as the new reference.");

        startTime.onValueChange = [this] { setSelection (startTime.getValue(), selectionEnd, false); };
        endTime.onValueChange = [this] { setSelection (selectionStart, endTime.getValue(), false); };
        zoom.onValueChange = [this] { updateView(); waveform.repaint(); };
        pan.onValueChange = [this] { updateView(); waveform.repaint(); };
        fullSelection.onClick = [this]
        {
            if (duration > 0.0) { setSelection (0.0, duration, true); zoom.setValue (1.0); pan.setValue (0.0); }
        };
        fitSelection.onClick = [this] { fitSelectionInView(); };
        preview.onClick = [this]
        {
            if (selectionLength() <= 0.001) return;
            const bool limited = selectionLength() > 180.0;
            const bool ok = proc.previewReferenceRegion ((float) selectionStart, (float) selectionEnd,
                                                         normalize.getToggleState(), (float) fadeIn.getValue(), (float) fadeOut.getValue());
            status.setText (ok ? (limited ? "Previewing the first 180 s of the selection. Narrow the range to hear its exact end."
                                                 : "Previewing selected range through the plug-in output.")
                               : "Could not preview the selected range.",
                            juce::dontSendNotification);
        };
        stopPreview.onClick = [this]
        {
            proc.stopReferencePreview();
            proc.setReferenceAuditionMode (previousAuditionMode);
            status.setText ("Preview stopped.", juce::dontSendNotification);
        };
        applyResynth.onClick = [this]
        {
            proc.stopReferencePreview();
            if (proc.setReferenceAnalysisRegion ((float) selectionStart, (float) selectionEnd))
                status.setText ("Selection applied to resynthesis analysis. Pitch was re-detected unless BASE NOTE is manually locked.", juce::dontSendNotification);
            else status.setText ("Could not analyze this selection.", juce::dontSendNotification);
        };
        useForMidi.onClick = [this]
        {
            proc.setMidiAnalysisRegion ((float) selectionStart, (float) selectionEnd);
            status.setText ("Selection stored as the Melody Lab MIDI analysis range.", juce::dontSendNotification);
        };
        cutToNewReference.onClick = [this] { chooseCutDestination(); };

        duration = juce::jmax (0.0, (double) proc.getReferenceAnalysisDuration());
        selectionStart = juce::jlimit (0.0, duration, (double) proc.getAnalysisStartSeconds());
        selectionEnd = proc.getAnalysisEndSeconds() > selectionStart ? juce::jlimit (selectionStart, duration, (double) proc.getAnalysisEndSeconds()) : duration;
        updateRanges();
        updateControls();
        updateView();
        status.setText ("Drag either handle, drag the highlighted selection to move it, use the time controls, or mouse-wheel over the waveform to zoom.", juce::dontSendNotification);
        startTimerHz (12);
    }

    ~ReferenceEditorDialog() override
    {
        stopTimer();
        thumbnail.removeChangeListener (this);
        proc.stopReferencePreview();
        proc.setReferenceAuditionMode (previousAuditionMode);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff070a0c));
        const RetroHardware3D::Palette palette { findColour (RetroLookAndFeel::primaryLed),
                                                 findColour (RetroLookAndFeel::secondaryLed),
                                                 findColour (RetroLookAndFeel::tertiaryLed) };
        auto body = getLocalBounds().toFloat().reduced (7.0f);
        RetroHardware3D::drawRecessedPanel (g, body, palette, 10.0f);
        g.setColour (findColour (RetroLookAndFeel::secondaryLed));
        g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        g.drawText (proc.getReferenceFile().getFileName() + "  /  " + juce::String (duration, 2) + " s",
                    22, 12, getWidth() - 44, 24, juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (18);
        area.removeFromTop (28);
        waveform.setBounds (area.removeFromTop (juce::jmax (250, area.getHeight() / 2)).reduced (1));
        area.removeFromTop (8);

        auto timeRow = area.removeFromTop (32);
        startLabel.setBounds (timeRow.removeFromLeft (52));
        startTime.setBounds (timeRow.removeFromLeft (juce::jmax (170, timeRow.getWidth() / 3)).reduced (2, 1));
        timeRow.removeFromLeft (8);
        endLabel.setBounds (timeRow.removeFromLeft (42));
        endTime.setBounds (timeRow.reduced (2, 1));

        auto viewRow = area.removeFromTop (32);
        zoomLabel.setBounds (viewRow.removeFromLeft (52));
        zoom.setBounds (viewRow.removeFromLeft (viewRow.getWidth() / 2).reduced (2, 1));
        panLabel.setBounds (viewRow.removeFromLeft (42));
        pan.setBounds (viewRow.reduced (2, 1));

        auto editRow = area.removeFromTop (34);
        normalize.setBounds (editRow.removeFromLeft (170).reduced (2, 1));
        fadeInLabel.setBounds (editRow.removeFromLeft (58));
        fadeIn.setBounds (editRow.removeFromLeft (juce::jmax (160, editRow.getWidth() / 2)).reduced (2, 1));
        fadeOutLabel.setBounds (editRow.removeFromLeft (64));
        fadeOut.setBounds (editRow.reduced (2, 1));

        area.removeFromTop (7);
        auto utility = area.removeFromTop (34);
        fullSelection.setBounds (utility.removeFromLeft (130).reduced (2));
        fitSelection.setBounds (utility.removeFromLeft (130).reduced (2));
        preview.setBounds (utility.removeFromLeft (120).reduced (2));
        stopPreview.setBounds (utility.removeFromLeft (90).reduced (2));

        auto actions = area.removeFromTop (38);
        const int third = actions.getWidth() / 3;
        applyResynth.setBounds (actions.removeFromLeft (third).reduced (2));
        useForMidi.setBounds (actions.removeFromLeft (third).reduced (2));
        cutToNewReference.setBounds (actions.reduced (2));
        area.removeFromTop (5);
        status.setBounds (area.removeFromTop (juce::jmin (54, area.getHeight())).reduced (3));
    }

private:
    class Waveform final : public juce::Component
    {
    public:
        explicit Waveform (ReferenceEditorDialog& o) : owner (o)
        {
            setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
            setTitle ("Large reference waveform. Drag handles or selection; mouse wheel zooms.");
        }

        void paint (juce::Graphics& g) override
        {
            auto bounds = getLocalBounds().toFloat();
            const auto led = findColour (RetroLookAndFeel::primaryLed);
            g.setColour (juce::Colour (0xff03080a));
            g.fillRoundedRectangle (bounds, 8.0f);
            g.setColour (juce::Colour (0xff40545a));
            g.drawRoundedRectangle (bounds.reduced (1.0f), 8.0f, 1.0f);
            auto plot = bounds.reduced (12.0f, 24.0f);
            if (owner.duration <= 0.0)
            {
                g.setColour (juce::Colour (0xff71847e));
                g.drawText ("No reference loaded", plot, juce::Justification::centred);
                return;
            }

            g.setColour (juce::Colour (0xff28434a));
            owner.thumbnail.drawChannels (g, plot.toNearestInt(), owner.viewStart, owner.viewEnd, 1.0f);
            g.setColour (juce::Colour (0xff263a3f));
            g.drawHorizontalLine ((int) plot.getCentreY(), plot.getX(), plot.getRight());

            const double leftTime = juce::jmax (owner.selectionStart, owner.viewStart);
            const double rightTime = juce::jmin (owner.selectionEnd, owner.viewEnd);
            if (rightTime > leftTime)
            {
                const float left = xForTime (leftTime, plot);
                const float right = xForTime (rightTime, plot);
                g.setColour (led.withAlpha (0.16f));
                g.fillRect (left, plot.getY(), right - left, plot.getHeight());
            }

            for (const auto value : { owner.selectionStart, owner.selectionEnd })
            {
                if (value < owner.viewStart || value > owner.viewEnd) continue;
                const float x = xForTime (value, plot);
                g.setColour (findColour (RetroLookAndFeel::secondaryLed));
                g.fillRect (x - 1.0f, plot.getY(), 2.0f, plot.getHeight());
                g.fillRoundedRectangle (x - 7.0f, plot.getY(), 14.0f, 22.0f, 4.0f);
            }

            g.setColour (juce::Colour (0xffafc4be));
            g.setFont (juce::Font (juce::FontOptions (9.0f, juce::Font::bold)));
            g.drawText (owner.formatTime (owner.viewStart), (int) plot.getX(), 3, 120, 18, juce::Justification::centredLeft);
            g.drawText (owner.formatTime (owner.viewEnd), (int) plot.getRight() - 120, 3, 120, 18, juce::Justification::centredRight);
            g.drawText ("SELECTED  " + owner.formatTime (owner.selectionStart) + "  -  " + owner.formatTime (owner.selectionEnd)
                        + "  (" + juce::String (owner.selectionLength(), 3) + " s)",
                        130, 3, juce::jmax (1, getWidth() - 260), 18, juce::Justification::centred, true);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (owner.duration <= 0.0) return;
            auto plot = getLocalBounds().toFloat().reduced (12.0f, 24.0f);
            const double t = timeForX (e.position.x, plot);
            const float sx = xForTime (owner.selectionStart, plot);
            const float ex = xForTime (owner.selectionEnd, plot);
            if (std::abs (e.position.x - sx) <= 10.0f) dragMode = 1;
            else if (std::abs (e.position.x - ex) <= 10.0f) dragMode = 2;
            else if (t > owner.selectionStart && t < owner.selectionEnd)
            {
                dragMode = 3;
                anchorTime = t;
                anchorStart = owner.selectionStart;
                anchorEnd = owner.selectionEnd;
            }
            else dragMode = std::abs (t - owner.selectionStart) < std::abs (t - owner.selectionEnd) ? 1 : 2;
            mouseDrag (e);
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (dragMode == 0 || owner.duration <= 0.0) return;
            auto plot = getLocalBounds().toFloat().reduced (12.0f, 24.0f);
            const double t = timeForX (e.position.x, plot);
            if (dragMode == 1) owner.setSelection (t, owner.selectionEnd, true);
            else if (dragMode == 2) owner.setSelection (owner.selectionStart, t, true);
            else
            {
                const double length = anchorEnd - anchorStart;
                const double delta = t - anchorTime;
                const double first = juce::jlimit (0.0, juce::jmax (0.0, owner.duration - length), anchorStart + delta);
                owner.setSelection (first, first + length, true);
            }
        }

        void mouseUp (const juce::MouseEvent&) override { dragMode = 0; }

        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
        {
            if (owner.duration <= 0.0 || std::abs (wheel.deltaY) < 0.0001f) return;
            auto plot = getLocalBounds().toFloat().reduced (12.0f, 24.0f);
            const double focus = timeForX (e.position.x, plot);
            const double oldViewLength = owner.viewEnd - owner.viewStart;
            const double relative = oldViewLength > 0.0 ? (focus - owner.viewStart) / oldViewLength : 0.5;
            const double factor = wheel.deltaY > 0.0f ? 1.35 : (1.0 / 1.35);
            owner.zoom.setValue (juce::jlimit (1.0, 100.0, owner.zoom.getValue() * factor), juce::dontSendNotification);
            owner.updateView();
            const double newLength = owner.viewEnd - owner.viewStart;
            const double desiredStart = juce::jlimit (0.0, juce::jmax (0.0, owner.duration - newLength), focus - relative * newLength);
            const double maxStart = juce::jmax (0.0, owner.duration - newLength);
            owner.pan.setValue (maxStart > 0.0 ? desiredStart / maxStart : 0.0, juce::dontSendNotification);
            owner.updateView();
            repaint();
        }

    private:
        ReferenceEditorDialog& owner;
        int dragMode = 0;
        double anchorTime = 0.0, anchorStart = 0.0, anchorEnd = 0.0;

        float xForTime (double value, juce::Rectangle<float> plot) const
        {
            const double span = juce::jmax (0.001, owner.viewEnd - owner.viewStart);
            return plot.getX() + (float) ((value - owner.viewStart) / span) * plot.getWidth();
        }

        double timeForX (float x, juce::Rectangle<float> plot) const
        {
            const double norm = juce::jlimit (0.0, 1.0, (double) ((x - plot.getX()) / juce::jmax (1.0f, plot.getWidth())));
            return owner.viewStart + norm * (owner.viewEnd - owner.viewStart);
        }
    };

    RetroMatchSynthAudioProcessor& proc;
    juce::AudioFormatManager formats;
    juce::AudioThumbnailCache cache { 8 };
    juce::AudioThumbnail thumbnail;
    Waveform waveform;
    RetroMatchSynthAudioProcessor::ReferenceAuditionMode previousAuditionMode;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Label startLabel, endLabel, zoomLabel, panLabel, fadeInLabel, fadeOutLabel, status;
    juce::Slider startTime, endTime, zoom, pan, fadeIn, fadeOut;
    juce::ToggleButton normalize { "NORMALIZE -1 dB" };
    juce::TextButton fullSelection { "SELECT FULL" }, fitSelection { "FIT SELECTION" }, preview { "PREVIEW" }, stopPreview { "STOP" };
    juce::TextButton applyResynth { "APPLY TO RESYNTH" }, useForMidi { "USE FOR MIDI" }, cutToNewReference { "CUT -> NEW REFERENCE" };
    double duration = 0.0, selectionStart = 0.0, selectionEnd = 0.0, viewStart = 0.0, viewEnd = 0.0;

    static juce::String formatTime (double seconds)
    {
        const int minutes = (int) (seconds / 60.0);
        const double rest = seconds - minutes * 60.0;
        return juce::String (minutes) + ":" + (rest < 10.0 ? "0" : "") + juce::String (rest, 3);
    }

    static void setupTimeSlider (juce::Slider& slider)
    {
        slider.setSliderStyle (juce::Slider::IncDecButtons);
        slider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 95, 24);
        slider.setTextValueSuffix (" s");
        slider.setNumDecimalPlacesToDisplay (3);
    }

    static void setupFadeSlider (juce::Slider& slider)
    {
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 22);
        slider.setRange (0.0, 10.0, 0.001);
        slider.setTextValueSuffix (" s");
        slider.setNumDecimalPlacesToDisplay (3);
    }

    double selectionLength() const { return juce::jmax (0.0, selectionEnd - selectionStart); }

    void setSelection (double first, double last, bool updateSliders)
    {
        if (duration <= 0.0) return;
        constexpr double gap = 0.002;
        first = juce::jlimit (0.0, juce::jmax (0.0, duration - gap), first);
        last = juce::jlimit (juce::jmin (duration, first + gap), duration, last);
        selectionStart = first;
        selectionEnd = last;
        if (updateSliders) updateControls();
        const double maxFade = juce::jmax (0.0, selectionLength() * 0.5);
        fadeIn.setRange (0.0, juce::jmin (10.0, maxFade), 0.001);
        fadeOut.setRange (0.0, juce::jmin (10.0, maxFade), 0.001);
        waveform.repaint();
    }

    void updateRanges()
    {
        const double d = juce::jmax (0.002, duration);
        startTime.setRange (0.0, d, 0.001);
        endTime.setRange (0.0, d, 0.001);
    }

    void updateControls()
    {
        startTime.setValue (selectionStart, juce::dontSendNotification);
        endTime.setValue (selectionEnd, juce::dontSendNotification);
    }

    void updateView()
    {
        if (duration <= 0.0) { viewStart = 0.0; viewEnd = 0.0; return; }
        const double viewLength = juce::jlimit (duration / 100.0, duration, duration / juce::jmax (1.0, zoom.getValue()));
        const double maxStart = juce::jmax (0.0, duration - viewLength);
        viewStart = juce::jlimit (0.0, maxStart, pan.getValue() * maxStart);
        viewEnd = juce::jmin (duration, viewStart + viewLength);
    }

    void fitSelectionInView()
    {
        if (duration <= 0.0 || selectionLength() <= 0.0) return;
        const double padded = juce::jmin (duration, juce::jmax (selectionLength() * 1.18, 0.05));
        zoom.setValue (juce::jlimit (1.0, 100.0, duration / padded), juce::dontSendNotification);
        updateView();
        const double maxStart = juce::jmax (0.0, duration - (viewEnd - viewStart));
        const double desired = juce::jlimit (0.0, maxStart, (selectionStart + selectionEnd) * 0.5 - (viewEnd - viewStart) * 0.5);
        pan.setValue (maxStart > 0.0 ? desired / maxStart : 0.0, juce::dontSendNotification);
        updateView();
        waveform.repaint();
    }

    void chooseCutDestination()
    {
        if (! proc.getReferenceFile().existsAsFile() || selectionLength() <= 0.001) return;
        const auto initial = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                 .getChildFile (proc.getReferenceFile().getFileNameWithoutExtension() + "-RetroMatch-cut.wav");
        chooser = std::make_unique<juce::FileChooser> ("Save edited reference cut", initial, "*.wav");
        juce::Component::SafePointer<ReferenceEditorDialog> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
            [safe] (const juce::FileChooser& fc)
            {
                if (safe == nullptr || fc.getResult() == juce::File()) return;
                auto target = fc.getResult();
                if (target.getFileExtension().isEmpty()) target = target.withFileExtension (".wav");
                const bool written = safe->proc.exportReferenceSelection (target,
                    (float) safe->selectionStart, (float) safe->selectionEnd,
                    safe->normalize.getToggleState(), (float) safe->fadeIn.getValue(), (float) safe->fadeOut.getValue());
                if (! written)
                {
                    safe->status.setText ("Could not render the edited selection.", juce::dontSendNotification);
                    return;
                }
                if (! safe->proc.loadReferenceSample (target))
                {
                    safe->status.setText ("Cut was saved, but it could not be loaded as the new reference.", juce::dontSendNotification);
                    return;
                }
                safe->thumbnail.setSource (new juce::FileInputSource (target));
                safe->duration = safe->proc.getReferenceAnalysisDuration();
                safe->selectionStart = safe->proc.getAnalysisStartSeconds();
                safe->selectionEnd = safe->proc.getAnalysisEndSeconds();
                safe->zoom.setValue (1.0, juce::dontSendNotification);
                safe->pan.setValue (0.0, juce::dontSendNotification);
                safe->updateRanges(); safe->updateControls(); safe->updateView(); safe->waveform.repaint();
                safe->status.setText ("Edited cut saved and loaded as the new reference.", juce::dontSendNotification);
            });
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        const double loadedDuration = thumbnail.getTotalLength();
        if (loadedDuration > 0.0 && std::abs (loadedDuration - duration) > 0.001)
        {
            duration = loadedDuration;
            selectionStart = juce::jlimit (0.0, duration, selectionStart);
            selectionEnd = juce::jlimit (selectionStart, duration, selectionEnd > selectionStart ? selectionEnd : duration);
            updateRanges(); updateControls(); updateView();
        }
        waveform.repaint();
    }

    void timerCallback() override { waveform.repaint(); }
};
'''
write('Source/UI/ReferenceEditorDialog.h', reference_editor)

reference_player_h = r'''#pragma once
#include <JuceHeader.h>
#include <atomic>

// Polyphonic playback of the loaded reference sample for A/B auditioning.
// The sample is mapped across MIDI notes using the detected/manual root note.
class ReferenceSamplePlayer
{
public:
    ReferenceSamplePlayer();

    void prepare (double sampleRate);
    bool load (const juce::File& file, int rootMidiNote);
    bool setRootMidiNote (int rootMidiNote);
    void clear();

    void render (juce::AudioBuffer<float>& audio, const juce::MidiBuffer& midi, int startSample, int numSamples);
    void noteOnFromUi (int midiNote, float velocity);
    void noteOffFromUi (int midiNote, float velocity = 0.0f);
    void allNotesOff();

    bool previewRegion (const juce::File& file, int rootMidiNote, double startSeconds, double endSeconds,
                        bool normalize, float fadeInSeconds, float fadeOutSeconds);
    void stopPreview();
    bool isPreviewing() const noexcept { return previewing.load(); }
    static bool writeProcessedRegion (const juce::File& source, const juce::File& destination,
                                      double startSeconds, double endSeconds, bool normalize,
                                      float fadeInSeconds, float fadeOutSeconds);

    bool hasSample() const noexcept { return loaded.load(); }
    int getRootMidiNote() const noexcept { return rootNote.load(); }
    const juce::File& getSourceFile() const noexcept { return sourceFile; }

private:
    juce::Synthesiser synth, previewSynth;
    juce::AudioFormatManager formats;
    juce::File sourceFile;
    std::atomic<int> rootNote { 60 };
    std::atomic<bool> loaded { false }, previewing { false };
    double playbackSampleRate = 44100.0;

    bool rebuildSound();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReferenceSamplePlayer)
};
'''
write('Source/Reference/ReferenceSamplePlayer.h', reference_player_h)

reference_player_cpp = r'''#include "ReferenceSamplePlayer.h"
#include <cmath>

namespace
{
bool readProcessedRegion (const juce::File& source, double startSeconds, double endSeconds,
                          bool normalize, float fadeInSeconds, float fadeOutSeconds,
                          juce::AudioBuffer<float>& audio, double& sampleRate)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (source));
    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0) return false;

    const double duration = (double) reader->lengthInSamples / reader->sampleRate;
    const double start = juce::jlimit (0.0, juce::jmax (0.0, duration - 1.0 / reader->sampleRate), startSeconds);
    const double end = juce::jlimit (start + 1.0 / reader->sampleRate, duration,
                                     endSeconds > start ? endSeconds : duration);
    const int64_t firstSample = (int64_t) std::llround (start * reader->sampleRate);
    const int64_t requested = (int64_t) std::llround ((end - start) * reader->sampleRate);
    const int64_t available = juce::jmax<int64_t> (0, reader->lengthInSamples - firstSample);
    const int64_t count64 = juce::jmin (requested, available);
    if (count64 <= 0 || count64 > (int64_t) std::numeric_limits<int>::max()) return false;

    const int count = (int) count64;
    const int channels = juce::jlimit (1, 2, (int) reader->numChannels);
    audio.setSize (channels, count, false, false, true);
    if (! reader->read (&audio, 0, count, firstSample, true, true)) return false;
    sampleRate = reader->sampleRate;

    float gain = 1.0f;
    if (normalize)
    {
        float peak = 0.0f;
        for (int ch = 0; ch < channels; ++ch) peak = juce::jmax (peak, audio.getMagnitude (ch, 0, count));
        if (peak > 1.0e-7f) gain = juce::Decibels::decibelsToGain (-1.0f) / peak;
    }
    if (gain != 1.0f) audio.applyGain (gain);

    const int fadeInSamples = juce::jlimit (0, count / 2, (int) std::llround (juce::jmax (0.0f, fadeInSeconds) * sampleRate));
    const int fadeOutSamples = juce::jlimit (0, count / 2, (int) std::llround (juce::jmax (0.0f, fadeOutSeconds) * sampleRate));
    for (int ch = 0; ch < channels; ++ch)
    {
        if (fadeInSamples > 0) audio.applyGainRamp (ch, 0, fadeInSamples, 0.0f, 1.0f);
        if (fadeOutSamples > 0) audio.applyGainRamp (ch, count - fadeOutSamples, fadeOutSamples, 1.0f, 0.0f);
    }
    return true;
}
}

ReferenceSamplePlayer::ReferenceSamplePlayer()
{
    formats.registerBasicFormats();
    for (int i = 0; i < 8; ++i) synth.addVoice (new juce::SamplerVoice());
    previewSynth.addVoice (new juce::SamplerVoice());
    synth.setCurrentPlaybackSampleRate (playbackSampleRate);
    previewSynth.setCurrentPlaybackSampleRate (playbackSampleRate);
}

void ReferenceSamplePlayer::prepare (double sampleRate)
{
    if (sampleRate > 1000.0) playbackSampleRate = sampleRate;
    synth.setCurrentPlaybackSampleRate (playbackSampleRate);
    previewSynth.setCurrentPlaybackSampleRate (playbackSampleRate);
}

bool ReferenceSamplePlayer::load (const juce::File& file, int rootMidiNote)
{
    sourceFile = file;
    rootNote.store (juce::jlimit (0, 127, rootMidiNote));
    stopPreview();
    return rebuildSound();
}

bool ReferenceSamplePlayer::setRootMidiNote (int rootMidiNote)
{
    rootNote.store (juce::jlimit (0, 127, rootMidiNote));
    if (! sourceFile.existsAsFile()) return false;
    allNotesOff();
    stopPreview();
    return rebuildSound();
}

void ReferenceSamplePlayer::clear()
{
    allNotesOff();
    stopPreview();
    synth.clearSounds();
    sourceFile = juce::File();
    loaded.store (false);
}

bool ReferenceSamplePlayer::rebuildSound()
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (sourceFile));
    if (reader == nullptr)
    {
        synth.clearSounds();
        loaded.store (false);
        return false;
    }

    juce::BigInteger notes;
    notes.setRange (0, 128, true);
    auto sound = std::make_unique<juce::SamplerSound> (
        sourceFile.getFileNameWithoutExtension(), *reader, notes, rootNote.load(),
        0.002, 0.06, 12.0);
    synth.clearSounds();
    synth.addSound (sound.release());
    synth.setCurrentPlaybackSampleRate (playbackSampleRate);
    loaded.store (true);
    return true;
}

void ReferenceSamplePlayer::render (juce::AudioBuffer<float>& audio,
                                    const juce::MidiBuffer& midi,
                                    int startSample,
                                    int numSamples)
{
    if (numSamples <= 0) return;
    if (loaded.load()) synth.renderNextBlock (audio, midi, startSample, numSamples);
    if (previewing.load())
    {
        juce::MidiBuffer noMidi;
        previewSynth.renderNextBlock (audio, noMidi, startSample, numSamples);
        bool anyActive = false;
        for (int i = 0; i < previewSynth.getNumVoices(); ++i)
            if (auto* voice = previewSynth.getVoice (i); voice != nullptr && voice->isVoiceActive()) { anyActive = true; break; }
        previewing.store (anyActive);
    }
}

void ReferenceSamplePlayer::noteOnFromUi (int midiNote, float velocity)
{
    if (! loaded.load()) return;
    synth.noteOn (1, juce::jlimit (0, 127, midiNote), juce::jlimit (0.0f, 1.0f, velocity));
}

void ReferenceSamplePlayer::noteOffFromUi (int midiNote, float velocity)
{
    synth.noteOff (1, juce::jlimit (0, 127, midiNote), juce::jlimit (0.0f, 1.0f, velocity), true);
}

void ReferenceSamplePlayer::allNotesOff()
{
    synth.allNotesOff (0, true);
}

bool ReferenceSamplePlayer::writeProcessedRegion (const juce::File& source, const juce::File& destination,
                                                  double startSeconds, double endSeconds, bool normalize,
                                                  float fadeInSeconds, float fadeOutSeconds)
{
    juce::AudioBuffer<float> audio;
    double sr = 0.0;
    if (! readProcessedRegion (source, startSeconds, endSeconds, normalize, fadeInSeconds, fadeOutSeconds, audio, sr)) return false;

    juce::TemporaryFile temp (destination);
    std::unique_ptr<juce::OutputStream> stream = temp.getFile().createOutputStream();
    if (! stream) return false;
    juce::WavAudioFormat wav;
    const auto options = juce::AudioFormatWriter::Options {}
                             .withSampleRate (sr)
                             .withNumChannels (audio.getNumChannels())
                             .withBitsPerSample (24);
    auto writer = wav.createWriterFor (stream, options);
    if (! writer || ! writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples())) return false;
    writer.reset();
    return temp.overwriteTargetFileWithTemporary();
}

bool ReferenceSamplePlayer::previewRegion (const juce::File& file, int rootMidiNote,
                                           double startSeconds, double endSeconds, bool normalize,
                                           float fadeInSeconds, float fadeOutSeconds)
{
    stopPreview();
    allNotesOff();
    const double previewEnd = juce::jmin (endSeconds, startSeconds + 180.0);
    auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getNonexistentChildFile ("RetroMatch-reference-preview", ".wav", false);
    if (! writeProcessedRegion (file, temp, startSeconds, previewEnd, normalize, fadeInSeconds, fadeOutSeconds)) return false;

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (temp));
    if (reader == nullptr) { temp.deleteFile(); return false; }
    juce::BigInteger notes;
    notes.setRange (0, 128, true);
    const double length = juce::jmin (180.0, juce::jmax (0.01, previewEnd - startSeconds));
    auto sound = std::make_unique<juce::SamplerSound> (
        "Reference preview", *reader, notes, juce::jlimit (0, 127, rootMidiNote), 0.001, 0.03, length + 0.5);
    previewSynth.clearSounds();
    previewSynth.addSound (sound.release());
    previewSynth.setCurrentPlaybackSampleRate (playbackSampleRate);
    temp.deleteFile();
    previewSynth.noteOn (1, juce::jlimit (0, 127, rootMidiNote), 1.0f);
    previewing.store (true);
    return true;
}

void ReferenceSamplePlayer::stopPreview()
{
    previewSynth.allNotesOff (0, false);
    previewSynth.clearSounds();
    previewing.store (false);
}
'''
write('Source/Reference/ReferenceSamplePlayer.cpp', reference_player_cpp)

# Processor API and state.
must_replace('Source/PluginProcessor.h',
'''    float getAnalysisEndSeconds() const noexcept { return analysisEndSeconds.load(); }\n    float getEffectiveBpm() const noexcept { return effectiveBpm.load (std::memory_order_relaxed); }\n    bool setReferenceAnalysisRegion (float startSeconds, float endSeconds);''',
'''    float getAnalysisEndSeconds() const noexcept { return analysisEndSeconds.load(); }\n    float getMidiAnalysisStartSeconds() const noexcept;\n    float getMidiAnalysisEndSeconds() const noexcept;\n    void setMidiAnalysisRegion (float startSeconds, float endSeconds);\n    float getEffectiveBpm() const noexcept { return effectiveBpm.load (std::memory_order_relaxed); }\n    bool setReferenceAnalysisRegion (float startSeconds, float endSeconds);''')

must_replace('Source/PluginProcessor.h',
'''    void setReferenceAuditionLevel (float level) noexcept { referenceAuditionLevel.store (juce::jlimit (0.0f, 1.0f, level)); }\n    float getReferenceAuditionLevel() const noexcept { return referenceAuditionLevel.load(); }''',
'''    void setReferenceAuditionLevel (float level) noexcept { referenceAuditionLevel.store (juce::jlimit (0.0f, 1.0f, level)); }\n    float getReferenceAuditionLevel() const noexcept { return referenceAuditionLevel.load(); }\n    bool previewReferenceRegion (float startSeconds, float endSeconds, bool normalize, float fadeInSeconds, float fadeOutSeconds);\n    void stopReferencePreview();\n    bool exportReferenceSelection (const juce::File& destination, float startSeconds, float endSeconds,\n                                   bool normalize, float fadeInSeconds, float fadeOutSeconds);''')

must_replace('Source/PluginProcessor.h',
'''    std::atomic<float> analysisSourceDuration { 0.0f };\n    std::atomic<float> effectiveBpm { 120.0f };''',
'''    std::atomic<float> analysisSourceDuration { 0.0f };\n    std::atomic<bool> referencePitchLocked { false };\n    std::atomic<float> effectiveBpm { 120.0f };''')

load_sample = r'''bool RetroMatchSynthAudioProcessor::loadReferenceSample (const juce::File& f)
{
    auto analysed = SampleAnalyzer::analyzeFile (f);
    if (! analysed) return false;
    setMelodyClip ({});

    detectedReferenceHz = analysed->fundamentalHz;
    detectedReferencePitchConfidence = analysed->pitchConfidence;
    detectedReferenceMidiNote = hzToNearestMidiNote (detectedReferenceHz);
    referenceBaseMidiNote.store (detectedReferenceMidiNote);
    referencePitchLocked.store (false);
    loadedReferenceFile = f;

    double sourceDuration = analysed->duration;
    juce::AudioFormatManager durationFormats;
    durationFormats.registerBasicFormats();
    if (std::unique_ptr<juce::AudioFormatReader> durationReader (durationFormats.createReaderFor (f)); durationReader != nullptr && durationReader->sampleRate > 0.0)
        sourceDuration = (double) durationReader->lengthInSamples / durationReader->sampleRate;
    const float sourceSeconds = (float) juce::jmax (0.0, sourceDuration);
    analysisSourceDuration.store (sourceSeconds);
    analysisStartSeconds.store (0.0f);
    const float initialEnd = sourceSeconds > 0.0f ? juce::jmin (sourceSeconds, juce::jmax (0.05f, analysed->duration)) : analysed->duration;
    analysisEndSeconds.store (initialEnd);

    currentFeatures = std::move (analysed);
    currentCandidateFeatures.reset();
    const float wavetableHz = currentFeatures->fundamentalHz > 20.0f ? currentFeatures->fundamentalHz : midiNoteToHz (referenceBaseMidiNote.load());
    referenceWavetable = ReferenceWavetableExtractor::extract (f, wavetableHz, 0.0f, initialEnd);
    referencePlayer.load (f, detectedReferenceMidiNote);
    loadedSampleName = f.getFileName();
    setMidiAnalysisRegion (0.0f, sourceSeconds);
    lastMatch = {};
    candidateBank = {};
    selectedCandidate = 0;
    return true;
}'''
replace_between('Source/PluginProcessor.cpp',
                'bool RetroMatchSynthAudioProcessor::loadReferenceSample (const juce::File& f)',
                'bool RetroMatchSynthAudioProcessor::setReferenceAnalysisRegion (float startSeconds, float endSeconds)',
                load_sample)

set_region = r'''bool RetroMatchSynthAudioProcessor::setReferenceAnalysisRegion (float startSeconds, float endSeconds)
{
    if (! loadedReferenceFile.existsAsFile()) return false;
    const float duration = analysisSourceDuration.load();
    const float start = juce::jlimit (0.0f, juce::jmax (0.0f, duration - 0.002f), startSeconds);
    const float end = juce::jlimit (start + 0.002f, juce::jmax (start + 0.002f, duration), endSeconds);
    const float expected = referencePitchLocked.load() ? midiNoteToHz (referenceBaseMidiNote.load()) : 0.0f;
    auto analysed = SampleAnalyzer::analyzeFile (loadedReferenceFile, expected, start, end);
    if (! analysed) return false;

    analysisStartSeconds.store (start);
    analysisEndSeconds.store (end);
    if (! referencePitchLocked.load())
    {
        detectedReferenceHz = analysed->fundamentalHz;
        detectedReferencePitchConfidence = analysed->pitchConfidence;
        if (detectedReferenceHz > 20.0f && detectedReferencePitchConfidence > 0.10f)
        {
            detectedReferenceMidiNote = hzToNearestMidiNote (detectedReferenceHz);
            referenceBaseMidiNote.store (detectedReferenceMidiNote);
            referencePlayer.setRootMidiNote (detectedReferenceMidiNote);
        }
    }

    currentFeatures = std::move (analysed);
    const float wavetableHz = currentFeatures->fundamentalHz > 20.0f ? currentFeatures->fundamentalHz : midiNoteToHz (referenceBaseMidiNote.load());
    referenceWavetable = ReferenceWavetableExtractor::extract (loadedReferenceFile, wavetableHz, start, end);
    invalidateMatchesAfterReferencePitchChange();
    return true;
}'''
replace_between('Source/PluginProcessor.cpp',
                'bool RetroMatchSynthAudioProcessor::setReferenceAnalysisRegion (float startSeconds, float endSeconds)',
                'bool RetroMatchSynthAudioProcessor::createUserWavetableFromReference',
                set_region)

set_base = r'''bool RetroMatchSynthAudioProcessor::setReferenceBaseMidiNote (int midiNote)
{
    if (! loadedReferenceFile.existsAsFile()) return false;

    const int note = juce::jlimit (0, 127, midiNote);
    const float expectedHz = midiNoteToHz (note);
    const float start = analysisStartSeconds.load();
    const float end = analysisEndSeconds.load() > start ? analysisEndSeconds.load() : analysisSourceDuration.load();
    auto analysed = SampleAnalyzer::analyzeFile (loadedReferenceFile, expectedHz, start, end);
    if (! analysed) return false;

    currentFeatures = std::move (analysed);
    referenceBaseMidiNote.store (note);
    referencePitchLocked.store (true);
    referencePlayer.setRootMidiNote (note);
    referenceWavetable = ReferenceWavetableExtractor::extract (loadedReferenceFile, expectedHz, start, end);
    invalidateMatchesAfterReferencePitchChange();
    return true;
}'''
replace_between('Source/PluginProcessor.cpp',
                'bool RetroMatchSynthAudioProcessor::setReferenceBaseMidiNote (int midiNote)',
                'bool RetroMatchSynthAudioProcessor::resetReferenceBaseMidiNote()',
                set_base)

reset_and_tools = r'''bool RetroMatchSynthAudioProcessor::resetReferenceBaseMidiNote()
{
    if (! loadedReferenceFile.existsAsFile()) return false;
    referencePitchLocked.store (false);
    const float start = analysisStartSeconds.load();
    const float end = analysisEndSeconds.load() > start ? analysisEndSeconds.load() : analysisSourceDuration.load();
    auto analysed = SampleAnalyzer::analyzeFile (loadedReferenceFile, 0.0f, start, end);
    if (! analysed) return false;

    detectedReferenceHz = analysed->fundamentalHz;
    detectedReferencePitchConfidence = analysed->pitchConfidence;
    if (detectedReferenceHz > 20.0f) detectedReferenceMidiNote = hzToNearestMidiNote (detectedReferenceHz);
    referenceBaseMidiNote.store (detectedReferenceMidiNote);
    referencePlayer.setRootMidiNote (detectedReferenceMidiNote);
    currentFeatures = std::move (analysed);
    const float wavetableHz = currentFeatures->fundamentalHz > 20.0f ? currentFeatures->fundamentalHz : midiNoteToHz (detectedReferenceMidiNote);
    referenceWavetable = ReferenceWavetableExtractor::extract (loadedReferenceFile, wavetableHz, start, end);
    invalidateMatchesAfterReferencePitchChange();
    return true;
}

float RetroMatchSynthAudioProcessor::getMidiAnalysisStartSeconds() const noexcept
{
    const float duration = analysisSourceDuration.load();
    return juce::jlimit (0.0f, juce::jmax (0.0f, duration), (float) apvts.state.getProperty ("midiAnalysisStart", 0.0f));
}

float RetroMatchSynthAudioProcessor::getMidiAnalysisEndSeconds() const noexcept
{
    const float duration = analysisSourceDuration.load();
    const float start = getMidiAnalysisStartSeconds();
    return juce::jlimit (start, juce::jmax (start, duration), (float) apvts.state.getProperty ("midiAnalysisEnd", duration));
}

void RetroMatchSynthAudioProcessor::setMidiAnalysisRegion (float startSeconds, float endSeconds)
{
    const float duration = analysisSourceDuration.load();
    if (duration <= 0.0f) return;
    const float start = juce::jlimit (0.0f, juce::jmax (0.0f, duration - 0.002f), startSeconds);
    const float end = juce::jlimit (start + 0.002f, duration, endSeconds);
    apvts.state.setProperty ("midiAnalysisStart", start, nullptr);
    apvts.state.setProperty ("midiAnalysisEnd", end, nullptr);
}

bool RetroMatchSynthAudioProcessor::previewReferenceRegion (float startSeconds, float endSeconds, bool normalize,
                                                            float fadeInSeconds, float fadeOutSeconds)
{
    if (! loadedReferenceFile.existsAsFile()) return false;
    setReferenceAuditionMode (ReferenceAuditionMode::referenceOnly);
    return referencePlayer.previewRegion (loadedReferenceFile, referenceBaseMidiNote.load(), startSeconds, endSeconds,
                                          normalize, fadeInSeconds, fadeOutSeconds);
}

void RetroMatchSynthAudioProcessor::stopReferencePreview()
{
    referencePlayer.stopPreview();
}

bool RetroMatchSynthAudioProcessor::exportReferenceSelection (const juce::File& destination,
                                                              float startSeconds, float endSeconds, bool normalize,
                                                              float fadeInSeconds, float fadeOutSeconds)
{
    if (! loadedReferenceFile.existsAsFile()) return false;
    return ReferenceSamplePlayer::writeProcessedRegion (loadedReferenceFile, destination, startSeconds, endSeconds,
                                                        normalize, fadeInSeconds, fadeOutSeconds);
}'''
replace_between('Source/PluginProcessor.cpp',
                'bool RetroMatchSynthAudioProcessor::resetReferenceBaseMidiNote()',
                'void RetroMatchSynthAudioProcessor::setReferenceAuditionMode (ReferenceAuditionMode mode)',
                reset_and_tools)

must_replace('Source/PluginProcessor.cpp',
'''    const int value = juce::jlimit ((int) ReferenceAuditionMode::synthOnly,\n                                    (int) ReferenceAuditionMode::mixed,\n                                    (int) mode);\n    allEditorNotesOff();\n    referenceAuditionMode.store (value);''',
'''    const int value = juce::jlimit ((int) ReferenceAuditionMode::synthOnly,\n                                    (int) ReferenceAuditionMode::mixed,\n                                    (int) mode);\n    referencePlayer.stopPreview();\n    allEditorNotesOff();\n    referenceAuditionMode.store (value);''')

# Large editor launcher in the main hardware UI.
must_replace('Source/UI/RetroMatchEditorV3.cpp',
'''#include "LayersPage.h"\n#include <BinaryData.h>''',
'''#include "LayersPage.h"\n#include "ReferenceEditorDialog.h"\n#include <BinaryData.h>''')

must_replace('Source/UI/RetroMatchEditorV3.cpp',
'''    addAndMakeVisible (referenceRegion);\n    referenceRegion.onRegion = [this] (double first, double last, bool commit)''',
'''    addAndMakeVisible (referenceRegion);\n    referenceRegion.onOpenEditor = [this]\n    {\n        if (! proc.getReferenceFile().existsAsFile())\n        {\n            status.setText ("Load a reference before opening the large editor.", juce::dontSendNotification);\n            return;\n        }\n        auto* content = new ReferenceEditorDialog (proc);\n        content->setSize (1120, 680);\n        juce::DialogWindow::LaunchOptions options;\n        options.content.setOwned (content);\n        options.dialogTitle = "RM-01  /  LARGE REFERENCE EDITOR";\n        options.dialogBackgroundColour = juce::Colour (0xff070a0c);\n        options.escapeKeyTriggersCloseButton = true;\n        options.useNativeTitleBar = true;\n        options.resizable = true;\n        options.componentToCentreAround = this;\n        if (auto* window = options.launchAsync()) window->setResizeLimits (820, 520, 1600, 1000);\n    };\n    referenceRegion.onRegion = [this] (double first, double last, bool commit)''')

# Melody state limits and full-track chunked transcription.
melody_h = r'''#pragma once
#include <JuceHeader.h>
#include <functional>

struct TranscribedNote
{
    int pitch = 60;
    double start = 0.0, duration = 0.1;
    float velocity = 0.8f, confidence = 0.0f;
};

struct MelodyClip
{
    static constexpr int maxNotes = 16384;
    static constexpr double maxDurationSeconds = 6.0 * 60.0 * 60.0;
    std::vector<TranscribedNote> notes;
    double duration = 0.0, bpm = 120.0;
    juce::String sourceName;
    bool layered = false, truncated = false;
    juce::MidiMessageSequence sequenceInSeconds() const;
    bool writeMidi (const juce::File&) const;
    juce::ValueTree toState() const;
    static MelodyClip fromState (const juce::ValueTree&);
};

class MelodyAnalyzer
{
public:
    using Cancel = std::function<bool()>;
    using Progress = std::function<void(float)>;
    static MelodyClip analyzeFile (const juce::File&, bool layered, double bpm,
                                   Cancel = {}, Progress = {});
    static MelodyClip analyzeFile (const juce::File&, bool layered, double bpm,
                                   double startSeconds, double endSeconds,
                                   Cancel = {}, Progress = {});
    static MelodyClip analyzeBuffer (const juce::AudioBuffer<float>&, double sampleRate,
                                     bool layered, double bpm, Cancel = {}, Progress = {});
};
'''
write('Source/Analysis/MelodyAnalyzer.h', melody_h)

from_state = r'''MelodyClip MelodyClip::fromState (const juce::ValueTree& state)
{
    MelodyClip clip;
    if (! state.hasType ("MELODY")) return clip;
    auto bounded = [] (double value, double lo, double hi, double fallback)
    { return std::isfinite (value) ? juce::jlimit (lo, hi, value) : fallback; };
    clip.bpm = bounded ((double) state.getProperty ("bpm", 120.0), 30.0, 300.0, 120.0);
    clip.duration = bounded ((double) state.getProperty ("duration"), 0.0, MelodyClip::maxDurationSeconds, 0.0);
    clip.sourceName = state.getProperty ("source").toString().substring (0, 256);
    clip.layered = state.getProperty ("layered"); clip.truncated = state.getProperty ("truncated");
    for (const auto& n : state)
    {
        if (! n.hasType ("NOTE") || (int) clip.notes.size() >= maxNotes) continue;
        TranscribedNote note;
        note.pitch = juce::jlimit (0, 127, (int) n.getProperty ("pitch", 60));
        note.start = bounded ((double) n.getProperty ("start"), 0.0, MelodyClip::maxDurationSeconds, 0.0);
        const double remaining = juce::jmax (0.01, MelodyClip::maxDurationSeconds - note.start);
        note.duration = bounded ((double) n.getProperty ("duration"), 0.01, remaining, 0.1);
        note.velocity = (float) bounded ((double) n.getProperty ("velocity"), 0.05, 1.0, 0.8);
        note.confidence = (float) bounded ((double) n.getProperty ("confidence"), 0.0, 1.0, 0.0);
        clip.notes.push_back (note); clip.duration = std::max (clip.duration, note.start + note.duration);
    }
    std::sort (clip.notes.begin(), clip.notes.end(), [] (const auto& a, const auto& b) { return a.start < b.start; });
    return clip;
}'''
replace_between('Source/Analysis/MelodyAnalyzer.cpp',
                'MelodyClip MelodyClip::fromState (const juce::ValueTree& state)',
                'MelodyClip MelodyAnalyzer::analyzeFile (const juce::File& file, bool layered, double bpm, Cancel cancel, Progress progress)',
                from_state)

chunked_analyzer = r'''MelodyClip MelodyAnalyzer::analyzeFile (const juce::File& file, bool layered, double bpm,
                                        double startSeconds, double endSeconds, Cancel cancel, Progress progress)
{
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    MelodyClip clip;
    clip.layered = layered;
    clip.bpm = std::isfinite (bpm) ? juce::jlimit (30.0, 300.0, bpm) : 120.0;
    clip.sourceName = file.getFileName();
    if (! reader || reader->sampleRate <= 0.0) return clip;

    const double fileDuration = (double) reader->lengthInSamples / reader->sampleRate;
    const double start = juce::jlimit (0.0, fileDuration, startSeconds);
    const double requestedEnd = endSeconds > start ? juce::jlimit (start + 1.0 / reader->sampleRate, fileDuration, endSeconds) : fileDuration;
    const double end = juce::jmin (requestedEnd, start + MelodyClip::maxDurationSeconds);
    const double total = juce::jmax (0.0, end - start);
    clip.duration = total;
    clip.truncated = requestedEnd > end;
    if (total <= 0.0) return clip;

    constexpr double chunkSeconds = 45.0;
    double cursor = start;
    while (cursor < end - 0.5 / reader->sampleRate)
    {
        if (cancel && cancel()) return {};
        const double chunkEnd = juce::jmin (end, cursor + chunkSeconds);
        const int64_t firstSample = (int64_t) std::llround (cursor * reader->sampleRate);
        const int64_t wanted = (int64_t) std::llround ((chunkEnd - cursor) * reader->sampleRate);
        const int64_t available = juce::jmax<int64_t> (0, reader->lengthInSamples - firstSample);
        const int length = (int) juce::jmin<int64_t> (wanted, available);
        if (length <= 0) break;

        juce::AudioBuffer<float> audio (juce::jlimit (1, 2, (int) reader->numChannels), length);
        if (! reader->read (&audio, 0, length, firstSample, true, true)) break;
        const double baseProgress = (cursor - start) / total;
        const double progressSpan = (chunkEnd - cursor) / total;
        auto chunkProgress = [progress, baseProgress, progressSpan] (float value)
        {
            if (progress) progress ((float) juce::jlimit (0.0, 1.0, baseProgress + progressSpan * juce::jlimit (0.0f, 1.0f, value)));
        };
        auto part = analyzeBuffer (audio, reader->sampleRate, layered, clip.bpm, cancel, chunkProgress);
        if (cancel && cancel()) return {};

        const double offset = cursor - start;
        for (auto note : part.notes)
        {
            if ((int) clip.notes.size() >= MelodyClip::maxNotes) { clip.truncated = true; break; }
            note.start += offset;
            if (note.start < clip.duration) clip.notes.push_back (note);
        }
        clip.truncated = clip.truncated || part.truncated;
        if ((int) clip.notes.size() >= MelodyClip::maxNotes) break;
        cursor = chunkEnd;
    }

    std::sort (clip.notes.begin(), clip.notes.end(), [] (const auto& a, const auto& b) { return a.start < b.start; });
    if (progress) progress (1.0f);
    return clip;
}'''
replace_between('Source/Analysis/MelodyAnalyzer.cpp',
                'MelodyClip MelodyAnalyzer::analyzeFile (const juce::File& file, bool layered, double bpm,\n                                        double startSeconds, double endSeconds, Cancel cancel, Progress progress)',
                'MelodyClip MelodyAnalyzer::analyzeBuffer (const juce::AudioBuffer<float>& audio, double sr,',
                chunked_analyzer)

# Melody Lab region controls.
must_replace('Source/UI/MelodyPage.h',
'''        for (auto* button : std::array<juce::Button*, 9> { &analyze, &play, &samplePlay, &stop, &save, &drag, &lower, &higher, &remove }) addAndMakeVisible (*button);''',
'''        for (auto* button : std::array<juce::Button*, 11> { &analyze, &play, &samplePlay, &stop, &save, &drag, &lower, &higher, &remove, &midiFromSynth, &midiFull }) addAndMakeVisible (*button);''')

must_replace('Source/UI/MelodyPage.h',
'''            worker = std::make_unique<Worker> (file, mode.getSelectedId() == 2, tempo.getValue(), midiStart.getValue(), midiEnd.getValue());''',
'''            if (midiEnd.getValue() <= midiStart.getValue() + 0.002) { hint.setText ("MIDI region must have a positive duration.", juce::dontSendNotification); return; }\n            proc.setMidiAnalysisRegion ((float) midiStart.getValue(), (float) midiEnd.getValue());\n            worker = std::make_unique<Worker> (file, mode.getSelectedId() == 2, tempo.getValue(), midiStart.getValue(), midiEnd.getValue());''')

must_replace('Source/UI/MelodyPage.h',
'''        applyRegion.onClick = [this]\n        {\n            if (proc.setReferenceAnalysisRegion ((float) synthStart.getValue(), (float) synthEnd.getValue()))\n            { hint.setText ("Synthesis analysis region applied. Analyze MIDI again if needed.", juce::dontSendNotification); refreshClip(); }\n        };\n        refreshClip(); startTimerHz (25);''',
'''        applyRegion.onClick = [this]\n        {\n            if (proc.setReferenceAnalysisRegion ((float) synthStart.getValue(), (float) synthEnd.getValue()))\n            { hint.setText ("Synthesis analysis region applied. Pitch is re-detected unless BASE NOTE is manually locked.", juce::dontSendNotification); refreshClip(); }\n        };\n        midiFromSynth.onClick = [this]\n        {\n            midiStart.setValue (synthStart.getValue(), juce::dontSendNotification);\n            midiEnd.setValue (synthEnd.getValue(), juce::dontSendNotification);\n            proc.setMidiAnalysisRegion ((float) midiStart.getValue(), (float) midiEnd.getValue());\n            hint.setText ("MIDI region copied from the resynthesis selection.", juce::dontSendNotification);\n        };\n        midiFull.onClick = [this]\n        {\n            const double duration = juce::jmax (0.01, (double) proc.getReferenceAnalysisDuration());\n            midiStart.setValue (0.0, juce::dontSendNotification); midiEnd.setValue (duration, juce::dontSendNotification);\n            proc.setMidiAnalysisRegion (0.0f, (float) duration);\n            hint.setText ("MIDI region set to the full source. Long tracks are transcribed in 45-second chunks.", juce::dontSendNotification);\n        };\n        auto persistMidiRegion = [this]\n        {\n            if (midiEnd.getValue() > midiStart.getValue() + 0.002)\n                proc.setMidiAnalysisRegion ((float) midiStart.getValue(), (float) midiEnd.getValue());\n        };\n        midiStart.onValueChange = persistMidiRegion; midiEnd.onValueChange = persistMidiRegion;\n        refreshClip(); startTimerHz (25);''')

must_replace('Source/UI/MelodyPage.h',
'''        auto midi = area.removeFromTop (27);\n        midiLabel.setBounds (midi.removeFromLeft (105).reduced (2)); midiStart.setBounds (midi.removeFromLeft (125).reduced (2)); midiEnd.setBounds (midi.reduced (2));''',
'''        auto midi = area.removeFromTop (30);\n        midiLabel.setBounds (midi.removeFromLeft (96).reduced (2)); midiStart.setBounds (midi.removeFromLeft (120).reduced (2)); midiEnd.setBounds (midi.removeFromLeft (120).reduced (2));\n        midiFromSynth.setBounds (midi.removeFromLeft (112).reduced (2)); midiFull.setBounds (midi.reduced (2));''')

must_replace('Source/UI/MelodyPage.h',
'''    float shownStart = -1, shownEnd = -1;''',
'''    float shownStart = -1, shownEnd = -1, shownMidiStart = -1, shownMidiEnd = -1;''')

must_replace('Source/UI/MelodyPage.h',
'''    juce::TextButton lower { "NOTE -" }, higher { "NOTE +" }, remove { "DELETE NOTE" };''',
'''    juce::TextButton lower { "NOTE -" }, higher { "NOTE +" }, remove { "DELETE NOTE" };\n    juce::TextButton midiFromSynth { "MIDI = SYNTH" }, midiFull { "FULL TRACK MIDI" };''')

must_replace('Source/UI/MelodyPage.h',
'''        midiStart.setValue (0.0, juce::dontSendNotification); midiEnd.setValue (clip.duration > 0 ? clip.duration : duration, juce::dontSendNotification);''',
'''        double midiFirst = juce::jlimit (0.0, duration, (double) proc.getMidiAnalysisStartSeconds());\n        double midiLast = juce::jlimit (midiFirst, duration, (double) proc.getMidiAnalysisEndSeconds());\n        if (midiLast <= midiFirst + 0.002) { midiFirst = 0.0; midiLast = duration; }\n        midiStart.setValue (midiFirst, juce::dontSendNotification); midiEnd.setValue (midiLast, juce::dontSendNotification);\n        shownMidiStart = (float) midiFirst; shownMidiEnd = (float) midiLast;''')

must_replace('Source/UI/MelodyPage.h',
'''        text += clip.truncated ? "Analysis limit reached (60 s / 4096 notes). " : "";\n        text += "Click a note to correct/delete it. Mixed audio may contain extra or missed notes. MIDI carries notes; save the synth patch separately.";''',
'''        text += clip.truncated ? "Analysis safety limit reached (6 h / 16384 notes). " : "";\n        text += "Click a note to correct/delete it. FULL TRACK MIDI analyzes the whole source in chunks; the MIDI timing is relative to the selected MIDI region. Mixed audio may contain extra or missed notes.";''')

must_replace('Source/UI/MelodyPage.h',
'''            shownStart = start; shownEnd = end; sampleView.repaint();\n        }\n        const bool ready = ! clip.notes.empty() && ! worker;''',
'''            shownStart = start; shownEnd = end; sampleView.repaint();\n        }\n        const auto midiRegionStart = proc.getMidiAnalysisStartSeconds();\n        const auto midiRegionEnd = proc.getMidiAnalysisEndSeconds();\n        if ((midiRegionStart != shownMidiStart || midiRegionEnd != shownMidiEnd) && ! midiStart.isMouseButtonDown() && ! midiEnd.isMouseButtonDown())\n        {\n            const double duration = juce::jmax (0.01, (double) proc.getReferenceAnalysisDuration());\n            midiStart.setRange (0, duration, 0.001); midiEnd.setRange (0, duration, 0.001);\n            midiStart.setValue (midiRegionStart, juce::dontSendNotification); midiEnd.setValue (midiRegionEnd, juce::dontSendNotification);\n            shownMidiStart = midiRegionStart; shownMidiEnd = midiRegionEnd;\n        }\n        const bool ready = ! clip.notes.empty() && ! worker;''')

# Long-duration state regression test.
must_replace('Tests/MelodyTests.cpp',
'''    auto restored = MelodyClip::fromState (clip.toState());\n    if (restored.notes.size() != clip.notes.size() || restored.notes[2].pitch != 67) return fail ("clip state round trip failed");''',
'''    auto restored = MelodyClip::fromState (clip.toState());\n    if (restored.notes.size() != clip.notes.size() || restored.notes[2].pitch != 67) return fail ("clip state round trip failed");\n    MelodyClip longClip; longClip.duration = 185.0; longClip.notes.push_back ({ 72, 121.25, 0.5, 0.7f, 0.9f });\n    const auto longRestored = MelodyClip::fromState (longClip.toState());\n    if (longRestored.duration < 180.0 || longRestored.notes.empty() || longRestored.notes[0].start < 120.0)\n        return fail ("long-track melody state was truncated to the old 60 second limit");''')

print('Reference editor migration applied.')
