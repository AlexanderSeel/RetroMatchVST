#pragma once
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
