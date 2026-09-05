#pragma once
#include "../PluginProcessor.h"
#include "RetroLookAndFeel.h"

class MelodyPage final : public juce::Component, private juce::Timer
{
public:
    explicit MelodyPage (RetroMatchSynthAudioProcessor& p) : proc (p), roll (*this)
    {
        for (auto* button : std::array<juce::Button*, 9> { &analyze, &play, &samplePlay, &stop, &save, &drag, &lower, &higher, &remove }) addAndMakeVisible (*button);
        addAndMakeVisible (mode); addAndMakeVisible (tempo); addAndMakeVisible (hint); addAndMakeVisible (roll);
        for (auto* slider : { &synthStart, &synthEnd, &midiStart, &midiEnd })
        { addAndMakeVisible (*slider); slider->setRange (0.0, 60.0, 0.01); slider->setSliderStyle (juce::Slider::LinearHorizontal); slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, 58, 20); slider->setTextValueSuffix (" s"); }
        addAndMakeVisible (applyRegion); addAndMakeVisible (synthLabel); addAndMakeVisible (midiLabel); addAndMakeVisible (sampleView);
        synthLabel.setText ("SYNTH REGION", juce::dontSendNotification); midiLabel.setText ("MIDI REGION", juce::dontSendNotification);
        mode.addItemList ({ "MELODY", "LAYERED / EXPERIMENTAL" }, 1); mode.setSelectedId (1);
        mode.setTooltip ("Melody follows one predominant line. Layered estimates up to four simultaneous notes; overlapping harmonics can cause errors.");
        tempo.setRange (30, 300, 1); tempo.setSliderStyle (juce::Slider::LinearHorizontal);
        tempo.setTextBoxStyle (juce::Slider::TextBoxRight, false, 80, 24); tempo.setTextValueSuffix (" BPM");
        tempo.setTooltip ("Export tempo / piano-roll grid. Changing it preserves the recording's note times; no automatic beat detection or quantization.");
        hint.setColour (juce::Label::textColourId, juce::Colour (0xffa6bcb9));
        hint.setFont (juce::Font (juce::FontOptions (12.0f)));
        analyze.onClick = [this]
        {
            if (worker) { worker->signalThreadShouldExit(); return; }
            const auto file = proc.getReferenceFile();
            if (! file.existsAsFile()) { hint.setText ("Load a reference recording first.", juce::dontSendNotification); return; }
            worker = std::make_unique<Worker> (file, mode.getSelectedId() == 2, tempo.getValue(), midiStart.getValue(), midiEnd.getValue());
            worker->startThread(); analyze.setButtonText ("CANCEL"); proc.melodyTransport.stop();
        };
        play.onClick = [this] { proc.playMelody(); };
        samplePlay.onClick = [this] { proc.melodyTransport.stop(); proc.setReferenceAuditionMode (RetroMatchSynthAudioProcessor::ReferenceAuditionMode::referenceOnly); proc.noteOnFromEditor (proc.getReferenceBaseMidiNote(), 0.82f); };
        play.setTooltip ("Play the extracted notes through the current synth and enabled layers.");
        stop.onClick = [this] { proc.melodyTransport.stop(); proc.allEditorNotesOff(); proc.setReferenceAuditionMode (RetroMatchSynthAudioProcessor::ReferenceAuditionMode::synthOnly); };
        save.onClick = [this] { chooseMidi(); };
        drag.begin = [this]
        {
            auto directory = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("RetroMatch-MIDI");
            if (directory.createDirectory().failed()) { hint.setText ("Cannot create MIDI drag file.", juce::dontSendNotification); return; }
            auto file = directory.getChildFile ("RetroMatch-" + juce::Uuid().toString() + ".mid");
            if (! clip.writeMidi (file)) { hint.setText ("MIDI export failed.", juce::dontSendNotification); return; }
            const bool started = juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, &drag);
            hint.setText (started ? "Drop onto a DAW MIDI track. Load RetroMatch on that track to use this patch."
                                 : "This host did not start an external drag. Use EXPORT MIDI instead.", juce::dontSendNotification);
        };
        lower.onClick = [this] { editSelected (-1); }; higher.onClick = [this] { editSelected (1); };
        remove.onClick = [this] { editSelected (0); };
        tempo.onValueChange = [this] { clip.bpm = tempo.getValue(); if (! clip.notes.empty()) proc.setMelodyClip (clip); };
        applyRegion.onClick = [this]
        {
            if (proc.setReferenceAnalysisRegion ((float) synthStart.getValue(), (float) synthEnd.getValue()))
            { hint.setText ("Synthesis analysis region applied. Analyze MIDI again if needed.", juce::dontSendNotification); refreshClip(); }
        };
        refreshClip(); startTimerHz (25);
    }
    ~MelodyPage() override
    {
        stopTimer(); proc.melodyTransport.stop(); proc.allEditorNotesOff();
        if (worker) { worker->signalThreadShouldExit(); worker->waitForThreadToExit (-1); }
    }
    void resized() override
    {
        auto area = getLocalBounds().reduced (16);
        area.removeFromTop (34);
        auto tools = area.removeFromTop (34);
        analyze.setBounds (tools.removeFromLeft (105).reduced (2)); mode.setBounds (tools.removeFromLeft (210).reduced (3));
        tempo.setBounds (tools.reduced (4));
        area.removeFromTop (4); auto synth = area.removeFromTop (27);
        synthLabel.setBounds (synth.removeFromLeft (105).reduced (2)); synthStart.setBounds (synth.removeFromLeft (125).reduced (2)); synthEnd.setBounds (synth.removeFromLeft (125).reduced (2)); applyRegion.setBounds (synth.reduced (2));
        auto midi = area.removeFromTop (27);
        midiLabel.setBounds (midi.removeFromLeft (105).reduced (2)); midiStart.setBounds (midi.removeFromLeft (125).reduced (2)); midiEnd.setBounds (midi.reduced (2));
        area.removeFromTop (5); auto actions = area.removeFromTop (34);
        play.setBounds (actions.removeFromLeft (112).reduced (2)); samplePlay.setBounds (actions.removeFromLeft (108).reduced (2)); stop.setBounds (actions.removeFromLeft (65).reduced (2));
        save.setBounds (actions.removeFromLeft (125).reduced (2)); drag.setBounds (actions.removeFromLeft (155).reduced (2));
        area.removeFromTop (6); sampleView.setBounds (area.removeFromTop (112)); area.removeFromTop (8); auto edit = area.removeFromTop (28);
        lower.setBounds (edit.removeFromLeft (90).reduced (2)); higher.setBounds (edit.removeFromLeft (90).reduced (2));
        remove.setBounds (edit.removeFromLeft (100).reduced (2));
        hint.setBounds (area.removeFromBottom (62)); area.removeFromTop (10); roll.setBounds (area);
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff101719));
        g.setColour (findColour (RetroLookAndFeel::primaryLed));
        g.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
        g.drawText ("MELODY LAB  /  AUDIO > NOTES > YOUR SYNTH", 18, 12, getWidth() - 36, 25, juce::Justification::centredLeft);
    }
