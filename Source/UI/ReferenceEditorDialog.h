#pragma once
#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "RetroLookAndFeel.h"
#include "Hardware3DKit.h"
#include <array>
#include <cmath>

class ReferenceEditorDialog final : public juce::Component,
                                    private juce::Timer,
                                    private juce::ChangeListener
{
public:
    explicit ReferenceEditorDialog (RetroMatchSynthAudioProcessor& p)
        : proc (p), thumbnail (1024, formats, cache), waveform (*this), previousAuditionMode (p.getReferenceAuditionMode())
    {
        laf.setPalette (proc.lightPalette.load());
        setLookAndFeel (&laf);
        setOpaque (true);

        formats.registerBasicFormats();
        thumbnail.addChangeListener (this);
        const auto file = proc.getReferenceFile();
        if (file.existsAsFile()) thumbnail.setSource (new juce::FileInputSource (file));

        addAndMakeVisible (waveform);
        for (auto* c : std::array<juce::Component*, 11> {
                 &startLabel, &startTime, &endLabel, &endTime,
                 &zoomLabel, &zoom, &panLabel, &pan,
                 &normalizePreview, &resetEdits, &status })
            addAndMakeVisible (*c);

        for (auto* c : std::array<juce::Component*, 14> {
                 &fadeInLabel, &fadeIn, &lowCutLabel, &lowCut, &lowLabel, &lowGain,
                 &midLabel, &midGain, &highLabel, &highGain, &highCutLabel, &highCut,
                 &fadeOutLabel, &fadeOut })
            addAndMakeVisible (*c);

        for (auto* b : { &fullSelection, &fitSelection, &preview, &stopPreview,
                         &applyResynth, &useForMidi, &cutToNewReference })
            addAndMakeVisible (*b);

        setupTimeSlider (startTime);
        setupTimeSlider (endTime);
        setupViewSlider (zoom, 1.0, 100.0, " x");
        zoom.setSkewFactorFromMidPoint (8.0);
        zoom.setValue (1.0, juce::dontSendNotification);
        setupViewSlider (pan, 0.0, 1.0, {});
        pan.setValue (0.0, juce::dontSendNotification);

        setupEditKnob (fadeIn, 0.0, 10.0, 0.0, " s");
        setupEditKnob (lowCut, 20.0, 600.0, 20.0, " Hz", 120.0);
        setupEditKnob (lowGain, -18.0, 18.0, 0.0, " dB");
        setupEditKnob (midGain, -18.0, 18.0, 0.0, " dB");
        setupEditKnob (highGain, -18.0, 18.0, 0.0, " dB");
        setupEditKnob (highCut, 1500.0, 22000.0, 20000.0, " Hz", 10000.0);
        setupEditKnob (fadeOut, 0.0, 10.0, 0.0, " s");

        setLabel (startLabel, "START");
        setLabel (endLabel, "END");
        setLabel (zoomLabel, "ZOOM");
        setLabel (panLabel, "PAN");
        setLabel (fadeInLabel, "FADE IN", true);
        setLabel (lowCutLabel, "LOW END / HPF", true);
        setLabel (lowLabel, "LOW", true);
        setLabel (midLabel, "MID", true);
        setLabel (highLabel, "HIGH", true);
        setLabel (highCutLabel, "HIGH END / LPF", true);
        setLabel (fadeOutLabel, "FADE OUT", true);

        normalizePreview.setClickingTogglesState (true);
        normalizePreview.setTooltip ("Toggle -1 dBFS normalization and immediately preview the selected range. Normalization happens after EQ.");
        resetEdits.setTooltip ("Reset fades, EQ and normalization without changing the selected range.");
        fadeIn.setTooltip ("Fade-in length. The lower waveform handle edits the same value graphically.");
        fadeOut.setTooltip ("Fade-out length. The lower waveform handle edits the same value graphically.");
        lowCut.setTooltip ("Low-end high-pass cutoff before resynthesis preview/export.");
        lowGain.setTooltip ("Low shelf around 180 Hz.");
        midGain.setTooltip ("Broad mid bell around 1.2 kHz.");
        highGain.setTooltip ("High shelf around 6.5 kHz.");
        highCut.setTooltip ("High-end low-pass cutoff before resynthesis preview/export.");
        fullSelection.setTooltip ("Select the complete source file. This does not force the resynth analyzer to use the complete song.");
        applyResynth.setTooltip ("Use this range for resynthesis. If EQ/fades/normalize are active, RetroMatch renders a non-destructive working reference first so the matcher hears the edits.");
        useForMidi.setTooltip ("Use this range for Melody Lab transcription. Long ranges are processed in chunks.");
        cutToNewReference.setTooltip ("Render the selected range with the visible EQ, normalization and fades to a WAV file and load it as the new reference.");

        startTime.onValueChange = [this] { setSelection (startTime.getValue(), selectionEnd, false); };
        endTime.onValueChange = [this] { setSelection (selectionStart, endTime.getValue(), false); };
        zoom.onValueChange = [this] { updateView(); waveform.repaint(); };
        pan.onValueChange = [this] { updateView(); waveform.repaint(); };
        fadeIn.onValueChange = [this] { clampFades(); waveform.repaint(); };
        fadeOut.onValueChange = [this] { clampFades(); waveform.repaint(); };
        for (auto* slider : { &lowCut, &lowGain, &midGain, &highGain, &highCut })
            slider->onValueChange = [this] { waveform.repaint(); };

        normalizePreview.onClick = [this]
        {
            previewNow (normalizePreview.getToggleState() ? "Normalized -1 dBFS preview." : "Preview without normalization.");
        };
        resetEdits.onClick = [this]
        {
            resetEditControls();
            status.setText ("Reference edits reset. Selection is unchanged.", juce::dontSendNotification);
        };
        fullSelection.onClick = [this]
        {
            if (duration > 0.0)
            {
                setSelection (0.0, duration, true);
                zoom.setValue (1.0);
                pan.setValue (0.0);
            }
        };
        fitSelection.onClick = [this] { fitSelectionInView(); };
        preview.onClick = [this] { previewNow ("Previewing selected range with the current edits."); };
        stopPreview.onClick = [this]
        {
            proc.stopReferencePreview();
            proc.setReferenceAuditionMode (previousAuditionMode);
            status.setText ("Preview stopped.", juce::dontSendNotification);
        };
        applyResynth.onClick = [this] { applySelectionToResynth(); };
        useForMidi.onClick = [this]
        {
            proc.setMidiAnalysisRegion ((float) selectionStart, (float) selectionEnd);
            status.setText (hasEdits()
                                ? "MIDI range stored from the original source. Use APPLY TO RESYNTH first if you want to bake the tone edits into a working reference."
                                : "Selection stored as the Melody Lab MIDI analysis range.",
                            juce::dontSendNotification);
        };
        cutToNewReference.onClick = [this] { chooseCutDestination(); };

        duration = juce::jmax (0.0, (double) proc.getReferenceAnalysisDuration());
        selectionStart = juce::jlimit (0.0, duration, (double) proc.getAnalysisStartSeconds());
        selectionEnd = proc.getAnalysisEndSeconds() > selectionStart
                           ? juce::jlimit (selectionStart, duration, (double) proc.getAnalysisEndSeconds())
                           : duration;
        updateRanges();
        updateControls();
        updateView();
        status.setText ("Bright area = selected audio. Drag upper handles for START/END, drag inside to move, lower handles set fades. Wheel zooms around the pointer.",
                        juce::dontSendNotification);
        startTimerHz (12);
    }

    ~ReferenceEditorDialog() override
    {
        stopTimer();
        thumbnail.removeChangeListener (this);
        proc.stopReferencePreview();
        proc.setReferenceAuditionMode (previousAuditionMode);
        setLookAndFeel (nullptr);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff070a0c));
        const auto p = palette();
        auto body = getLocalBounds().toFloat().reduced (7.0f);
        RetroHardware3D::drawRecessedPanel (g, body, p, 10.0f);

        g.setColour (p.secondary);
        g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
        g.drawText ("RM-01  /  REFERENCE EDITOR", 22, 11, 260, 24, juce::Justification::centredLeft, true);
        g.setColour (juce::Colour (0xffa9bbb5));
        g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
        g.drawText (proc.getReferenceFile().getFileName() + "  /  " + juce::String (duration, 2) + " s",
                    286, 11, getWidth() - 308, 24, juce::Justification::centredRight, true);

        const juce::StringArray methods { "Balanced Hybrid", "Reference Wavetable", "Spectral Subtractive",
                                           "FM / Harmonic", "Layered Studio", "Texture / Chop" };
        const juce::StringArray depths { "Classic / 1-3", "Studio / 4", "Deep / 6", "Maximum / 8" };
        const int methodIndex = juce::jlimit (0, methods.size() - 1, (int) std::lround (proc.apvts.getRawParameterValue ("resynthStrategy")->load()));
        const int depthIndex = juce::jlimit (0, depths.size() - 1, (int) std::lround (proc.apvts.getRawParameterValue ("resynthComplexity")->load()));
        g.setColour (p.primary);
        g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
        g.drawText ("RESYNTH METHOD  " + methods[methodIndex] + "    /    DEPTH  " + depths[depthIndex],
                    22, 35, getWidth() - 44, 16, juce::Justification::centredLeft, true);

        drawSection (g, editSectionBounds, "SELECTION + VIEW");
        drawSection (g, toneSectionBounds, "REFERENCE TONE + FADES");
        drawSection (g, actionSectionBounds, "REFERENCE ACTIONS");
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (18);
        area.removeFromTop (50);
        const int waveH = juce::jlimit (245, 350, (int) std::round (area.getHeight() * 0.44));
        waveform.setBounds (area.removeFromTop (waveH).reduced (1));
        area.removeFromTop (8);

        editSectionBounds = area.removeFromTop (82).toFloat();
        auto edit = editSectionBounds.toNearestInt().reduced (10, 7);
        edit.removeFromTop (22);
        auto timeRow = edit.removeFromTop (25);
        startLabel.setBounds (timeRow.removeFromLeft (48));
        startTime.setBounds (timeRow.removeFromLeft (juce::jmax (150, timeRow.getWidth() / 3)).reduced (2, 0));
        timeRow.removeFromLeft (8);
        endLabel.setBounds (timeRow.removeFromLeft (38));
        endTime.setBounds (timeRow.reduced (2, 0));
        auto viewRow = edit.removeFromTop (25);
        zoomLabel.setBounds (viewRow.removeFromLeft (48));
        zoom.setBounds (viewRow.removeFromLeft (viewRow.getWidth() / 2).reduced (2, 0));
        panLabel.setBounds (viewRow.removeFromLeft (38));
        pan.setBounds (viewRow.reduced (2, 0));

        area.removeFromTop (7);
        toneSectionBounds = area.removeFromTop (148).toFloat();
        auto tone = toneSectionBounds.toNearestInt().reduced (9, 7);
        tone.removeFromTop (22);
        const int cells = 7;
        const int cellW = juce::jmax (1, tone.getWidth() / cells);
        std::array<juce::Label*, cells> toneLabels {{ &fadeInLabel, &lowCutLabel, &lowLabel, &midLabel, &highLabel, &highCutLabel, &fadeOutLabel }};
        std::array<juce::Slider*, cells> toneKnobs {{ &fadeIn, &lowCut, &lowGain, &midGain, &highGain, &highCut, &fadeOut }};
        for (int i = 0; i < cells; ++i)
        {
            auto cell = juce::Rectangle<int> (tone.getX() + i * cellW, tone.getY(),
                                              i == cells - 1 ? tone.getRight() - (tone.getX() + i * cellW) : cellW,
                                              tone.getHeight()).reduced (3, 0);
            toneLabels[(size_t) i]->setBounds (cell.removeFromTop (18));
            toneKnobs[(size_t) i]->setBounds (cell.reduced (2, 0));
        }

        area.removeFromTop (7);
        auto utility = area.removeFromTop (34);
        normalizePreview.setBounds (utility.removeFromLeft (198).reduced (2));
        fullSelection.setBounds (utility.removeFromLeft (120).reduced (2));
        fitSelection.setBounds (utility.removeFromLeft (120).reduced (2));
        preview.setBounds (utility.removeFromLeft (104).reduced (2));
        stopPreview.setBounds (utility.removeFromLeft (78).reduced (2));
        resetEdits.setBounds (utility.removeFromLeft (105).reduced (2));

        area.removeFromTop (5);
        actionSectionBounds = area.removeFromTop (72).toFloat();
        auto actions = actionSectionBounds.toNearestInt().reduced (9, 7);
        actions.removeFromTop (22);
        const int third = actions.getWidth() / 3;
        applyResynth.setBounds (actions.removeFromLeft (third).reduced (2));
        useForMidi.setBounds (actions.removeFromLeft (third).reduced (2));
        cutToNewReference.setBounds (actions.reduced (2));

        area.removeFromTop (4);
        status.setBounds (area.removeFromTop (juce::jmax (28, area.getHeight())).reduced (3, 1));
        status.setColour (juce::Label::textColourId, juce::Colour (0xffa9bbb5));
        status.setFont (juce::Font (juce::FontOptions (9.5f)));
        status.setJustificationType (juce::Justification::centredLeft);
    }

