#pragma once

#include "../PluginProcessor.h"
#include "RetroLookAndFeel.h"
#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

class SequencerPanel final : public juce::Component, private juce::Timer
{
public:
    explicit SequencerPanel (RetroMatchSynthAudioProcessor& processor) : proc (processor)
    {
        setSize (820, 410);
        addAndMakeVisible (title); addAndMakeVisible (status);
        title.setText ("STEP SEQUENCER / ARPEGGIATOR", juce::dontSendNotification);
        title.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
        title.setColour (juce::Label::textColourId, findColour (RetroLookAndFeel::primaryLed));
        status.setJustificationType (juce::Justification::centredRight);
        status.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
        status.setColour (juce::Label::textColourId, findColour (RetroLookAndFeel::secondaryLed));

        for (auto* c : std::array<juce::Component*, 14> { &enabled, &mode, &division, &bpm, &length, &swing, &latch,
                                                           &octaveRange, &restartMode, &previousPage, &nextPage,
                                                           &randomize, &reverse, &clear })
            addAndMakeVisible (*c);
        addAndMakeVisible (rotateLeft); addAndMakeVisible (rotateRight);

        enabled.setButtonText ("SEQ ON");
        latch.setButtonText ("LATCH");
        mode.addItemList ({ "UP", "DOWN", "UP / DOWN", "DOWN / UP", "PLAYED ORDER", "CHORD", "RANDOM", "WALK", "PATTERN" }, 1);
        division.addItemList ({ "1/1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/8T", "1/16T", "1/8.", "1/16." }, 1);
        octaveRange.addItemList ({ "1 OCT", "2 OCT", "3 OCT", "4 OCT" }, 1);
        restartMode.addItemList ({ "FREE RUN", "TRANSPORT", "FIRST NOTE" }, 1);
        bpm.setRange (20.0, 400.0, 1.0); bpm.setTextValueSuffix (" BPM");
        length.setRange (1.0, RetroMatchSequencer::maxSteps, 1.0); length.setTextValueSuffix (" steps");
        swing.setRange (0.0, 95.0, 1.0); swing.setTextValueSuffix (" %");
        for (auto* slider : { &bpm, &length, &swing })
        {
            slider->setSliderStyle (juce::Slider::LinearHorizontal);
            slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, 72, 21);
        }
        previousPage.setButtonText ("< 16"); nextPage.setButtonText ("16 >");
        randomize.setButtonText ("RANDOMIZE"); reverse.setButtonText ("REVERSE");
        rotateLeft.setButtonText ("ROTATE <"); rotateRight.setButtonText ("ROTATE >"); clear.setButtonText ("CLEAR");

        for (int i = 0; i < (int) stepButtons.size(); ++i)
        {
            addAndMakeVisible (stepButtons[(size_t) i]);
            stepButtons[(size_t) i].onClick = [this, i]
            {
                selectedStep = juce::jlimit (0, RetroMatchSequencer::maxSteps - 1, page * stepsPerPage + i);
                refreshStepEditor(); refreshStepButtons();
            };
        }