private:
    struct Worker final : juce::Thread
    {
        Worker (juce::File f, bool l, double b, double s, double e) : Thread ("Melody transcription"), file (f), layered (l), bpm (b), start (s), end (e) {}
        void run() override { result = MelodyAnalyzer::analyzeFile (file, layered, bpm, start, end, [this] { return threadShouldExit(); }, [this] (float v) { progress.store (v); }); }
        juce::File file; bool layered; double bpm, start, end; MelodyClip result; std::atomic<float> progress { 0.0f };
    };
    struct DragButton final : juce::TextButton
    {
        DragButton() : TextButton ("DRAG MIDI TO DAW") { setTooltip ("Press and drag this button onto a MIDI track in a compatible DAW. Export MIDI is the fallback."); }
        void mouseDown (const juce::MouseEvent& e) override { started = false; TextButton::mouseDown (e); }
        void mouseDrag (const juce::MouseEvent& e) override
        { TextButton::mouseDrag (e); if (! started && isEnabled() && e.getDistanceFromDragStart() > 6) { started = true; if (begin) begin(); } }
        std::function<void()> begin; bool started = false;
    };
    struct PianoRoll final : juce::Component
    {
        explicit PianoRoll (MelodyPage& p) : page (p) {}
        juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced (10).withTrimmedLeft (40).withTrimmedTop (22); }
        void range (int& low, int& high) const
        { low = 60; high = 72; for (const auto& n : page.clip.notes) { low = std::min (low, n.pitch - 2); high = std::max (high, n.pitch + 2); } }
        juce::Rectangle<float> rect (const TranscribedNote& n) const
        {
            int low, high; range (low, high); const auto p = plot(); const float row = p.getHeight() / (high - low + 1);
            const double seconds = std::max (1.0, page.clip.duration);
            return { p.getX() + (float) (n.start / seconds) * p.getWidth(), p.getY() + (high - n.pitch) * row,
                     std::max (3.0f, (float) (n.duration / seconds) * p.getWidth()), std::max (2.0f, row - 1.5f) };
        }
        void mouseDown (const juce::MouseEvent& e) override
        {
            page.selected = -1;
            for (int i = (int) page.clip.notes.size() - 1; i >= 0; --i)
                if (rect (page.clip.notes[(size_t) i]).contains (e.position)) { page.selected = i; break; }
            page.updateHint(); repaint();
        }
        void paint (juce::Graphics& g) override
        {
            auto bounds = getLocalBounds().toFloat(); auto p = plot();
            const auto led = findColour (RetroLookAndFeel::primaryLed);
            g.setColour (juce::Colour (0xff03090c)); g.fillRoundedRectangle (bounds, 8);
            g.setColour (juce::Colour (0xff566467)); g.drawRoundedRectangle (bounds.reduced (1), 8, 1);
            int low, high; range (low, high); float row = p.getHeight() / (high - low + 1);
            g.setFont (juce::Font (juce::FontOptions (9.0f)));
            for (int pitch = low; pitch <= high; ++pitch)
            {
                const float y = p.getY() + (high - pitch) * row;
                const bool black = juce::MidiMessage::isMidiNoteBlack (pitch);
                g.setColour (black ? juce::Colour (0xff0b1317) : juce::Colour (0xff142024));
                g.fillRect (p.getX(), y, p.getWidth(), row - 0.5f);
                if (pitch % 12 == 0 || high - low < 20)
                { g.setColour (led.withAlpha (0.65f)); g.drawText (juce::MidiMessage::getMidiNoteName (pitch, true, true, 3), 3, (int) y, 44, (int) std::max (10.0f, row), juce::Justification::centred); }
            }
            const double seconds = std::max (1.0, page.clip.duration), beat = 60.0 / page.clip.bpm;
            for (int b = 0; b * beat <= seconds; ++b)
            {
                float x = p.getX() + (float) (b * beat / seconds) * p.getWidth();
                g.setColour (led.withAlpha (b % 4 == 0 ? 0.22f : 0.07f)); g.drawVerticalLine ((int) x, p.getY(), p.getBottom());
                if (b % 4 == 0) { g.setColour (led.withAlpha (0.65f)); g.drawText (juce::String (b / 4 + 1), (int) x, 4, 30, 20, juce::Justification::centredLeft); }
            }
            for (int i = 0; i < (int) page.clip.notes.size(); ++i)
            {
                const auto& note = page.clip.notes[(size_t) i]; const auto r = rect (note);
                const auto colour = i == page.selected ? findColour (RetroLookAndFeel::secondaryLed) : led;
                g.setColour (colour.withAlpha (0.09f)); g.fillRoundedRectangle (r.expanded (3), 3);
                g.setColour (colour.withAlpha (0.35f + note.confidence * 0.55f)); g.fillRoundedRectangle (r, 2);
                g.setColour (colour.brighter (0.6f)); g.drawLine (r.getX(), r.getY() + 1, r.getRight(), r.getY() + 1, 1);
            }
            if (page.proc.melodyTransport.isPlaying())
            { const float x = p.getX() + (float) (page.proc.melodyTransport.getPosition() / seconds) * p.getWidth(); g.setColour (juce::Colours::white); g.drawLine (x, p.getY(), x, p.getBottom(), 1.5f); }
            if (page.clip.notes.empty())
            { g.setColour (led.withAlpha (0.6f)); g.setFont (juce::Font (juce::FontOptions (16.0f))); g.drawText ("Analyze a reference to reveal its notes", p, juce::Justification::centred); }
        }
        MelodyPage& page;
    };
    struct SampleRangeView final : juce::Component
    {
        explicit SampleRangeView (MelodyPage& p) : page (p) { setMouseCursor (juce::MouseCursor::LeftRightResizeCursor); }
        void paint (juce::Graphics& g) override
        {
            auto bounds = getLocalBounds().toFloat().reduced (2); const auto led = findColour (RetroLookAndFeel::primaryLed);
            g.setColour (juce::Colour (0xff03090c)); g.fillRoundedRectangle (bounds, 7); g.setColour (juce::Colour (0xff52656a)); g.drawRoundedRectangle (bounds, 7, 1);
            auto plot = bounds.reduced (10, 20); g.setColour (led.withAlpha (0.12f)); g.fillRect (plot);
            if (page.proc.currentFeatures)
            {
                juce::Path wave; const auto& points = page.proc.currentFeatures->waveformPreview;
                for (size_t i = 0; i < points.size(); ++i) { const float x = plot.getX() + (float) i / (points.size() - 1) * plot.getWidth(); const float y = plot.getCentreY() - points[i] * plot.getHeight() * 0.45f; if (i == 0) wave.startNewSubPath (x, y); else wave.lineTo (x, y); }
                g.setColour (led.withAlpha (0.18f)); g.strokePath (wave, juce::PathStrokeType (5.0f)); g.setColour (led); g.strokePath (wave, juce::PathStrokeType (1.2f));
            }
            const double duration = std::max (0.01, (double) page.proc.getReferenceAnalysisDuration());
            const float startX = plot.getX() + (float) (page.synthStart.getValue() / duration) * plot.getWidth(), endX = plot.getX() + (float) (page.synthEnd.getValue() / duration) * plot.getWidth();
            g.setColour (led.withAlpha (0.13f)); g.fillRect (startX, plot.getY(), std::max (0.0f, endX - startX), plot.getHeight());
            g.setColour (findColour (RetroLookAndFeel::secondaryLed)); g.drawLine (startX, bounds.getY() + 16, startX, plot.getBottom() + 2, 2.0f); g.drawLine (endX, bounds.getY() + 16, endX, plot.getBottom() + 2, 2.0f);
            g.setColour (led); g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold))); g.drawText ("SAMPLE PREVIEW  /  drag START + END", bounds.getX() + 10, bounds.getY() + 3, bounds.getWidth() - 20, 14, juce::Justification::centredLeft);
        }
        void mouseDown (const juce::MouseEvent& e) override { dragging = nearest (e.x); }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (dragging < 0) return; const double duration = std::max (0.01, (double) page.proc.getReferenceAnalysisDuration()); const auto plot = getLocalBounds().toFloat().reduced (10, 20); const double value = juce::jlimit (0.0, duration, (e.position.x - plot.getX()) / plot.getWidth() * duration);
            if (dragging == 0) page.synthStart.setValue (std::min (value, page.synthEnd.getValue() - 0.01)); else page.synthEnd.setValue (std::max (value, page.synthStart.getValue() + 0.01)); repaint();
        }
        int nearest (int x) const { const auto plot = getLocalBounds().toFloat().reduced (10, 20); const double duration = std::max (0.01, (double) page.proc.getReferenceAnalysisDuration()); const int sx = (int) (plot.getX() + page.synthStart.getValue() / duration * plot.getWidth()), ex = (int) (plot.getX() + page.synthEnd.getValue() / duration * plot.getWidth()); return std::abs (x - sx) < std::abs (x - ex) ? 0 : 1; }
        MelodyPage& page; int dragging = -1;
    };
    RetroMatchSynthAudioProcessor& proc;
    float shownStart = -1, shownEnd = -1;
    MelodyClip clip; juce::ValueTree previousState; int selected = -1; SampleRangeView sampleView { *this };
    std::unique_ptr<Worker> worker; std::unique_ptr<juce::FileChooser> chooser;
    juce::TextButton analyze { "ANALYZE" }, play { "PLAY MELODY" }, samplePlay { "PLAY SAMPLE" }, stop { "STOP" }, save { "EXPORT MIDI" };
    DragButton drag;
    juce::TextButton lower { "NOTE -" }, higher { "NOTE +" }, remove { "DELETE NOTE" };
    juce::ComboBox mode; juce::Slider tempo, synthStart, synthEnd, midiStart, midiEnd; juce::TextButton applyRegion { "APPLY SYNTH" }; juce::Label synthLabel, midiLabel, hint; PianoRoll roll;
    void refreshClip()
    {
        previousState = proc.apvts.state.getChildWithName ("MELODY"); clip = proc.getMelodyClip(); selected = -1;
        tempo.setValue (clip.bpm, juce::dontSendNotification); mode.setSelectedId (clip.layered ? 2 : 1, juce::dontSendNotification);
        const double duration = juce::jmax (0.01, (double) proc.getReferenceAnalysisDuration());
        for (auto* slider : { &synthStart, &synthEnd, &midiStart, &midiEnd }) slider->setRange (0.0, duration, 0.01);
        synthStart.setValue (proc.getAnalysisStartSeconds(), juce::dontSendNotification); synthEnd.setValue (proc.getAnalysisEndSeconds() > 0 ? proc.getAnalysisEndSeconds() : duration, juce::dontSendNotification);
        midiStart.setValue (0.0, juce::dontSendNotification); midiEnd.setValue (clip.duration > 0 ? clip.duration : duration, juce::dontSendNotification);
        updateHint(); roll.repaint();
        sampleView.repaint();
    }
    void updateHint()
    {
        juce::String text = juce::String ((int) clip.notes.size()) + " notes  /  " + juce::String (clip.duration, 2) + " s. ";
        if (juce::isPositiveAndBelow (selected, (int) clip.notes.size()))
        { const auto& n = clip.notes[(size_t) selected]; text += juce::MidiMessage::getMidiNoteName (n.pitch, true, true, 3) + " at " + juce::String (n.start, 2) + " s / confidence " + juce::String (n.confidence * 100, 0) + "%. "; }
        text += clip.truncated ? "Analysis limit reached (60 s / 4096 notes). " : "";
        text += "Click a note to correct/delete it. Mixed audio may contain extra or missed notes. MIDI carries notes; save the synth patch separately.";
        hint.setText (text, juce::dontSendNotification);
    }
    void editSelected (int delta)
    {
        if (! juce::isPositiveAndBelow (selected, (int) clip.notes.size())) return;
        const int edited = selected;
        if (delta == 0) clip.notes.erase (clip.notes.begin() + selected);
        else clip.notes[(size_t) selected].pitch = juce::jlimit (0, 127, clip.notes[(size_t) selected].pitch + delta);
        proc.setMelodyClip (clip); refreshClip();
        if (delta != 0) { selected = edited; updateHint(); }
    }
    void chooseMidi()
    {
        chooser = std::make_unique<juce::FileChooser> ("Export extracted notes", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("RetroMatch.mid"), "*.mid");
        juce::Component::SafePointer<MelodyPage> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
            [safe] (const juce::FileChooser& fc)
            {
                if (! safe || fc.getResult() == juce::File()) return;
                const auto file = fc.getResult().withFileExtension ("mid");
                safe->hint.setText (safe->clip.writeMidi (file) ? "MIDI exported: " + file.getFileName() : "Could not write MIDI file.", juce::dontSendNotification);
            });
    }
    void timerCallback() override
    {
        if (worker)
        {
            if (worker->file != proc.getReferenceFile()) worker->signalThreadShouldExit();
            if (! worker->isThreadRunning())
            {
                if (! worker->threadShouldExit()) { proc.setMelodyClip (worker->result); refreshClip(); }
                worker.reset(); analyze.setButtonText ("ANALYZE"); updateHint();
            }
            else hint.setText ("Analyzing locally... " + juce::String (worker->progress.load() * 100, 0) + "%", juce::dontSendNotification);
        }
        if (previousState != proc.apvts.state.getChildWithName ("MELODY")) refreshClip();
        const auto start = proc.getAnalysisStartSeconds();
        const auto end = proc.getAnalysisEndSeconds() > 0 ? proc.getAnalysisEndSeconds() : proc.getReferenceAnalysisDuration();
        if (start != shownStart || end != shownEnd)
        {
            const double duration = juce::jmax (0.01, (double) proc.getReferenceAnalysisDuration());
            synthStart.setRange (0, duration, 0.001); synthEnd.setRange (0, duration, 0.001);
            synthStart.setValue (start, juce::dontSendNotification); synthEnd.setValue (end, juce::dontSendNotification);
            shownStart = start; shownEnd = end; sampleView.repaint();
        }
        const bool ready = ! clip.notes.empty() && ! worker;
        play.setEnabled (ready); save.setEnabled (ready); drag.setEnabled (ready);
        samplePlay.setEnabled (proc.hasReferenceSample());
        lower.setEnabled (ready && selected >= 0); higher.setEnabled (ready && selected >= 0); remove.setEnabled (ready && selected >= 0);
        play.setToggleState (proc.melodyTransport.isPlaying(), juce::dontSendNotification);
        roll.repaint();
    }
};
