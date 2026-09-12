#pragma once

#include "../PluginProcessor.h"
#include "../Sequencer/PatternLibrary.h"
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
        setSize (820, 500);
        addAndMakeVisible (title); addAndMakeVisible (status);
        title.setText ("STEP SEQUENCER / ARPEGGIATOR", juce::dontSendNotification);
        title.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
        title.setColour (juce::Label::textColourId, findColour (RetroLookAndFeel::primaryLed));
        status.setJustificationType (juce::Justification::centredRight);
        status.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
        status.setColour (juce::Label::textColourId, findColour (RetroLookAndFeel::secondaryLed));

        for (auto* c : std::array<juce::Component*, 17> { &enabled, &mode, &outputMode, &division, &bpm, &length, &swing, &latch,
                                                           &rootNote, &octaveRange, &restartMode, &previousPage, &nextPage,
                                                           &randomize, &reverse, &clear, &patternTemplate })
            addAndMakeVisible (*c);
        addAndMakeVisible (rotateLeft); addAndMakeVisible (rotateRight);
        for (auto* c : { &macroDestination1, &macroDestination2, &macroInterpolation1, &macroInterpolation2 })
            addAndMakeVisible (*c);
        addAndMakeVisible (macroRate1); addAndMakeVisible (macroRate2);
        addAndMakeVisible (savePattern); addAndMakeVisible (loadPattern); addAndMakeVisible (zoom);
        addAndMakeVisible (fitMotion);

        enabled.setButtonText ("SEQ ON");
        latch.setButtonText ("LATCH");
        mode.addItemList ({ "UP", "DOWN", "UP / DOWN", "DOWN / UP", "PLAYED ORDER", "CHORD", "RANDOM", "WALK", "PATTERN" }, 1);
        outputMode.addItemList ({ "NOTES + MOTION", "MOTION ONLY" }, 1);
        targetScope.addItemList ({ "GLOBAL", "MAIN INSTANCE", "LAYER INSTANCE" }, 1);
        for (int i = 0; i < VoiceParameters::extraLayerCount; ++i) targetLayer.addItem ("LAYER " + juce::String (i + 1), i + 1);
        division.addItemList ({ "1/1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/8T", "1/16T", "1/8.", "1/16." }, 1);
        octaveRange.addItemList ({ "1 OCT", "2 OCT", "3 OCT", "4 OCT" }, 1);
        restartMode.addItemList ({ "FREE RUN", "TRANSPORT", "FIRST NOTE" }, 1);
        for (int i = 0; i < RetroMatchSequencer::patternTemplateCount; ++i)
            patternTemplate.addItem (RetroMatchSequencer::makePatternTemplate (i).name, i + 1);
        patternTemplate.setTooltip ("Load a reusable sequencer pattern independent of the current synth preset. Loading replaces the editable steps.");
        for (int midiNote = 0; midiNote < 128; ++midiNote)
            rootNote.addItem ("ROOT " + juce::MidiMessage::getMidiNoteName (midiNote, true, true, 3), midiNote + 1);
        bpm.setRange (20.0, 400.0, 1.0); bpm.setTextValueSuffix (" BPM");
        length.setRange (1.0, RetroMatchSequencer::maxSteps, 1.0); length.setTextValueSuffix (" steps");
        swing.setRange (0.0, 95.0, 1.0); swing.setTextValueSuffix (" %");
        for (auto* slider : { &bpm, &length, &swing })
        {
            slider->setSliderStyle (juce::Slider::LinearHorizontal);
            slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, 72, 21);
        }
        previousPage.setButtonText ("< 16"); nextPage.setButtonText ("16 >");
        savePattern.setButtonText ("SAVE PATTERN"); loadPattern.setButtonText ("LOAD PATTERN");
        fitMotion.setButtonText ("FIT FROM REF");
        fitMotion.setTooltip ("Analyze the reference's temporal RMS and spectral movement, then create a disabled motion-only lane suggestion. Review it and arm SEQ ON when ready.");
        zoom.setRange (0.65, 1.5, 0.05); zoom.setValue (1.0, juce::dontSendNotification);
        zoom.setTextValueSuffix (" x"); zoom.setTooltip ("Zoom the step editor lanes while keeping the 16-step page model.");
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

        for (auto* c : std::array<juce::Component*, 14> { &stepLabel, &rest, &tie, &glide, &pitch, &octave, &velocity,
                                                           &gate, &probability, &modulationProbability, &ratchet, &microTiming, &macro1, &macro2 })
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
        configureStepSlider (modulationProbability, 0, 100, 1, " %");
        configureStepSlider (ratchet, 1, 8, 1, " x");
        configureStepSlider (microTiming, -45, 45, 1, " %");
        configureStepSlider (macro1, 0, 100, 1, " %");
        configureStepSlider (macro2, 0, 100, 1, " %");

        mode.setTooltip ("Arp modes use held MIDI notes. PATTERN uses ROOT + per-step pitch/octave and runs without held notes.");
        outputMode.setTooltip ("NOTES + MOTION emits sequencer notes. MOTION ONLY advances the lanes and evolves the current sound without creating a second melody/MIDI player.");
        targetScope.setTooltip ("Select where the motion lanes are applied: the whole instrument, main instance, or one companion layer.");
        targetLayer.setTooltip ("Companion layer receiving motion when LAYER INSTANCE is selected.");
        rootNote.setTooltip ("Base MIDI note for PATTERN mode. Each step adds its pitch and octave offsets to this root.");
        division.setTooltip ("Step division. In DAW Tempo mode, steps follow the host play/stop state and BPM; Manual BPM runs independently.");
        swing.setTooltip ("Alternating swing while preserving each two-step pair duration.");
        latch.setTooltip ("Keep held arpeggiator notes active after key release.");
        octaveRange.setTooltip ("Arpeggiator octave span for held-note modes. Pattern mode keeps using each step's explicit pitch/octave.");
        restartMode.setTooltip ("FREE RUN keeps phase, TRANSPORT follows a transport-start reset when supplied, FIRST NOTE restarts when a new held-note phrase begins.");
        probability.setTooltip ("Independent note trigger probability for this step.");
        modulationProbability.setTooltip ("Independent probability for applying this step's two macro modulation values; note triggering is unaffected.");
        microTiming.setTooltip ("Bounded offset within the nominal step, +/-45% maximum.");
        macro1.setTooltip ("Step modulation lane 1 value.");
        macro2.setTooltip ("Step modulation lane 2 value.");
        for (auto* destination : { &macroDestination1, &macroDestination2 })
            destination->addItemList ({ "OFF", "CUTOFF", "RESONANCE", "PITCH", "AMPLITUDE", "WAVETABLE" }, 1);
        for (auto* interpolation : { &macroInterpolation1, &macroInterpolation2 })
            interpolation->addItemList ({ "HOLD", "LINEAR", "SMOOTH", "RANDOM" }, 1);
        for (auto* rate : { &macroRate1, &macroRate2 })
        {
            rate->setRange (0.25, 4.0, 0.25);
            rate->setSliderStyle (juce::Slider::LinearHorizontal);
            rate->setTextBoxStyle (juce::Slider::TextBoxRight, false, 58, 21);
            rate->setTextValueSuffix (" x");
        }

        enabled.onClick = [this] { commitSettings(); };
        latch.onClick = [this] { commitSettings(); };
        mode.onChange = [this] { commitSettings(); };
        outputMode.onChange = [this] { commitSettings(); };
        targetScope.onChange = [this] { commitSettings(); };
        targetLayer.onChange = [this] { commitSettings(); };
        division.onChange = [this] { commitSettings(); };
        rootNote.onChange = [this] { commitSettings(); };
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
        macroDestination1.onChange = [this] { commitSettings(); }; macroDestination2.onChange = [this] { commitSettings(); };
        macroInterpolation1.onChange = [this] { commitSettings(); }; macroInterpolation2.onChange = [this] { commitSettings(); };
        macroRate1.onValueChange = [this] { commitSettings(); }; macroRate2.onValueChange = [this] { commitSettings(); };
        previousPage.onClick = [this] { page = juce::jmax (0, page - 1); selectFirstVisible(); };
        nextPage.onClick = [this] { page = juce::jmin (pageCount() - 1, page + 1); selectFirstVisible(); };
        randomize.onClick = [this] { randomizePattern(); };
        reverse.onClick = [this] { reversePattern(); };
        rotateLeft.onClick = [this] { rotatePattern (-1); };
        rotateRight.onClick = [this] { rotatePattern (1); };
        clear.onClick = [this] { clearPattern(); };
        savePattern.onClick = [this] { choosePattern (true); };
        loadPattern.onClick = [this] { choosePattern (false); };
        zoom.onValueChange = [this] { refreshStepButtons(); resized(); };
        fitMotion.onClick = [this]
        {
            if (! proc.applySequencerInference())
            { status.setText ("REFERENCE TOO STATIC FOR MOTION FIT", juce::dontSendNotification); return; }
            loadState(); status.setText ("MOTION FIT READY - REVIEW LANES, THEN ARM", juce::dontSendNotification);
        };
        patternTemplate.onChange = [this] { loadPatternTemplate (patternTemplate.getSelectedId() - 1); };

        auto stepChanged = [this] { commitSelectedStep(); };
        rest.onClick = stepChanged; tie.onClick = stepChanged; glide.onClick = stepChanged;
        pitch.onValueChange = stepChanged; octave.onValueChange = stepChanged; velocity.onValueChange = stepChanged;
        gate.onValueChange = stepChanged; probability.onValueChange = stepChanged; modulationProbability.onValueChange = stepChanged; ratchet.onValueChange = stepChanged;
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
        enabled.setBounds (transport.removeFromLeft (70).reduced (2));
        mode.setBounds (transport.removeFromLeft (132).reduced (2));
        outputMode.setBounds (transport.removeFromLeft (132).reduced (2));
        targetScope.setBounds (transport.removeFromLeft (118).reduced (2));
        targetLayer.setBounds (transport.removeFromLeft (82).reduced (2));
        division.setBounds (transport.removeFromLeft (70).reduced (2));
        rootNote.setBounds (transport.removeFromLeft (96).reduced (2));
        bpm.setBounds (transport.removeFromLeft (122).reduced (2));
        length.setBounds (transport.removeFromLeft (112).reduced (2));
        swing.setBounds (transport.removeFromLeft (104).reduced (2));
        latch.setBounds (transport.reduced (2));

        auto tools = area.removeFromTop (30);
        octaveRange.setBounds (tools.removeFromLeft (82).reduced (2)); restartMode.setBounds (tools.removeFromLeft (110).reduced (2));
        previousPage.setBounds (tools.removeFromLeft (58).reduced (2)); nextPage.setBounds (tools.removeFromLeft (58).reduced (2));
        patternTemplate.setBounds (tools.removeFromLeft (112).reduced (2));
        randomize.setBounds (tools.removeFromLeft (94).reduced (2)); reverse.setBounds (tools.removeFromLeft (76).reduced (2));
        rotateLeft.setBounds (tools.removeFromLeft (76).reduced (2)); rotateRight.setBounds (tools.removeFromLeft (76).reduced (2));
        clear.setBounds (tools.removeFromLeft (62).reduced (2));
        savePattern.setBounds (tools.removeFromLeft (100).reduced (2)); loadPattern.setBounds (tools.removeFromLeft (100).reduced (2));
        zoom.setBounds (tools.removeFromLeft (82).reduced (2)); fitMotion.setBounds (tools.removeFromLeft (112).reduced (2)); status.setBounds (tools.reduced (2));

        area.removeFromTop (5);
        auto steps = area.removeFromTop (52);
        const int stepWidth = juce::jmax (30, (int) std::lround (steps.getWidth() / (stepsPerPage * zoom.getValue())));
        stepGridBounds = steps.reduced (2);
        for (auto& button : stepButtons) button.setBounds (0, 0, 0, 0);

        area.removeFromTop (7);
        stepLabel.setBounds (area.removeFromTop (23));
        auto flags = area.removeFromTop (29);
        rest.setBounds (flags.removeFromLeft (74).reduced (2)); tie.setBounds (flags.removeFromLeft (74).reduced (2)); glide.setBounds (flags.removeFromLeft (74).reduced (2));
        auto first = area.removeFromTop (58);
        layoutLabeledSlider (first.removeFromLeft (first.getWidth() / 6), pitch, "PITCH");
        layoutLabeledSlider (first.removeFromLeft (first.getWidth() / 5), octave, "OCTAVE");
        layoutLabeledSlider (first.removeFromLeft (first.getWidth() / 4), velocity, "VELOCITY");
        layoutLabeledSlider (first.removeFromLeft (first.getWidth() / 3), gate, "GATE");
        layoutLabeledSlider (first.removeFromLeft (first.getWidth() / 3), probability, "NOTE PROB");
        layoutLabeledSlider (first.removeFromLeft (first.getWidth() / 2), modulationProbability, "MOD PROB");
        layoutLabeledSlider (first, ratchet, "RATCHET");
        auto second = area.removeFromTop (58);
        layoutLabeledSlider (second.removeFromLeft (second.getWidth() / 3), microTiming, "MICRO TIME");
        layoutLabeledSlider (second.removeFromLeft (second.getWidth() / 2), macro1, "MACRO 1");
        layoutLabeledSlider (second, macro2, "MACRO 2");
        area.removeFromTop (4);
        auto macros = area.removeFromTop (30);
        macroDestination1.setBounds (macros.removeFromLeft (130).reduced (2));
        macroInterpolation1.setBounds (macros.removeFromLeft (120).reduced (2));
        macroRate1.setBounds (macros.removeFromLeft (120).reduced (2));
        macroDestination2.setBounds (macros.removeFromLeft (130).reduced (2));
        macroInterpolation2.setBounds (macros.removeFromLeft (120).reduced (2));
        macroRate2.setBounds (macros.reduced (2));
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff0b1215));
        g.setColour (juce::Colour (0xff3f5559));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1), 8.0f, 1.0f);
        paintStepGrid (g);
    }

    void mouseDown (const juce::MouseEvent& e) override { editStepAt (e.position); }
    void mouseDrag (const juce::MouseEvent& e) override { editStepAt (e.position); }

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
    juce::ValueTree observedState;

    juce::Label title, stepLabel, status;
    juce::ToggleButton enabled, latch, rest, tie, glide;
    juce::ComboBox mode, outputMode, targetScope, targetLayer, division, rootNote, octaveRange, restartMode;
    juce::ComboBox macroDestination1, macroDestination2, macroInterpolation1, macroInterpolation2;
    juce::Slider bpm, length, swing;
    juce::Slider macroRate1, macroRate2;
    juce::TextButton previousPage, nextPage, randomize, reverse, rotateLeft, rotateRight, clear;
    juce::TextButton savePattern, loadPattern;
    juce::TextButton fitMotion;
    juce::Slider zoom;
    std::unique_ptr<juce::FileChooser> patternChooser;
    juce::ComboBox patternTemplate;
    std::array<juce::TextButton, stepsPerPage> stepButtons;
    juce::Rectangle<int> stepGridBounds;
    juce::Slider pitch, octave, velocity, gate, probability, modulationProbability, ratchet, microTiming, macro1, macro2;
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

    void refreshModeControls()
    {
        const bool patternMode = settings.mode == RetroMatchSequencer::Mode::pattern;
        rootNote.setEnabled (patternMode);
        octaveRange.setEnabled (! patternMode);
    }

    void commitSettings()
    {
        if (updating) return;
        settings.enabled = enabled.getToggleState();
        settings.clockSource = RetroMatchSequencer::ClockSource::internal;
        settings.mode = (RetroMatchSequencer::Mode) juce::jlimit (0, 8, mode.getSelectedId() - 1);
        settings.outputMode = (RetroMatchSequencer::OutputMode) juce::jlimit (0, 1, outputMode.getSelectedId() - 1);
        settings.targetScope = (RetroMatchSequencer::TargetScope) juce::jlimit (0, 2, targetScope.getSelectedId() - 1);
        settings.targetLayer = juce::jlimit (0, VoiceParameters::extraLayerCount - 1, targetLayer.getSelectedId() - 1);
        settings.division = (RetroMatchSequencer::Division) juce::jlimit (0, 9, division.getSelectedId() - 1);
        settings.internalBpm = bpm.getValue();
        settings.length = juce::jlimit (1, RetroMatchSequencer::maxSteps, (int) std::lround (length.getValue()));
        settings.swing = juce::jlimit (0.0f, 0.95f, (float) swing.getValue() * 0.01f);
        settings.latch = latch.getToggleState();
        settings.rootNote = juce::jlimit (0, 127, rootNote.getSelectedId() - 1);
        settings.octaveRange = juce::jlimit (1, 4, octaveRange.getSelectedId());
        settings.restartMode = (RetroMatchSequencer::RestartMode) juce::jlimit (0, 2, restartMode.getSelectedId() - 1);
        settings.macroDestination = {{ (RetroMatchSequencer::MacroDestination) juce::jlimit (0, 5, macroDestination1.getSelectedId() - 1),
                                       (RetroMatchSequencer::MacroDestination) juce::jlimit (0, 5, macroDestination2.getSelectedId() - 1) }};
        settings.macroInterpolation = {{ (RetroMatchSequencer::MacroInterpolation) juce::jlimit (0, 3, macroInterpolation1.getSelectedId() - 1),
                                          (RetroMatchSequencer::MacroInterpolation) juce::jlimit (0, 3, macroInterpolation2.getSelectedId() - 1) }};
        settings.macroLaneRate = {{ (float) macroRate1.getValue(), (float) macroRate2.getValue() }};
        refreshModeControls();
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
        step.probability = (float) probability.getValue() * 0.01f; step.modulationProbability = (float) modulationProbability.getValue() * 0.01f; step.ratchet = (int) std::lround (ratchet.getValue());
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
        probability.setValue (step.probability * 100.0f, juce::dontSendNotification); modulationProbability.setValue (step.modulationProbability * 100.0f, juce::dontSendNotification); ratchet.setValue (step.ratchet, juce::dontSendNotification);
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

    void paintStepGrid (juce::Graphics& g)
    {
        if (stepGridBounds.isEmpty()) return;
        const auto area = stepGridBounds.toFloat();
        const auto led = findColour (RetroLookAndFeel::primaryLed);
        g.setColour (juce::Colour (0xff071014)); g.fillRoundedRectangle (area, 6.0f);
        g.setColour (juce::Colour (0xff52686c)); g.drawRoundedRectangle (area, 6.0f, 1.0f);
        const float cellW = area.getWidth() / (float) stepsPerPage;
        const float laneH = area.getHeight() / 4.0f;
        const int first = page * stepsPerPage;
        for (int lane = 0; lane < 4; ++lane)
        {
            const float y = area.getY() + lane * laneH;
            g.setColour (led.withAlpha (0.10f)); g.drawHorizontalLine ((int) y, area.getX(), area.getRight());
            g.setColour (led.withAlpha (0.55f));
            g.setFont (juce::Font (juce::FontOptions (9.0f, juce::Font::bold)));
            g.drawText (lane == 0 ? "PITCH" : lane == 1 ? "VELOCITY / GATE" : lane == 2 ? "MACRO 1" : "MACRO 2",
                        (int) area.getX() + 5, (int) y + 2, 115, 14, juce::Justification::left);
        }
        for (int i = 0; i < stepsPerPage; ++i)
        {
            const int index = first + i; if (index >= settings.length) break;
            const auto& step = stepState[(size_t) index];
            const float x = area.getX() + i * cellW;
            g.setColour (index == selectedStep ? findColour (RetroLookAndFeel::secondaryLed).withAlpha (0.18f) : led.withAlpha (0.04f));
            g.fillRect (x + 1.0f, area.getY() + 1.0f, cellW - 2.0f, area.getHeight() - 2.0f);
            g.setColour (led.withAlpha (0.22f)); g.drawVerticalLine ((int) x, area.getY(), area.getBottom());
            const float pitchY = area.getY() + laneH * 0.5f - juce::jlimit (-24.0f, 24.0f, (float) step.semitone + step.octave * 12.0f) / 48.0f * laneH * 0.38f;
            g.setColour (step.rest ? led.withAlpha (0.18f) : led); g.fillEllipse (x + cellW * 0.5f - 4.0f, pitchY - 4.0f, 8.0f, 8.0f);
            g.setColour (led.withAlpha (0.65f)); g.fillRect (x + cellW * 0.28f, area.getY() + laneH * 2.0f - step.macro[0] * laneH * 0.8f, cellW * 0.44f, step.macro[0] * laneH * 0.8f);
            g.setColour (findColour (RetroLookAndFeel::secondaryLed).withAlpha (0.75f)); g.fillRect (x + cellW * 0.28f, area.getY() + laneH * 3.0f - step.macro[1] * laneH * 0.8f, cellW * 0.44f, step.macro[1] * laneH * 0.8f);
            g.setColour (led.withAlpha (0.65f)); g.fillRect (x + cellW * 0.72f, area.getY() + laneH - step.velocity * laneH * 0.75f, cellW * 0.16f, step.velocity * laneH * 0.75f);
            g.setColour (led.withAlpha (0.75f)); g.setFont (juce::Font (juce::FontOptions (9.0f))); g.drawText (juce::String (index + 1), (int) x + 2, (int) area.getBottom() - 14, (int) cellW - 4, 12, juce::Justification::centred);
        }
        const int play = proc.melodyTransport.getSequencerCurrentStep();
        if (play >= first && play < first + stepsPerPage)
        { g.setColour (juce::Colours::white.withAlpha (0.8f)); g.drawRect (area.withX (area.getX() + (play - first) * cellW).withWidth (cellW), 1.5f); }
    }

    void editStepAt (juce::Point<float> position)
    {
        if (! stepGridBounds.contains (position.toInt())) return;
        const float cellW = stepGridBounds.getWidth() / (float) stepsPerPage;
        const int index = page * stepsPerPage + juce::jlimit (0, stepsPerPage - 1, (int) ((position.x - stepGridBounds.getX()) / cellW));
        if (! juce::isPositiveAndBelow (index, settings.length)) return;
        selectedStep = index;
        auto& step = stepState[(size_t) index];
        const float lane = (position.y - stepGridBounds.getY()) / (float) stepGridBounds.getHeight();
        if (lane < 0.25f) step.semitone = juce::jlimit (-48, 48, (int) std::lround ((0.125f - lane) * 192.0f));
        else if (lane < 0.5f) step.velocity = juce::jlimit (0.0f, 1.0f, 1.0f - (lane - 0.25f) * 4.0f);
        else if (lane < 0.75f) step.macro[0] = juce::jlimit (0.0f, 1.0f, 1.0f - (lane - 0.5f) * 4.0f);
        else step.macro[1] = juce::jlimit (0.0f, 1.0f, 1.0f - (lane - 0.75f) * 4.0f);
        proc.melodyTransport.setSequencerStep (index, step); refreshStepEditor(); repaint();
    }

    void loadPatternTemplate (int index)
    {
        if (index < 0 || index >= RetroMatchSequencer::patternTemplateCount) return;
        const auto pattern = RetroMatchSequencer::makePatternTemplate (index);
        settings.length = pattern.length;
        stepState = pattern.steps;
        settings.mode = RetroMatchSequencer::Mode::pattern;
        settings.outputMode = RetroMatchSequencer::OutputMode::notesAndMotion;
        settings.targetScope = RetroMatchSequencer::TargetScope::global;
        settings.targetLayer = 0;
        pushPattern();
        updating = true;
        length.setValue (settings.length, juce::dontSendNotification);
        mode.setSelectedId ((int) settings.mode + 1, juce::dontSendNotification);
        updating = false;
        commitSettings();
        page = 0; selectedStep = 0;
        refreshModeControls(); refreshStepEditor(); refreshStepButtons();
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
        settings.rootNote = 60;
        for (auto& step : stepState) step = {};

        const auto state = proc.apvts.state.getChildWithName ("SEQUENCER");
        if (state.isValid())
        {
            settings.enabled = (bool) state.getProperty ("enabled", false);
            settings.mode = (RetroMatchSequencer::Mode) juce::jlimit (0, 8, (int) state.getProperty ("mode", 8));
            settings.outputMode = (RetroMatchSequencer::OutputMode) juce::jlimit (0, 1, (int) state.getProperty ("outputMode", 0));
            settings.targetScope = (RetroMatchSequencer::TargetScope) juce::jlimit (0, 2, (int) state.getProperty ("targetScope", 0));
            settings.targetLayer = juce::jlimit (0, VoiceParameters::extraLayerCount - 1, (int) state.getProperty ("targetLayer", 0));
            settings.division = (RetroMatchSequencer::Division) juce::jlimit (0, 9, (int) state.getProperty ("division", 4));
            settings.length = juce::jlimit (1, RetroMatchSequencer::maxSteps, (int) state.getProperty ("length", 16));
            settings.internalBpm = juce::jlimit (20.0, 400.0, (double) state.getProperty ("bpm", 120.0));
            settings.swing = juce::jlimit (0.0f, 0.95f, (float) state.getProperty ("swing", 0.0f));
            settings.latch = (bool) state.getProperty ("latch", false);
            settings.rootNote = juce::jlimit (0, 127, (int) state.getProperty ("rootNote", 60));
            settings.octaveRange = juce::jlimit (1, 4, (int) state.getProperty ("octaveRange", 1));
            settings.restartMode = (RetroMatchSequencer::RestartMode) juce::jlimit (0, 2, (int) state.getProperty ("restartMode", 2));
            settings.macroDestination = {{ (RetroMatchSequencer::MacroDestination) juce::jlimit (0, 5, (int) state.getProperty ("macroDestination1", 0)),
                                           (RetroMatchSequencer::MacroDestination) juce::jlimit (0, 5, (int) state.getProperty ("macroDestination2", 0)) }};
            settings.macroInterpolation = {{ (RetroMatchSequencer::MacroInterpolation) juce::jlimit (0, 3, (int) state.getProperty ("macroInterpolation1", 0)),
                                              (RetroMatchSequencer::MacroInterpolation) juce::jlimit (0, 3, (int) state.getProperty ("macroInterpolation2", 0)) }};
            settings.macroLaneRate = {{ juce::jlimit (0.25f, 4.0f, (float) state.getProperty ("macroRate1", 1.0f)),
                                        juce::jlimit (0.25f, 4.0f, (float) state.getProperty ("macroRate2", 1.0f)) }};
            for (const auto child : state)
            {
                if (! child.hasType ("STEP")) continue;
                const int index = juce::jlimit (0, RetroMatchSequencer::maxSteps - 1, (int) child.getProperty ("index", 0));
                auto step = stepState[(size_t) index];
                step.enabled = (bool) child.getProperty ("enabled", true); step.rest = (bool) child.getProperty ("rest", false); step.tie = (bool) child.getProperty ("tie", false);
                step.semitone = (int) child.getProperty ("semitone", 0); step.octave = (int) child.getProperty ("octave", 0);
                step.velocity = (float) child.getProperty ("velocity", 1.0f); step.gate = (float) child.getProperty ("gate", 0.85f);
                step.probability = (float) child.getProperty ("probability", 1.0f); step.modulationProbability = (float) child.getProperty ("modulationProbability", 1.0f); step.ratchet = (int) child.getProperty ("ratchet", 1);
                step.microTiming = (float) child.getProperty ("microTiming", 0.0f); step.glide = (bool) child.getProperty ("glide", false);
                step.macro[0] = (float) child.getProperty ("macro1", 0.5f); step.macro[1] = (float) child.getProperty ("macro2", 0.5f);
                stepState[(size_t) index] = step;
            }
        }

        updating = true;
        enabled.setToggleState (settings.enabled, juce::dontSendNotification); latch.setToggleState (settings.latch, juce::dontSendNotification);
        mode.setSelectedId ((int) settings.mode + 1, juce::dontSendNotification); outputMode.setSelectedId ((int) settings.outputMode + 1, juce::dontSendNotification); targetScope.setSelectedId ((int) settings.targetScope + 1, juce::dontSendNotification); targetLayer.setSelectedId (settings.targetLayer + 1, juce::dontSendNotification); division.setSelectedId ((int) settings.division + 1, juce::dontSendNotification);
        targetLayer.setEnabled (settings.targetScope == RetroMatchSequencer::TargetScope::layerInstance);
        rootNote.setSelectedId (settings.rootNote + 1, juce::dontSendNotification);
        octaveRange.setSelectedId (settings.octaveRange, juce::dontSendNotification); restartMode.setSelectedId ((int) settings.restartMode + 1, juce::dontSendNotification);
        bpm.setValue (settings.internalBpm, juce::dontSendNotification); length.setValue (settings.length, juce::dontSendNotification); swing.setValue (settings.swing * 100.0f, juce::dontSendNotification);
        macroDestination1.setSelectedId ((int) settings.macroDestination[0] + 1, juce::dontSendNotification); macroDestination2.setSelectedId ((int) settings.macroDestination[1] + 1, juce::dontSendNotification);
        macroInterpolation1.setSelectedId ((int) settings.macroInterpolation[0] + 1, juce::dontSendNotification); macroInterpolation2.setSelectedId ((int) settings.macroInterpolation[1] + 1, juce::dontSendNotification);
        macroRate1.setValue (settings.macroLaneRate[0], juce::dontSendNotification); macroRate2.setValue (settings.macroLaneRate[1], juce::dontSendNotification);
        updating = false;
        refreshModeControls();
        proc.melodyTransport.setSequencerSettings (settings);
        for (int i = 0; i < RetroMatchSequencer::maxSteps; ++i) proc.melodyTransport.setSequencerStep (i, stepState[(size_t) i]);
        observedState = proc.apvts.state.getChildWithName ("SEQUENCER");
        refreshStepEditor(); refreshStepButtons();
    }

    void saveState()
    {
        juce::ValueTree state ("SEQUENCER");
        state.setProperty ("schema", schemaVersion, nullptr); state.setProperty ("enabled", settings.enabled, nullptr);
        state.setProperty ("mode", (int) settings.mode, nullptr); state.setProperty ("division", (int) settings.division, nullptr);
        state.setProperty ("outputMode", (int) settings.outputMode, nullptr);
        state.setProperty ("targetScope", (int) settings.targetScope, nullptr); state.setProperty ("targetLayer", settings.targetLayer, nullptr);
        state.setProperty ("length", settings.length, nullptr); state.setProperty ("bpm", settings.internalBpm, nullptr);
        state.setProperty ("swing", settings.swing, nullptr); state.setProperty ("latch", settings.latch, nullptr);
        state.setProperty ("rootNote", settings.rootNote, nullptr); state.setProperty ("octaveRange", settings.octaveRange, nullptr);
        state.setProperty ("restartMode", (int) settings.restartMode, nullptr);
        state.setProperty ("macroDestination1", (int) settings.macroDestination[0], nullptr); state.setProperty ("macroDestination2", (int) settings.macroDestination[1], nullptr);
        state.setProperty ("macroInterpolation1", (int) settings.macroInterpolation[0], nullptr); state.setProperty ("macroInterpolation2", (int) settings.macroInterpolation[1], nullptr);
        state.setProperty ("macroRate1", settings.macroLaneRate[0], nullptr); state.setProperty ("macroRate2", settings.macroLaneRate[1], nullptr);
        for (int i = 0; i < RetroMatchSequencer::maxSteps; ++i)
        {
            const auto& step = stepState[(size_t) i];
            juce::ValueTree child ("STEP");
            child.setProperty ("index", i, nullptr); child.setProperty ("enabled", step.enabled, nullptr); child.setProperty ("rest", step.rest, nullptr); child.setProperty ("tie", step.tie, nullptr);
            child.setProperty ("semitone", step.semitone, nullptr); child.setProperty ("octave", step.octave, nullptr); child.setProperty ("velocity", step.velocity, nullptr);
            child.setProperty ("gate", step.gate, nullptr); child.setProperty ("probability", step.probability, nullptr); child.setProperty ("modulationProbability", step.modulationProbability, nullptr); child.setProperty ("ratchet", step.ratchet, nullptr);
            child.setProperty ("microTiming", step.microTiming, nullptr); child.setProperty ("glide", step.glide, nullptr); child.setProperty ("macro1", step.macro[0], nullptr); child.setProperty ("macro2", step.macro[1], nullptr);
            state.appendChild (child, nullptr);
        }
        auto previous = proc.apvts.state.getChildWithName ("SEQUENCER");
        if (previous.isValid()) proc.apvts.state.removeChild (previous, nullptr);
        proc.apvts.state.appendChild (state, nullptr);
    }

    void choosePattern (bool saving)
    {
        const auto directory = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("RetroMatch/Patterns");
        if (saving && directory.createDirectory().failed()) return;
        patternChooser = std::make_unique<juce::FileChooser> (saving ? "Save RetroMatch pattern" : "Load RetroMatch pattern",
                                                               directory.getChildFile ("My Pattern.xml"), "*.xml");
        juce::Component::SafePointer<SequencerPanel> safe (this);
        const int flags = saving ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting
                                 : juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
        patternChooser->launchAsync (flags, [safe, saving] (const juce::FileChooser& chooser)
        {
            if (! safe || chooser.getResult() == juce::File()) return;
            const auto file = chooser.getResult().withFileExtension ("xml");
            if (saving)
            {
                auto state = safe->proc.apvts.state.getChildWithName ("SEQUENCER");
                if (state.isValid())
                    if (auto xml = state.createXml()) xml->writeTo (file);
            }
            else if (auto xml = juce::XmlDocument::parse (file))
            {
                const auto loaded = juce::ValueTree::fromXml (*xml);
                if (loaded.hasType ("SEQUENCER"))
                {
                    auto previous = safe->proc.apvts.state.getChildWithName ("SEQUENCER");
                    if (previous.isValid()) safe->proc.apvts.state.removeChild (previous, nullptr);
                    safe->proc.apvts.state.appendChild (loaded, nullptr);
                    safe->loadState();
                }
            }
        });
    }

    void timerCallback() override
    {
        const auto currentState = proc.apvts.state.getChildWithName ("SEQUENCER");
        if (currentState != observedState)
        {
            loadState();
            return;
        }
        const int current = proc.melodyTransport.getSequencerCurrentStep();
        if (current != lastPlayStep)
        {
            lastPlayStep = current;
            refreshStepButtons();
        }
        status.setText (settings.enabled ? (proc.melodyTransport.isSequencerRunning() ? "RUNNING" : "ARMED") : "OFF", juce::dontSendNotification);
    }
};