        for (auto* c : std::array<juce::Component*, 13> { &stepLabel, &rest, &tie, &glide, &pitch, &octave, &velocity,
                                                           &gate, &probability, &ratchet, &microTiming, &macro1, &macro2 })
            addAndMakeVisible (*c);
        rest.setButtonText ("REST"); tie.setButtonText ("TIE"); glide.setButtonText ("GLIDE");
        stepLabel.setJustificationType (juce::Justification::centredLeft);
        stepLabel.setColour (juce::Label::textColourId, findColour (RetroLookAndFeel::secondaryLed));
        stepLabel.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));

        configureStepSlider (pitch, -48, 48, 1, " st");
        configureStepSlider (octave, -4, 4, 1, " oct");
        configureStepSlider (velocity, 0, 100, 1, " %");
        configureStepSlider (gate, 2, 100, 1, " %");
        configureStepSlider (probability, 0, 100, 1, " %");
        configureStepSlider (ratchet, 1, 8, 1, " x");
        configureStepSlider (microTiming, -45, 45, 1, " %");
        configureStepSlider (macro1, 0, 100, 1, " %");
        configureStepSlider (macro2, 0, 100, 1, " %");

        mode.setTooltip ("Arp modes use held MIDI notes. PATTERN uses ROOT + per-step pitch/octave and runs without held notes.");
        division.setTooltip ("Internal sequencer clock division. Host clock wiring is intentionally deferred until transport position is supplied to the core.");
        swing.setTooltip ("Alternating swing while preserving each two-step pair duration.");
        latch.setTooltip ("Keep held arpeggiator notes active after key release.");
        octaveRange.setTooltip ("Arpeggiator octave span for held-note modes. Pattern mode keeps using each step's explicit pitch/octave.");
        restartMode.setTooltip ("FREE RUN keeps phase, TRANSPORT follows a transport-start reset when supplied, FIRST NOTE restarts when a new held-note phrase begins.");
        probability.setTooltip ("Independent note trigger probability for this step.");
        microTiming.setTooltip ("Bounded offset within the nominal step, +/-45% maximum.");
        macro1.setTooltip ("Step modulation lane 1 value (stored now; destination routing comes with modulation-lane wiring).");
        macro2.setTooltip ("Step modulation lane 2 value (stored now; destination routing comes with modulation-lane wiring).");

        enabled.onClick = [this] { commitSettings(); };
        latch.onClick = [this] { commitSettings(); };
        mode.onChange = [this] { commitSettings(); };
        division.onChange = [this] { commitSettings(); };
        octaveRange.onChange = [this] { commitSettings(); };
        restartMode.onChange = [this] { commitSettings(); };
        bpm.onValueChange = [this] { commitSettings(); };
        length.onValueChange = [this]
        {
            commitSettings();
            page = juce::jlimit (0, pageCount() - 1, page);
            selectedStep = juce::jmin (selectedStep, settings.length - 1);
            refreshStepEditor(); refreshStepButtons();
        };
        swing.onValueChange = [this] { commitSettings(); };
        previousPage.onClick = [this] { page = juce::jmax (0, page - 1); selectFirstVisible(); };
        nextPage.onClick = [this] { page = juce::jmin (pageCount() - 1, page + 1); selectFirstVisible(); };
        randomize.onClick = [this] { randomizePattern(); };
        reverse.onClick = [this] { reversePattern(); };
        rotateLeft.onClick = [this] { rotatePattern (-1); };
        rotateRight.onClick = [this] { rotatePattern (1); };
        clear.onClick = [this] { clearPattern(); };

        auto stepChanged = [this] { commitSelectedStep(); };
        rest.onClick = stepChanged; tie.onClick = stepChanged; glide.onClick = stepChanged;
        pitch.onValueChange = stepChanged; octave.onValueChange = stepChanged; velocity.onValueChange = stepChanged;
        gate.onValueChange = stepChanged; probability.onValueChange = stepChanged; ratchet.onValueChange = stepChanged;
        microTiming.onValueChange = stepChanged; macro1.onValueChange = stepChanged; macro2.onValueChange = stepChanged;

        loadState();
        startTimerHz (15);
    }

    ~SequencerPanel() override { stopTimer(); }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12);
        title.setBounds (area.removeFromTop (24));
        auto transport = area.removeFromTop (30);
        enabled.setBounds (transport.removeFromLeft (74).reduced (2));
        mode.setBounds (transport.removeFromLeft (142).reduced (2));
        division.setBounds (transport.removeFromLeft (76).reduced (2));
        bpm.setBounds (transport.removeFromLeft (150).reduced (2));
        length.setBounds (transport.removeFromLeft (142).reduced (2));
        swing.setBounds (transport.removeFromLeft (132).reduced (2));
        latch.setBounds (transport.reduced (2));

        auto tools = area.removeFromTop (30);
        octaveRange.setBounds (tools.removeFromLeft (82).reduced (2)); restartMode.setBounds (tools.removeFromLeft (110).reduced (2));
        previousPage.setBounds (tools.removeFromLeft (58).reduced (2)); nextPage.setBounds (tools.removeFromLeft (58).reduced (2));
        randomize.setBounds (tools.removeFromLeft (94).reduced (2)); reverse.setBounds (tools.removeFromLeft (76).reduced (2));
        rotateLeft.setBounds (tools.removeFromLeft (76).reduced (2)); rotateRight.setBounds (tools.removeFromLeft (76).reduced (2));
        clear.setBounds (tools.removeFromLeft (62).reduced (2)); status.setBounds (tools.reduced (2));

        area.removeFromTop (5);
        auto steps = area.removeFromTop (52);
        const int stepWidth = juce::jmax (30, steps.getWidth() / stepsPerPage);
        for (auto& button : stepButtons) button.setBounds (steps.removeFromLeft (stepWidth).reduced (2));

        area.removeFromTop (7);
        stepLabel.setBounds (area.removeFromTop (23));
        auto flags = area.removeFromTop (29);
        rest.setBounds (flags.removeFromLeft (74).reduced (2)); tie.setBounds (flags.removeFromLeft (74).reduced (2)); glide.setBounds (flags.removeFromLeft (74).reduced (2));
        auto first = area.removeFromTop (58);
        layoutLabeledSlider (first.removeFromLeft (first.getWidth() / 6), pitch, "PITCH");
        layoutLabeledSlider (first.removeFromLeft (first.getWidth() / 5), octave, "OCTAVE");
        layoutLabeledSlider (first.removeFromLeft (first.getWidth() / 4), velocity, "VELOCITY");
        layoutLabeledSlider (first.removeFromLeft (first.getWidth() / 3), gate, "GATE");
        layoutLabeledSlider (first.removeFromLeft (first.getWidth() / 2), probability, "PROBABILITY");
        layoutLabeledSlider (first, ratchet, "RATCHET");
        auto second = area.removeFromTop (58);
        layoutLabeledSlider (second.removeFromLeft (second.getWidth() / 3), microTiming, "MICRO TIME");
        layoutLabeledSlider (second.removeFromLeft (second.getWidth() / 2), macro1, "MACRO 1");
        layoutLabeledSlider (second, macro2, "MACRO 2");
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff0b1215));
        g.setColour (juce::Colour (0xff3f5559));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1), 8.0f, 1.0f);
    }