private:
    class Waveform final : public juce::Component
    {
    public:
        explicit Waveform (ReferenceEditorDialog& o) : owner (o)
        {
            setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
            setTitle ("Large reference waveform. Bright area is selected. Upper handles edit selection; lower handles edit fades; mouse wheel zooms.");
        }

        void paint (juce::Graphics& g) override
        {
            auto bounds = getLocalBounds().toFloat();
            const auto p = owner.palette();
            RetroHardware3D::drawPhosphorDisplay (g, bounds, p);
            auto plot = bounds.reduced (13.0f, 28.0f);
            if (owner.duration <= 0.0)
            {
                g.setColour (juce::Colour (0xff71847e));
                g.drawText ("No reference loaded", plot, juce::Justification::centred);
                return;
            }

            g.setColour (p.primary.withAlpha (0.34f));
            owner.thumbnail.drawChannels (g, plot.toNearestInt(), owner.viewStart, owner.viewEnd, 1.0f);
            g.setColour (p.primary.withAlpha (0.10f));
            g.drawHorizontalLine ((int) plot.getCentreY(), plot.getX(), plot.getRight());

            const double leftTime = juce::jmax (owner.selectionStart, owner.viewStart);
            const double rightTime = juce::jmin (owner.selectionEnd, owner.viewEnd);
            if (rightTime > leftTime)
            {
                const float left = xForTime (leftTime, plot);
                const float right = xForTime (rightTime, plot);

                // Make the selection unmistakable: dim the non-selected audio,
                // fill the selected window, then redraw its waveform at full brightness.
                g.setColour (juce::Colours::black.withAlpha (0.48f));
                if (left > plot.getX()) g.fillRect (plot.getX(), plot.getY(), left - plot.getX(), plot.getHeight());
                if (right < plot.getRight()) g.fillRect (right, plot.getY(), plot.getRight() - right, plot.getHeight());

                g.setGradientFill (juce::ColourGradient (p.primary.withAlpha (0.26f), left, plot.getY(),
                                                         p.secondary.withAlpha (0.10f), right, plot.getBottom(), false));
                g.fillRect (left, plot.getY(), right - left, plot.getHeight());
                g.setColour (p.primary.withAlpha (0.88f));
                g.drawRect (juce::Rectangle<float> (left, plot.getY(), right - left, plot.getHeight()), 1.4f);

                g.saveState();
                g.reduceClipRegion (juce::Rectangle<float> (left, plot.getY(), right - left, plot.getHeight()).toNearestInt());
                g.setColour (p.primary.brighter (0.48f));
                owner.thumbnail.drawChannels (g, plot.toNearestInt(), owner.viewStart, owner.viewEnd, 1.0f);
                g.restoreState();
            }

            for (int i = 0; i < 2; ++i)
            {
                const double value = i == 0 ? owner.selectionStart : owner.selectionEnd;
                if (value < owner.viewStart || value > owner.viewEnd) continue;
                const float x = xForTime (value, plot);
                const auto colour = i == 0 ? p.secondary : p.tertiary;
                g.setColour (colour.withAlpha (0.24f));
                g.fillRect (x - 4.0f, plot.getY(), 8.0f, plot.getHeight());
                g.setColour (colour);
                g.fillRect (x - 1.4f, plot.getY(), 2.8f, plot.getHeight());
                g.fillRoundedRectangle (x - 8.0f, plot.getY() + 2.0f, 16.0f, 24.0f, 4.0f);
                g.setColour (juce::Colours::black.withAlpha (0.55f));
                g.drawVerticalLine ((int) x, plot.getY() + 7.0f, plot.getY() + 20.0f);
            }

            drawFadeEnvelope (g, plot, p);

            g.setColour (juce::Colour (0xffc9dad5));
            g.setFont (juce::Font (juce::FontOptions (9.0f, juce::Font::bold)));
            g.drawText (owner.formatTime (owner.viewStart), (int) plot.getX(), 5, 120, 18, juce::Justification::centredLeft);
            g.drawText (owner.formatTime (owner.viewEnd), (int) plot.getRight() - 120, 5, 120, 18, juce::Justification::centredRight);
            g.drawText ("SELECTED  " + owner.formatTime (owner.selectionStart) + "  -  " + owner.formatTime (owner.selectionEnd)
                        + "  /  " + juce::String (owner.selectionLength(), 3) + " s",
                        125, 5, juce::jmax (1, getWidth() - 250), 18, juce::Justification::centred, true);

            if (owner.normalizePreview.getToggleState())
            {
                auto badge = juce::Rectangle<float> (plot.getRight() - 102.0f, plot.getY() + 5.0f, 94.0f, 20.0f);
                g.setColour (p.secondary.withAlpha (0.20f)); g.fillRoundedRectangle (badge, 4.0f);
                g.setColour (p.secondary); g.drawRoundedRectangle (badge, 4.0f, 1.0f);
                g.setFont (juce::Font (juce::FontOptions (8.5f, juce::Font::bold)));
                g.drawText ("NORM  -1 dBFS", badge, juce::Justification::centred);
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (owner.duration <= 0.0) return;
            auto plot = getLocalBounds().toFloat().reduced (13.0f, 28.0f);
            const double t = timeForX (e.position.x, plot);
            const float sx = xForTime (owner.selectionStart, plot);
            const float ex = xForTime (owner.selectionEnd, plot);
            const float fix = xForTime (owner.selectionStart + owner.fadeIn.getValue(), plot);
            const float fox = xForTime (owner.selectionEnd - owner.fadeOut.getValue(), plot);

            if (e.position.y > plot.getBottom() - 30.0f && std::abs (e.position.x - fix) <= 12.0f) dragMode = 4;
            else if (e.position.y > plot.getBottom() - 30.0f && std::abs (e.position.x - fox) <= 12.0f) dragMode = 5;
            else if (std::abs (e.position.x - sx) <= 11.0f) dragMode = 1;
            else if (std::abs (e.position.x - ex) <= 11.0f) dragMode = 2;
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
            auto plot = getLocalBounds().toFloat().reduced (13.0f, 28.0f);
            const double t = timeForX (e.position.x, plot);
            if (dragMode == 1) owner.setSelection (t, owner.selectionEnd, true);
            else if (dragMode == 2) owner.setSelection (owner.selectionStart, t, true);
            else if (dragMode == 3)
            {
                const double length = anchorEnd - anchorStart;
                const double delta = t - anchorTime;
                const double first = juce::jlimit (0.0, juce::jmax (0.0, owner.duration - length), anchorStart + delta);
                owner.setSelection (first, first + length, true);
            }
            else if (dragMode == 4)
            {
                owner.fadeIn.setValue (juce::jlimit (0.0, owner.selectionLength() * 0.5, t - owner.selectionStart),
                                       juce::sendNotificationSync);
            }
            else if (dragMode == 5)
            {
                owner.fadeOut.setValue (juce::jlimit (0.0, owner.selectionLength() * 0.5, owner.selectionEnd - t),
                                        juce::sendNotificationSync);
            }
        }

        void mouseUp (const juce::MouseEvent&) override { dragMode = 0; }

        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
        {
            if (owner.duration <= 0.0 || std::abs (wheel.deltaY) < 0.0001f) return;
            auto plot = getLocalBounds().toFloat().reduced (13.0f, 28.0f);
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

        void drawFadeEnvelope (juce::Graphics& g, juce::Rectangle<float> plot, const RetroHardware3D::Palette& p)
        {
            if (owner.selectionLength() <= 0.0) return;
            const double fiTime = owner.selectionStart + owner.fadeIn.getValue();
            const double foTime = owner.selectionEnd - owner.fadeOut.getValue();
            const float sx = xForTime (owner.selectionStart, plot);
            const float ex = xForTime (owner.selectionEnd, plot);
            const float fix = xForTime (fiTime, plot);
            const float fox = xForTime (foTime, plot);
            const float top = plot.getY() + 8.0f;
            const float bottom = plot.getBottom() - 10.0f;
            const float centre = plot.getCentreY();

            juce::Path envelope;
            if (owner.fadeIn.getValue() > 0.0005)
            {
                envelope.startNewSubPath (sx, centre); envelope.lineTo (fix, top);
                envelope.startNewSubPath (sx, centre); envelope.lineTo (fix, bottom);
            }
            if (owner.fadeOut.getValue() > 0.0005)
            {
                envelope.startNewSubPath (fox, top); envelope.lineTo (ex, centre);
                envelope.startNewSubPath (fox, bottom); envelope.lineTo (ex, centre);
            }
            g.setColour (p.secondary.withAlpha (0.22f));
            g.strokePath (envelope, juce::PathStrokeType (5.0f));
            g.setColour (p.secondary);
            g.strokePath (envelope, juce::PathStrokeType (1.4f));

            for (const auto x : { fix, fox })
            {
                if (x < plot.getX() - 8.0f || x > plot.getRight() + 8.0f) continue;
                auto handle = juce::Rectangle<float> (x - 7.0f, plot.getBottom() - 22.0f, 14.0f, 14.0f);
                g.setColour (p.secondary.withAlpha (0.20f)); g.fillEllipse (handle.expanded (4.0f));
                g.setColour (p.secondary); g.fillEllipse (handle);
                g.setColour (juce::Colour (0xff101719)); g.fillEllipse (handle.reduced (4.0f));
            }
        }

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
    RetroLookAndFeel laf;
    juce::AudioFormatManager formats;
    juce::AudioThumbnailCache cache { 8 };
    juce::AudioThumbnail thumbnail;
    Waveform waveform;
    RetroMatchSynthAudioProcessor::ReferenceAuditionMode previousAuditionMode;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Label startLabel, endLabel, zoomLabel, panLabel, fadeInLabel, fadeOutLabel;
    juce::Label lowCutLabel, lowLabel, midLabel, highLabel, highCutLabel, status;
    juce::Slider startTime, endTime, zoom, pan, fadeIn, fadeOut;
    juce::Slider lowCut, lowGain, midGain, highGain, highCut;
    juce::TextButton normalizePreview { "NORMALIZE -1 dB + PREVIEW" }, resetEdits { "RESET EDITS" };
    juce::TextButton fullSelection { "SELECT FULL" }, fitSelection { "FIT SELECTION" }, preview { "PREVIEW" }, stopPreview { "STOP" };
    juce::TextButton applyResynth { "APPLY TO RESYNTH" }, useForMidi { "USE FOR MIDI" }, cutToNewReference { "CUT -> NEW REFERENCE" };
    juce::Rectangle<float> editSectionBounds, toneSectionBounds, actionSectionBounds;
    double duration = 0.0, selectionStart = 0.0, selectionEnd = 0.0, viewStart = 0.0, viewEnd = 0.0;

    RetroHardware3D::Palette palette() const
    {
        return { findColour (RetroLookAndFeel::primaryLed),
                 findColour (RetroLookAndFeel::secondaryLed),
                 findColour (RetroLookAndFeel::tertiaryLed) };
    }

    void drawSection (juce::Graphics& g, juce::Rectangle<float> bounds, const juce::String& titleText)
    {
        if (bounds.isEmpty()) return;
        const auto p = palette();
        RetroHardware3D::drawSectionPlate (g, bounds, p);
        g.setColour (p.secondary);
        g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
        g.drawText (titleText, bounds.withHeight (22.0f).reduced (10.0f, 0.0f), juce::Justification::centredLeft);
    }

    static juce::String formatTime (double seconds)
    {
        const int minutes = (int) (seconds / 60.0);
        const double rest = seconds - minutes * 60.0;
        return juce::String (minutes) + ":" + (rest < 10.0 ? "0" : "") + juce::String (rest, 3);
    }

    void setLabel (juce::Label& label, const juce::String& text, bool centred = false)
    {
        label.setText (text, juce::dontSendNotification);
        label.setColour (juce::Label::textColourId, centred ? juce::Colour (0xffb7c7c2) : juce::Colour (0xffaec2bc));
        label.setFont (juce::Font (juce::FontOptions (centred ? 8.5f : 9.5f, juce::Font::bold)));
        label.setJustificationType (centred ? juce::Justification::centred : juce::Justification::centredLeft);
    }

    static void setupTimeSlider (juce::Slider& slider)
    {
        slider.setSliderStyle (juce::Slider::IncDecButtons);
        slider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 95, 24);
        slider.setTextValueSuffix (" s");
        slider.setNumDecimalPlacesToDisplay (3);
    }

    static void setupViewSlider (juce::Slider& slider, double lo, double hi, const juce::String& suffix)
    {
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 22);
        slider.setRange (lo, hi, 0.001);
        slider.setTextValueSuffix (suffix);
        slider.setNumDecimalPlacesToDisplay (suffix.isNotEmpty() ? 2 : 3);
    }

    static void setupEditKnob (juce::Slider& slider, double lo, double hi, double value,
                               const juce::String& suffix, double midpoint = 0.0)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 76, 19);
        slider.setRange (lo, hi, (hi - lo) > 1000.0 ? 1.0 : 0.001);
        if (midpoint > lo && midpoint < hi) slider.setSkewFactorFromMidPoint (midpoint);
        slider.setValue (value, juce::dontSendNotification);
        slider.setTextValueSuffix (suffix);
        slider.setMouseDragSensitivity (190);
        slider.setNumDecimalPlacesToDisplay ((hi - lo) > 1000.0 ? 0 : ((hi - lo) > 100.0 ? 1 : 2));
    }

    ReferenceSamplePlayer::EditTone currentTone() const
    {
        ReferenceSamplePlayer::EditTone tone;
        tone.lowCutHz = (float) lowCut.getValue();
        tone.lowGainDb = (float) lowGain.getValue();
        tone.midGainDb = (float) midGain.getValue();
        tone.highGainDb = (float) highGain.getValue();
        tone.highCutHz = (float) highCut.getValue();
        return tone;
    }

    bool hasEdits() const
    {
        return normalizePreview.getToggleState() || fadeIn.getValue() > 0.0005 || fadeOut.getValue() > 0.0005
               || ! currentTone().isNeutral();
    }

    void resetEditControls()
    {
        normalizePreview.setToggleState (false, juce::dontSendNotification);
        fadeIn.setValue (0.0, juce::dontSendNotification);
        fadeOut.setValue (0.0, juce::dontSendNotification);
        lowCut.setValue (20.0, juce::dontSendNotification);
        lowGain.setValue (0.0, juce::dontSendNotification);
        midGain.setValue (0.0, juce::dontSendNotification);
        highGain.setValue (0.0, juce::dontSendNotification);
        highCut.setValue (20000.0, juce::dontSendNotification);
        waveform.repaint();
    }

    double selectionLength() const { return juce::jmax (0.0, selectionEnd - selectionStart); }

    void clampFades()
    {
        const double maxFade = juce::jmin (10.0, selectionLength() * 0.5);
        fadeIn.setRange (0.0, maxFade, 0.001);
        fadeOut.setRange (0.0, maxFade, 0.001);
        if (fadeIn.getValue() > maxFade) fadeIn.setValue (maxFade, juce::dontSendNotification);
        if (fadeOut.getValue() > maxFade) fadeOut.setValue (maxFade, juce::dontSendNotification);
    }

    void setSelection (double first, double last, bool updateSliders)
    {
        if (duration <= 0.0) return;
        constexpr double gap = 0.002;
        first = juce::jlimit (0.0, juce::jmax (0.0, duration - gap), first);
        last = juce::jlimit (juce::jmin (duration, first + gap), duration, last);
        selectionStart = first;
        selectionEnd = last;
        if (updateSliders) updateControls();
        clampFades();
        waveform.repaint();
    }

    void updateRanges()
    {
        const double d = juce::jmax (0.002, duration);
        startTime.setRange (0.0, d, 0.001);
        endTime.setRange (0.0, d, 0.001);
        clampFades();
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
        const double desired = juce::jlimit (0.0, maxStart,
            (selectionStart + selectionEnd) * 0.5 - (viewEnd - viewStart) * 0.5);
        pan.setValue (maxStart > 0.0 ? desired / maxStart : 0.0, juce::dontSendNotification);
        updateView();
        waveform.repaint();
    }

    void previewNow (const juce::String& successText)
    {
        if (selectionLength() <= 0.001) return;
        const bool limited = selectionLength() > 180.0;
        const bool ok = proc.previewReferenceRegionEdited ((float) selectionStart, (float) selectionEnd,
                                                           normalizePreview.getToggleState(),
                                                           (float) fadeIn.getValue(), (float) fadeOut.getValue(),
                                                           currentTone());
        status.setText (ok ? (limited ? successText + " Preview is limited to the first 180 s of the selection."
                                             : successText)
                           : "Could not preview the selected range.",
                        juce::dontSendNotification);
    }

    void applySelectionToResynth()
    {
        proc.stopReferencePreview();
        if (! hasEdits())
        {
            const bool ok = proc.setReferenceAnalysisRegion ((float) selectionStart, (float) selectionEnd);
            status.setText (ok ? "Selection applied to resynthesis analysis."
                               : "Could not analyze this selection.",
                            juce::dontSendNotification);
            return;
        }

        auto target = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getNonexistentChildFile ("RetroMatch-edited-reference", ".wav", false);
        const bool written = proc.exportReferenceSelectionEdited (target,
            (float) selectionStart, (float) selectionEnd, normalizePreview.getToggleState(),
            (float) fadeIn.getValue(), (float) fadeOut.getValue(), currentTone());
        if (! written || ! proc.loadReferenceSample (target))
        {
            status.setText ("Could not create the edited working reference.", juce::dontSendNotification);
            return;
        }

        thumbnail.setSource (new juce::FileInputSource (target));
        duration = juce::jmax (0.0, (double) proc.getReferenceAnalysisDuration());
        selectionStart = 0.0;
        selectionEnd = duration;
        zoom.setValue (1.0, juce::dontSendNotification);
        pan.setValue (0.0, juce::dontSendNotification);
        resetEditControls();
        updateRanges(); updateControls(); updateView();
        const bool ok = proc.setReferenceAnalysisRegion (0.0f, (float) duration);
        status.setText (ok ? "EQ / fades / normalization were rendered non-destructively to a working reference and applied to resynthesis."
                           : "Edited reference was created, but analysis failed.",
                        juce::dontSendNotification);
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
                const bool written = safe->proc.exportReferenceSelectionEdited (target,
                    (float) safe->selectionStart, (float) safe->selectionEnd,
                    safe->normalizePreview.getToggleState(), (float) safe->fadeIn.getValue(),
                    (float) safe->fadeOut.getValue(), safe->currentTone());
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
                safe->resetEditControls();
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

    void timerCallback() override
    {
        const int activePalette = proc.lightPalette.load();
        if (activePalette != displayedPalette)
        {
            displayedPalette = activePalette;
            laf.setPalette (activePalette);
            repaint(); waveform.repaint();
        }
        if (isVisible()) waveform.repaint();
    }

    int displayedPalette = -1;
};