private:
    static constexpr int schemaVersion = 1;
    static constexpr int stepsPerPage = 16;
    RetroMatchSynthAudioProcessor& proc;
    RetroMatchSequencer::Settings settings {};
    std::array<RetroMatchSequencer::Step, RetroMatchSequencer::maxSteps> stepState {};
    int selectedStep = 0;
    int page = 0;
    int lastPlayStep = -1;
    bool updating = false;

    juce::Label title, stepLabel, status;
    juce::ToggleButton enabled, latch, rest, tie, glide;
    juce::ComboBox mode, division, octaveRange, restartMode;
    juce::Slider bpm, length, swing;
    juce::TextButton previousPage, nextPage, randomize, reverse, rotateLeft, rotateRight, clear;
    std::array<juce::TextButton, stepsPerPage> stepButtons;
    juce::Slider pitch, octave, velocity, gate, probability, ratchet, microTiming, macro1, macro2;
    std::vector<std::unique_ptr<juce::Label>> labels;
    std::vector<std::pair<juce::Slider*, juce::Label*>> sliderLabels;

    static void configureStepSlider (juce::Slider& slider, double minimum, double maximum, double interval, const juce::String& suffix)
    {
        slider.setRange (minimum, maximum, interval);
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 66, 18);
        slider.setTextValueSuffix (suffix);
    }

    void layoutLabeledSlider (juce::Rectangle<int> area, juce::Slider& slider, const juce::String& label)
    {
        auto* name = labelsForSlider (slider);
        if (name == nullptr)
        {
            labels.emplace_back (std::make_unique<juce::Label>());
            name = labels.back().get();
            name->setText (label, juce::dontSendNotification);
            name->setJustificationType (juce::Justification::centred);
            name->setFont (juce::Font (juce::FontOptions (9.0f, juce::Font::bold)));
            name->setColour (juce::Label::textColourId, juce::Colour (0xff9bb2b6));
            addAndMakeVisible (*name);
            sliderLabels.push_back ({ &slider, name });
        }
        name->setBounds (area.removeFromTop (15));
        slider.setBounds (area.reduced (2));
    }

    juce::Label* labelsForSlider (juce::Slider& slider)
    {
        for (auto& pair : sliderLabels) if (pair.first == &slider) return pair.second;
        return nullptr;
    }

    int pageCount() const { return juce::jmax (1, (settings.length + stepsPerPage - 1) / stepsPerPage); }

    void selectFirstVisible()
    {
        selectedStep = juce::jmin (settings.length - 1, page * stepsPerPage);
        refreshStepEditor(); refreshStepButtons();
    }

    void commitSettings()
    {
        if (updating) return;
        settings.enabled = enabled.getToggleState();
        settings.clockSource = RetroMatchSequencer::ClockSource::internal;
        settings.mode = (RetroMatchSequencer::Mode) juce::jlimit (0, 8, mode.getSelectedId() - 1);
        settings.division = (RetroMatchSequencer::Division) juce::jlimit (0, 9, division.getSelectedId() - 1);
        settings.internalBpm = bpm.getValue();
        settings.length = juce::jlimit (1, RetroMatchSequencer::maxSteps, (int) std::lround (length.getValue()));
        settings.swing = juce::jlimit (0.0f, 0.95f, (float) swing.getValue() * 0.01f);
        settings.latch = latch.getToggleState();
        settings.octaveRange = juce::jlimit (1, 4, octaveRange.getSelectedId());
        settings.restartMode = (RetroMatchSequencer::RestartMode) juce::jlimit (0, 2, restartMode.getSelectedId() - 1);
        if (settings.enabled) proc.melodyTransport.stop();
        proc.melodyTransport.setSequencerSettings (settings);
        saveState();
    }

    void commitSelectedStep()
    {
        if (updating || ! juce::isPositiveAndBelow (selectedStep, RetroMatchSequencer::maxSteps)) return;
        auto& step = stepState[(size_t) selectedStep];
        step.enabled = true;
        step.rest = rest.getToggleState(); step.tie = tie.getToggleState(); step.glide = glide.getToggleState();
        step.semitone = (int) std::lround (pitch.getValue()); step.octave = (int) std::lround (octave.getValue());
        step.velocity = (float) velocity.getValue() * 0.01f; step.gate = (float) gate.getValue() * 0.01f;
        step.probability = (float) probability.getValue() * 0.01f; step.ratchet = (int) std::lround (ratchet.getValue());
        step.microTiming = (float) microTiming.getValue() * 0.01f;
        step.macro[0] = (float) macro1.getValue() * 0.01f; step.macro[1] = (float) macro2.getValue() * 0.01f;
        proc.melodyTransport.setSequencerStep (selectedStep, step);
        saveState(); refreshStepButtons();
    }

    void refreshStepEditor()
    {
        updating = true;
        const auto& step = stepState[(size_t) juce::jlimit (0, RetroMatchSequencer::maxSteps - 1, selectedStep)];
        const auto pitchText = juce::String (step.semitone >= 0 ? "+" : "") + juce::String (step.semitone) + " st";
        stepLabel.setText ("STEP " + juce::String (selectedStep + 1) + "  /  " + (step.rest ? juce::String ("REST") : pitchText), juce::dontSendNotification);
        rest.setToggleState (step.rest, juce::dontSendNotification); tie.setToggleState (step.tie, juce::dontSendNotification); glide.setToggleState (step.glide, juce::dontSendNotification);
        pitch.setValue (step.semitone, juce::dontSendNotification); octave.setValue (step.octave, juce::dontSendNotification);
        velocity.setValue (step.velocity * 100.0f, juce::dontSendNotification); gate.setValue (step.gate * 100.0f, juce::dontSendNotification);
        probability.setValue (step.probability * 100.0f, juce::dontSendNotification); ratchet.setValue (step.ratchet, juce::dontSendNotification);
        microTiming.setValue (step.microTiming * 100.0f, juce::dontSendNotification);
        macro1.setValue (step.macro[0] * 100.0f, juce::dontSendNotification); macro2.setValue (step.macro[1] * 100.0f, juce::dontSendNotification);
        updating = false;
    }

    void refreshStepButtons()
    {
        const int playingStep = proc.melodyTransport.getSequencerCurrentStep();
        for (int slot = 0; slot < stepsPerPage; ++slot)
        {
            const int index = page * stepsPerPage + slot;
            auto& button = stepButtons[(size_t) slot];
            const bool visible = index < settings.length;
            button.setVisible (visible);
            if (! visible) continue;
            const auto& step = stepState[(size_t) index];
            const juce::String value = step.rest ? "--" : (juce::String (step.semitone >= 0 ? "+" : "") + juce::String (step.semitone));
            const juce::String playMarker = playingStep == index && proc.melodyTransport.isSequencerRunning() ? ">" : "";
            button.setButtonText (playMarker + juce::String (index + 1) + "\n" + value);
            button.setToggleState (index == selectedStep, juce::dontSendNotification);
        }
        previousPage.setEnabled (page > 0); nextPage.setEnabled (page + 1 < pageCount());
    }

    void randomizePattern()
    {
        const auto seed = (std::uint32_t) juce::Random::getSystemRandom().nextInt();
        proc.melodyTransport.randomizeSequencerPattern (seed);
        for (int i = 0; i < RetroMatchSequencer::maxSteps; ++i) stepState[(size_t) i] = proc.melodyTransport.getSequencerStep (i);
        saveState(); refreshStepEditor(); refreshStepButtons();
    }

    void clearPattern()
    {
        proc.melodyTransport.clearSequencerPattern();
        for (int i = 0; i < RetroMatchSequencer::maxSteps; ++i) stepState[(size_t) i] = proc.melodyTransport.getSequencerStep (i);
        saveState(); refreshStepEditor(); refreshStepButtons();
    }

    void reversePattern()
    {
        if (settings.length < 2) return;
        std::reverse (stepState.begin(), stepState.begin() + settings.length);
        pushPattern();
    }

    void rotatePattern (int direction)
    {
        if (settings.length < 2) return;
        if (direction < 0) std::rotate (stepState.begin(), stepState.begin() + 1, stepState.begin() + settings.length);
        else std::rotate (stepState.begin(), stepState.begin() + settings.length - 1, stepState.begin() + settings.length);
        pushPattern();
    }

    void pushPattern()
    {
        for (int i = 0; i < settings.length; ++i) proc.melodyTransport.setSequencerStep (i, stepState[(size_t) i]);
        saveState(); refreshStepEditor(); refreshStepButtons();
    }

    void loadState()
    {
        settings.enabled = false;
        settings.clockSource = RetroMatchSequencer::ClockSource::internal;
        settings.mode = RetroMatchSequencer::Mode::pattern;
        settings.division = RetroMatchSequencer::Division::sixteenth;
        settings.length = 16;
        settings.internalBpm = 120.0;
        settings.restartMode = RetroMatchSequencer::RestartMode::firstNote;
        settings.octaveRange = 1;
        for (auto& step : stepState) step = {};

        const auto state = proc.apvts.state.getChildWithName ("SEQUENCER");
        if (state.isValid())
        {
            settings.enabled = (bool) state.getProperty ("enabled", false);
            settings.mode = (RetroMatchSequencer::Mode) juce::jlimit (0, 8, (int) state.getProperty ("mode", 8));
            settings.division = (RetroMatchSequencer::Division) juce::jlimit (0, 9, (int) state.getProperty ("division", 4));
            settings.length = juce::jlimit (1, RetroMatchSequencer::maxSteps, (int) state.getProperty ("length", 16));
            settings.internalBpm = juce::jlimit (20.0, 400.0, (double) state.getProperty ("bpm", 120.0));
            settings.swing = juce::jlimit (0.0f, 0.95f, (float) state.getProperty ("swing", 0.0f));
            settings.latch = (bool) state.getProperty ("latch", false);
            settings.octaveRange = juce::jlimit (1, 4, (int) state.getProperty ("octaveRange", 1));
            settings.restartMode = (RetroMatchSequencer::RestartMode) juce::jlimit (0, 2, (int) state.getProperty ("restartMode", 2));
            for (const auto child : state)
            {
                if (! child.hasType ("STEP")) continue;
                const int index = juce::jlimit (0, RetroMatchSequencer::maxSteps - 1, (int) child.getProperty ("index", 0));
                auto step = stepState[(size_t) index];
                step.enabled = (bool) child.getProperty ("enabled", true); step.rest = (bool) child.getProperty ("rest", false); step.tie = (bool) child.getProperty ("tie", false);
                step.semitone = (int) child.getProperty ("semitone", 0); step.octave = (int) child.getProperty ("octave", 0);
                step.velocity = (float) child.getProperty ("velocity", 1.0f); step.gate = (float) child.getProperty ("gate", 0.85f);
                step.probability = (float) child.getProperty ("probability", 1.0f); step.ratchet = (int) child.getProperty ("ratchet", 1);
                step.microTiming = (float) child.getProperty ("microTiming", 0.0f); step.glide = (bool) child.getProperty ("glide", false);
                step.macro[0] = (float) child.getProperty ("macro1", 0.5f); step.macro[1] = (float) child.getProperty ("macro2", 0.5f);
                stepState[(size_t) index] = step;
            }
        }

        updating = true;
        enabled.setToggleState (settings.enabled, juce::dontSendNotification); latch.setToggleState (settings.latch, juce::dontSendNotification);
        mode.setSelectedId ((int) settings.mode + 1, juce::dontSendNotification); division.setSelectedId ((int) settings.division + 1, juce::dontSendNotification);
        octaveRange.setSelectedId (settings.octaveRange, juce::dontSendNotification); restartMode.setSelectedId ((int) settings.restartMode + 1, juce::dontSendNotification);
        bpm.setValue (settings.internalBpm, juce::dontSendNotification); length.setValue (settings.length, juce::dontSendNotification); swing.setValue (settings.swing * 100.0f, juce::dontSendNotification);
        updating = false;
        proc.melodyTransport.setSequencerSettings (settings);
        for (int i = 0; i < RetroMatchSequencer::maxSteps; ++i) proc.melodyTransport.setSequencerStep (i, stepState[(size_t) i]);
        refreshStepEditor(); refreshStepButtons();
    }

    void saveState()
    {
        juce::ValueTree state ("SEQUENCER");
        state.setProperty ("schema", schemaVersion, nullptr); state.setProperty ("enabled", settings.enabled, nullptr);
        state.setProperty ("mode", (int) settings.mode, nullptr); state.setProperty ("division", (int) settings.division, nullptr);
        state.setProperty ("length", settings.length, nullptr); state.setProperty ("bpm", settings.internalBpm, nullptr);
        state.setProperty ("swing", settings.swing, nullptr); state.setProperty ("latch", settings.latch, nullptr);
        state.setProperty ("octaveRange", settings.octaveRange, nullptr); state.setProperty ("restartMode", (int) settings.restartMode, nullptr);
        for (int i = 0; i < RetroMatchSequencer::maxSteps; ++i)
        {
            const auto& step = stepState[(size_t) i];
            juce::ValueTree child ("STEP");
            child.setProperty ("index", i, nullptr); child.setProperty ("enabled", step.enabled, nullptr); child.setProperty ("rest", step.rest, nullptr); child.setProperty ("tie", step.tie, nullptr);
            child.setProperty ("semitone", step.semitone, nullptr); child.setProperty ("octave", step.octave, nullptr); child.setProperty ("velocity", step.velocity, nullptr);
            child.setProperty ("gate", step.gate, nullptr); child.setProperty ("probability", step.probability, nullptr); child.setProperty ("ratchet", step.ratchet, nullptr);
            child.setProperty ("microTiming", step.microTiming, nullptr); child.setProperty ("glide", step.glide, nullptr); child.setProperty ("macro1", step.macro[0], nullptr); child.setProperty ("macro2", step.macro[1], nullptr);
            state.appendChild (child, nullptr);
        }
        auto previous = proc.apvts.state.getChildWithName ("SEQUENCER");
        if (previous.isValid()) proc.apvts.state.removeChild (previous, nullptr);
        proc.apvts.state.appendChild (state, nullptr);
    }

    void timerCallback() override
    {
        const int current = proc.melodyTransport.getSequencerCurrentStep();
        if (current != lastPlayStep)
        {
            lastPlayStep = current;
            refreshStepButtons();
        }
        status.setText (settings.enabled ? (proc.melodyTransport.isSequencerRunning() ? "RUNNING" : "ARMED") : "OFF", juce::dontSendNotification);
    }
};