#include "RetroMatchEditorV3.h"
#include <algorithm>

namespace
{
const juce::StringArray synthOscKnobs {
    "osc1Mix", "osc2Mix", "subMix", "noise", "ringMix", "additiveMix",
    "masterTune", "osc2Semi", "osc2Detune", "pulseWidth"
};
const juce::StringArray synthTextureKnobs {
    "wavetableMix", "referenceWavetableMix", "wavetablePosition", "wavetableWarp",
    "supersawMix", "unisonDetune", "unisonSpread", "wavefold", "harmonicTilt", "oddEven"
};
const juce::StringArray fmCoreKnobs { "fmAmount", "fmRatio", "fmMix", "fmFeedback" };
const juce::StringArray fmOperatorKnobs {
    "fmOp1Ratio", "fmOp1Level", "fmOp2Ratio", "fmOp2Level", "fmOp3Ratio", "fmOp3Level",
    "fmOp4Ratio", "fmOp4Level", "fmOp5Ratio", "fmOp5Level", "fmOp6Ratio", "fmOp6Level"
};
const juce::StringArray filterKnobs { "cutoff", "resonance", "drive" };
const juce::StringArray ampKnobs { "attack", "decay", "sustain", "release", "outputGain" };
const juce::StringArray modLfoKnobs { "lfoRate", "lfoPitch", "lfoCutoff", "lfoAmp" };
const juce::StringArray chorusKnobs { "chorusMix", "chorusRate", "chorusDepth" };
const juce::StringArray delayKnobs { "delayMix", "delayTime", "delayFeedback" };
const juce::StringArray reverbKnobs { "reverbMix", "reverbSize", "reverbDamping", "stereoWidth" };

juce::Colour panelColour() { return juce::Colour (0xff0b1113); }
juce::Colour borderColour() { return juce::Colour (0xff2d3b3e); }
juce::Colour goldColour() { return juce::Colour (0xffd1ad5d); }
juce::Colour tealColour() { return juce::Colour (0xff65d5bc); }
juce::Colour cyanColour() { return juce::Colour (0xff59bcd6); }

void styleTextLabel (juce::Label& label, const juce::String& text)
{
    label.setText (text, juce::dontSendNotification);
    label.setColour (juce::Label::textColourId, juce::Colour (0xffaebdb8));
    label.setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)));
    label.setJustificationType (juce::Justification::centredLeft);
}

juce::String scoreText (float score)
{
    return score > 0.0f ? juce::String (score * 100.0f, 1) + "%" : "--";
}
}

//==============================================================================
RetroMatchSynthAudioProcessorEditor::CandidateButton::CandidateButton (const CandidateButton&) = delete;

void RetroMatchSynthAudioProcessorEditor::CandidateButton::setResult (const MatchResult* newResult, bool isSelected)
{
    selected = isSelected;
    hasResult = newResult != nullptr && newResult->confidence > 0.0f;
    if (newResult != nullptr) result = *newResult;
    repaint();
}

void RetroMatchSynthAudioProcessorEditor::CandidateButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    auto background = selected ? juce::Colour (0xff182b28) : juce::Colour (0xff11181a);
    if (highlighted) background = background.brighter (0.06f);
    if (down) background = background.darker (0.12f);

    g.setColour (juce::Colour (0x85000000));
    g.fillRoundedRectangle (r.translated (0.0f, 2.0f), 7.0f);
    g.setColour (background);
    g.fillRoundedRectangle (r, 7.0f);
    g.setColour (selected ? tealColour() : borderColour());
    g.drawRoundedRectangle (r, 7.0f, selected ? 1.7f : 1.0f);

    const auto led = juce::Rectangle<float> (r.getX() + 10.0f, r.getY() + 11.0f, 8.0f, 8.0f);
    g.setColour (selected ? tealColour().withAlpha (0.22f) : juce::Colour (0xff1c2928));
    g.fillEllipse (led.expanded (4.0f));
    g.setColour (selected ? tealColour() : juce::Colour (0xff43514f));
    g.fillEllipse (led);

    g.setColour (goldColour());
    g.setFont (juce::Font (juce::FontOptions (18.0f, juce::Font::bold)));
    g.drawText (code, 27, 4, 28, 26, juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xffb9c6c1));
    g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
    g.drawText (family, 56, 6, getWidth() - 135, 18, juce::Justification::centredLeft, true);

    g.setColour (hasResult ? juce::Colour (0xffffdf8a) : juce::Colour (0xff60706c));
    g.setFont (juce::Font (juce::FontOptions (18.0f, juce::Font::bold)));
    g.drawText (hasResult ? scoreText (result.similarity.total) : "--", getWidth() - 82, 5, 70, 24, juce::Justification::centredRight);

    if (! hasResult)
    {
        g.setColour (juce::Colour (0xff657672));
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        g.drawText ("Run Quick, Refine or AI", 28, 29, getWidth() - 40, getHeight() - 34, juce::Justification::centredLeft);
        return;
    }

    const std::array<std::pair<const char*, float>, 4> metrics {{
        { "SPEC", result.similarity.spectrum },
        { "HARM", result.similarity.harmonic },
        { "ENV", result.similarity.envelope },
        { "PITCH", result.similarity.pitch }
    }};

    auto metricArea = juce::Rectangle<int> (28, 31, getWidth() - 40, getHeight() - 37);
    const int rowH = juce::jmax (9, metricArea.getHeight() / 4);
    for (int i = 0; i < 4; ++i)
    {
        auto row = metricArea.removeFromTop (rowH);
        g.setColour (juce::Colour (0xff7d8c88));
        g.setFont (juce::Font (juce::FontOptions (8.0f, juce::Font::bold)));
        g.drawText (metrics[(size_t) i].first, row.removeFromLeft (38), juce::Justification::centredLeft);
        auto bar = row.reduced (2, juce::jmax (2, rowH / 4)).toFloat();
        g.setColour (juce::Colour (0xff1d292b));
        g.fillRoundedRectangle (bar, 2.0f);
        g.setColour (i == 1 ? goldColour() : cyanColour());
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, metrics[(size_t) i].second)), 2.0f);
    }
}

//==============================================================================
RetroMatchSynthAudioProcessorEditor::VariantThread::VariantThread (RetroMatchSynthAudioProcessorEditor& ownerIn, WorkMode modeIn)
    : juce::Thread (modeIn == WorkMode::ai ? "RetroMatch AI variants" : "RetroMatch local variants"), owner (ownerIn), mode (modeIn)
{
}

void RetroMatchSynthAudioProcessorEditor::VariantThread::run()
{
    owner.runVariantSearch (mode, *this);
}

//==============================================================================
RetroMatchSynthAudioProcessorEditor::RetroMatchSynthAudioProcessorEditor (RetroMatchSynthAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p), aiSettings (AISettings::load())
{
    setLookAndFeel (&laf);

    title.setText ("RETRO MATCH", juce::dontSendNotification);
    title.setColour (juce::Label::textColourId, juce::Colour (0xffffd77a));
    title.setFont (juce::Font (juce::FontOptions (24.0f, juce::Font::bold)));
    addAndMakeVisible (title);

    subtitle.setText ("REFERENCE-TO-SYNTH LAB  //  LOAD  >  MATCH  >  CHOOSE  >  EDIT  >  AUDITION", juce::dontSendNotification);
    subtitle.setColour (juce::Label::textColourId, juce::Colour (0xff8fa49d));
    subtitle.setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)));
    addAndMakeVisible (subtitle);

    savePatch.onClick = [this] { chooseSavePatch(); };
    loadPatch.onClick = [this] { chooseLoadPatch(); };
    exportPreview.onClick = [this] { chooseExportPreview(); };
    keyboardToggle.setClickingTogglesState (true);
    keyboardToggle.setToggleState (true, juce::dontSendNotification);
    keyboardToggle.onClick = [this]
    {
        keyboardVisible = keyboardToggle.getToggleState();
        keyboard.setVisible (keyboardVisible);
        if (! keyboardVisible)
        {
            keyboardState.allNotesOff (0);
            proc.allEditorNotesOff();
        }
        resized();
        repaint();
    };
    for (auto* button : { &savePatch, &loadPatch, &exportPreview, &keyboardToggle }) addAndMakeVisible (*button);

    load.onClick = [this] { chooseFile(); };
    quick.onClick = [this] { startVariantSearch (WorkMode::quick); };
    refine.onClick = [this] { startVariantSearch (WorkMode::refine); };
    aiVariants.onClick = [this] { startVariantSearch (WorkMode::ai); };
    for (auto* button : { &load, &quick, &refine, &aiVariants }) addAndMakeVisible (*button);

    candidateA.onClick = [this] { selectCandidate (0); };
    candidateB.onClick = [this] { selectCandidate (1); };
    candidateC.onClick = [this] { selectCandidate (2); };
    for (auto* button : { &candidateA, &candidateB, &candidateC }) addAndMakeVisible (*button);

    candidateMorphLabel.setText ("A  <  MORPH  >  C", juce::dontSendNotification);
    candidateMorphLabel.setColour (juce::Label::textColourId, goldColour());
    candidateMorphLabel.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
    candidateMorphLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (candidateMorphLabel);

    candidateMorph.setSliderStyle (juce::Slider::LinearHorizontal);
    candidateMorph.setRange (0.0, 1.0, 0.001);
    candidateMorph.setValue (0.0, juce::dontSendNotification);
    candidateMorph.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 20);
    candidateMorph.setNumDecimalPlacesToDisplay (2);
    candidateMorph.onValueChange = [this]
    {
        if (proc.candidateBank[0].confidence <= 0.0f || proc.candidateBank[1].confidence <= 0.0f || proc.candidateBank[2].confidence <= 0.0f) return;
        const float v = (float) candidateMorph.getValue();
        if (v <= 0.5f) proc.morphCandidates (0, 1, v * 2.0f);
        else proc.morphCandidates (1, 2, (v - 0.5f) * 2.0f);
        repaint (analyzerBounds);
    };
    addAndMakeVisible (candidateMorph);

    status.setText ("Load or drop a reference. Quick and Refine both create three selectable variants.", juce::dontSendNotification);
    status.setColour (juce::Label::textColourId, juce::Colour (0xffa9bbb5));
    status.setFont (juce::Font (juce::FontOptions (10.0f)));
    status.setJustificationType (juce::Justification::centredLeft);
    status.setMinimumHorizontalScale (0.65f);
    addAndMakeVisible (status);

    progressBar.setPercentageDisplay (true);
    progressBar.setVisible (false);
    addAndMakeVisible (progressBar);

    // Core selectors.
    styleTextLabel (osc1Label, "OSC 1 WAVE");
    styleTextLabel (osc2Label, "OSC 2 WAVE");
    styleTextLabel (filterLabel, "FILTER MODE");
    styleTextLabel (fmAlgorithmLabel, "6-OP ALGORITHM");
    osc1Choice.addItemList ({ "Sine", "Saw", "Square", "Triangle", "Pulse" }, 1);
    osc2Choice.addItemList ({ "Sine", "Saw", "Square", "Triangle", "Pulse" }, 1);
    filterChoice.addItemList ({ "Low-pass", "High-pass", "Band-pass" }, 1);
    fmAlgorithmChoice.addItemList ({ "Stack", "Dual Stack", "Triple Pair", "Star", "Branch", "Six Carriers" }, 1);
    osc1Attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "osc1Wave", osc1Choice);
    osc2Attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "osc2Wave", osc2Choice);
    filterAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "filterType", filterChoice);
    fmAlgorithmAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "fmAlgorithm", fmAlgorithmChoice);

    styleTextLabel (fmOperatorEditLabel, "EDIT OPERATOR");
    styleTextLabel (fmModeLabel, "FREQUENCY MODE");
    fmOperatorEditChoice.addItemList ({ "OP 1", "OP 2", "OP 3", "OP 4", "OP 5", "OP 6" }, 1);
    fmOperatorEditChoice.setSelectedId (1, juce::dontSendNotification);
    fmOperatorEditChoice.onChange = [this] { rebindFmOperatorEditor(); };
    fmModeChoice.addItemList ({ "Ratio", "Fixed" }, 1);

    const juce::String fmDetailNames[] { "FIXED HZ", "ATTACK", "DECAY", "SUSTAIN", "RELEASE", "KEY SCALE", "VELOCITY" };
    const juce::String fmDetailSuffix[] { " Hz", " s", " s", "", " s", "", "" };
    for (size_t i = 0; i < fmDetailSliders.size(); ++i)
    {
        auto& label = fmDetailLabels[i];
        label.setText (fmDetailNames[i], juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setColour (juce::Label::textColourId, juce::Colour (0xffaeb9b6));
        label.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));

        auto& slider = fmDetailSliders[i];
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 19);
        slider.setTextValueSuffix (fmDetailSuffix[i]);
        slider.setMouseDragSensitivity (180);
        slider.setNumDecimalPlacesToDisplay (i == 0 ? 1 : (i == 3 || i > 4 ? 2 : 3));
    }

    const std::tuple<const char*, const char*, const char*> knobDefs[] = {
        { "osc1Mix", "OSC 1", "" }, { "osc2Mix", "OSC 2", "" }, { "subMix", "SUB", "" }, { "noise", "NOISE", "" },
        { "ringMix", "RING", "" }, { "additiveMix", "ADDITIVE", "" }, { "wavetableMix", "WAVETABLE", "" }, { "referenceWavetableMix", "REF WT", "" }, { "wavetablePosition", "WT POSITION", "" }, { "wavetableWarp", "WT WARP", "" },
        { "supersawMix", "SUPERSAW", "" }, { "unisonDetune", "UNI DETUNE", " ct" }, { "unisonSpread", "UNI SPREAD", "" }, { "wavefold", "WAVEFOLD", "" },
        { "masterTune", "TUNE", " ct" }, { "osc2Semi", "OSC2 SEMI", " st" }, { "osc2Detune", "OSC2 FINE", " ct" }, { "pulseWidth", "PULSE", "" },
        { "fmAmount", "PM AMOUNT", "" }, { "fmRatio", "PM RATIO", "" }, { "fmMix", "6-OP FM", "" }, { "fmFeedback", "FM FEEDBACK", "" }, { "harmonicTilt", "HARM TILT", "" }, { "oddEven", "ODD/EVEN", "" }, { "cutoff", "CUTOFF", " Hz" }, { "resonance", "RESO", "" },
        { "attack", "ATTACK", " s" }, { "decay", "DECAY", " s" }, { "sustain", "SUSTAIN", "" }, { "release", "RELEASE", " s" },
        { "lfoRate", "LFO RATE", " Hz" }, { "lfoPitch", "LFO->PITCH", " st" }, { "lfoCutoff", "LFO->FILTER", "" }, { "lfoAmp", "LFO->AMP", "" },
        { "drive", "DRIVE", "" }, { "chorusMix", "CHORUS", "" }, { "chorusRate", "CHR RATE", " Hz" }, { "chorusDepth", "CHR DEPTH", "" },
        { "delayMix", "DELAY", "" }, { "delayTime", "DLY TIME", " s" }, { "delayFeedback", "DLY FDBK", "" },
        { "reverbMix", "REVERB", "" }, { "reverbSize", "ROOM", "" }, { "reverbDamping", "DAMPING", "" }, { "stereoWidth", "WIDTH", "" }, { "outputGain", "OUTPUT", " dB" }
    };
    for (const auto& [id, name, suffix] : knobDefs) addKnob (id, name, suffix);
    for (int i = 0; i < VoiceParameters::fmOperatorCount; ++i)
    {
        const auto n = juce::String (i + 1);
        addKnob ("fmOp" + n + "Ratio", "OP" + n + " RATIO", " x");
        addKnob ("fmOp" + n + "Level", "OP" + n + " LEVEL", "");
    }

    const juce::StringArray modSources { "Off", "LFO 1", "Velocity", "Key Track", "Random Note", "Amp Env" };
    const juce::StringArray modDestinations { "Off", "Pitch", "Cutoff", "Amplitude", "Pulse Width", "PM Amount", "6-OP FM Mix", "Wavetable Position", "Wavefold" };
    for (int i = 0; i < VoiceParameters::modSlotCount; ++i)
    {
        const auto index = juce::String (i + 1);
        modSlotLabels[(size_t) i].setText ("MOD " + index, juce::dontSendNotification);
        modSlotLabels[(size_t) i].setColour (juce::Label::textColourId, goldColour());
        modSlotLabels[(size_t) i].setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
        modSourceChoices[(size_t) i].addItemList (modSources, 1);
        modDestinationChoices[(size_t) i].addItemList (modDestinations, 1);
        auto& amount = modAmountSliders[(size_t) i];
        amount.setSliderStyle (juce::Slider::LinearHorizontal);
        amount.setTextBoxStyle (juce::Slider::TextBoxRight, false, 58, 20);
        amount.setNumDecimalPlacesToDisplay (2);
        modSourceAttachments[(size_t) i] = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "mod" + index + "Source", modSourceChoices[(size_t) i]);
        modDestinationAttachments[(size_t) i] = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "mod" + index + "Dest", modDestinationChoices[(size_t) i]);
        modAmountAttachments[(size_t) i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "mod" + index + "Amount", amount);
    }

    rebindFmOperatorEditor();

    // AI configuration.
    aiProvider.addItemList ({ "Disabled", "OpenAI", "Google Gemini", "OpenAI-compatible / Azure", "GitHub Copilot bridge" }, 1);
    aiProvider.onChange = [this]
    {
        const auto chosen = static_cast<AIProvider> (juce::jlimit (0, 4, aiProvider.getSelectedId() - 1));
        if (chosen != aiSettings.provider)
        {
            const auto sessionKey = aiSettings.sessionApiKey;
            const bool enabled = aiEnabled.getToggleState();
            aiSettings.applyProviderDefaults (chosen);
            aiSettings.sessionApiKey = sessionKey;
            aiSettings.enabled = enabled && chosen != AIProvider::disabled;
            updateAIControlsFromSettings();
        }
        updateAIStatus();
    };
    aiEnabled.onClick = [this] { syncAISettingsFromControls(); updateAIStatus(); };
    aiSaveSettings.onClick = [this]
    {
        syncAISettingsFromControls();
        aiSettings.save();
        status.setText ("AI settings saved. API keys are not persisted by RetroMatch.", juce::dontSendNotification);
        updateAIStatus();
    };
    aiSessionKey.setPasswordCharacter ((juce_wchar) 0x2022);
    aiSessionKey.setTextToShowWhenEmpty ("optional session key - not saved", juce::Colour (0xff62736e));
    aiModel.setTextToShowWhenEmpty ("model id", juce::Colour (0xff62736e));
    aiEndpoint.setTextToShowWhenEmpty ("HTTPS endpoint", juce::Colour (0xff62736e));
    aiKeyEnvironment.setTextToShowWhenEmpty ("environment variable", juce::Colour (0xff62736e));
    styleTextLabel (aiProviderLabel, "PROVIDER");
    styleTextLabel (aiModelLabel, "MODEL");
    styleTextLabel (aiEndpointLabel, "ENDPOINT");
    styleTextLabel (aiKeyEnvLabel, "API KEY ENVIRONMENT VARIABLE");
    styleTextLabel (aiSessionKeyLabel, "SESSION API KEY");
    aiStatus.setColour (juce::Label::textColourId, juce::Colour (0xff8fb2a8));
    aiStatus.setFont (juce::Font (juce::FontOptions (10.0f)));
    aiStatus.setJustificationType (juce::Justification::topLeft);

    resynthBackend.addItemList ({ "Native Hybrid / Reference Wavetable", "WORLD Vocoder (experimental speech/vocal backend)" }, 1);
    resynthBackend.setSelectedId (1, juce::dontSendNotification);
    styleTextLabel (resynthBackendLabel, "RESYNTH BACKEND");
    resynthInfo.setColour (juce::Label::textColourId, juce::Colour (0xff9db0aa));
    resynthInfo.setFont (juce::Font (juce::FontOptions (10.5f)));
    resynthInfo.setJustificationType (juce::Justification::topLeft);
    resynthInfo.setText ("Native Hybrid is the production backend for general musical audio. WORLD is a permissive BSD speech/vocal vocoder candidate; it is intentionally not forced into general-instrument matching. See docs/RESYNTH_BACKENDS.md.", juce::dontSendNotification);
    privacyInfo.setColour (juce::Label::textColourId, juce::Colour (0xff8ba099));
    privacyInfo.setFont (juce::Font (juce::FontOptions (10.5f)));
    privacyInfo.setJustificationType (juce::Justification::topLeft);
    privacyInfo.setText ("AI Assist sends numerical analysis features and the current synth seed, not the loaded audio. AI only proposes seeds; RetroMatch renders and scores every candidate locally before you can select it.", juce::dontSendNotification);

    updateAIControlsFromSettings();

    // Audition keyboard.
    keyboardState.addListener (this);
    keyboard.setAvailableRange (36, 96);
    keyboard.setKeyWidth (22.0f);
    keyboard.setScrollButtonsVisible (true);
    keyboard.setColour (juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour (0xffd8d7cf));
    keyboard.setColour (juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour (0xff202528));
    keyboard.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, juce::Colour (0xff596266));
    keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, cyanColour().withAlpha (0.25f));
    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, goldColour().withAlpha (0.65f));
    addAndMakeVisible (keyboard);

    configurePages();
    updateCandidateButtons();

    // Any size change can invoke resized() immediately, so this stays last.
    setResizable (true, true);
    setResizeLimits (1180, 760, 2300, 1500);
    setSize (1520, 920);
    startTimerHz (24);
}

RetroMatchSynthAudioProcessorEditor::~RetroMatchSynthAudioProcessorEditor()
{
    stopTimer();
    if (worker != nullptr)
    {
        worker->signalThreadShouldExit();
        worker->stopThread (10000);
    }
    keyboardState.removeListener (this);
    keyboardState.allNotesOff (0);
    proc.allEditorNotesOff();
    setLookAndFeel (nullptr);
}

//==============================================================================
void RetroMatchSynthAudioProcessorEditor::addKnob (const juce::String& id, const juce::String& name, const juce::String& suffix)
{
    auto knob = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
    knob->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 19);
    knob->setTextValueSuffix (suffix);
    knob->setMouseDragSensitivity (180);
    int decimals = 2;
    if (suffix == " s") decimals = 3;
    else if (suffix == " ct" || suffix == " st" || suffix == " dB") decimals = 1;
    else if (suffix == " Hz") decimals = (id == "cutoff" ? 0 : 2);
    knob->setNumDecimalPlacesToDisplay (decimals);

    auto label = std::make_unique<juce::Label>();
    label->setText (name, juce::dontSendNotification);
    label->setJustificationType (juce::Justification::centred);
    label->setColour (juce::Label::textColourId, juce::Colour (0xffb9c5c1));
    label->setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
    label->setMinimumHorizontalScale (0.70f);

    attachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, *knob));
    knobIds.push_back (id);
    knobs.push_back (std::move (knob));
    labels.push_back (std::move (label));
}

int RetroMatchSynthAudioProcessorEditor::findKnobIndex (const juce::String& id) const
{
    for (size_t i = 0; i < knobIds.size(); ++i)
        if (knobIds[i] == id) return (int) i;
    return -1;
}

void RetroMatchSynthAudioProcessorEditor::moveKnobToPage (const juce::String& id, juce::Component& page)
{
    const int index = findKnobIndex (id);
    if (! juce::isPositiveAndBelow (index, (int) knobs.size())) return;
    page.addAndMakeVisible (*labels[(size_t) index]);
    page.addAndMakeVisible (*knobs[(size_t) index]);
}

void RetroMatchSynthAudioProcessorEditor::configureSectionLabel (juce::Label& label, const juce::String& text, juce::Component& parent)
{
    label.setText (text, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centredLeft);
    label.setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)));
    label.setColour (juce::Label::textColourId, goldColour());
    label.setColour (juce::Label::backgroundColourId, juce::Colour (0xff141c1e));
    parent.addAndMakeVisible (label);
}

void RetroMatchSynthAudioProcessorEditor::configurePages()
{
    addAndMakeVisible (tabs);
    tabs.setTabBarDepth (38);
    tabs.addTab ("SYNTH", juce::Colour (0xff14201e), &synthPage, false);
    tabs.addTab ("FM", juce::Colour (0xff211b14), &fmPage, false);
    tabs.addTab ("FILTER + AMP", juce::Colour (0xff151e20), &filterAmpPage, false);
    tabs.addTab ("MOD", juce::Colour (0xff151c20), &modPage, false);
    tabs.addTab ("FX", juce::Colour (0xff191b20), &fxPage, false);
    tabs.addTab ("SETTINGS", juce::Colour (0xff171d1d), &settingsPage, false);

    configureSectionLabel (synthOscSection, "OSCILLATORS + PITCH", synthPage);
    configureSectionLabel (synthTextureSection, "WAVETABLE + UNISON + HARMONIC SHAPING", synthPage);
    configureSectionLabel (fmCoreSection, "FM / PHASE MOD CORE", fmPage);
    configureSectionLabel (fmOperatorsSection, "SIX-OPERATOR OVERVIEW", fmPage);
    configureSectionLabel (fmDetailSection, "SELECTED OPERATOR DETAIL", fmPage);
    configureSectionLabel (filterSection, "FILTER + DRIVE", filterAmpPage);
    configureSectionLabel (ampSection, "AMPLITUDE + OUTPUT", filterAmpPage);
    configureSectionLabel (modLfoSection, "LFO", modPage);
    configureSectionLabel (modMatrixSection, "MODULATION MATRIX", modPage);
    configureSectionLabel (fxChorusSection, "CHORUS", fxPage);
    configureSectionLabel (fxDelaySection, "DELAY", fxPage);
    configureSectionLabel (fxReverbSection, "REVERB + STEREO", fxPage);
    configureSectionLabel (aiSection, "AI-ASSISTED VARIANT SEEDS", settingsPage);
    configureSectionLabel (backendSection, "RESYNTH BACKEND RESEARCH", settingsPage);
    configureSectionLabel (privacySection, "PRIVACY + EVALUATION", settingsPage);

    for (auto* c : { (juce::Component*) &osc1Label, &osc1Choice, &osc2Label, &osc2Choice }) synthPage.addAndMakeVisible (*c);
    for (auto* c : { (juce::Component*) &fmAlgorithmLabel, &fmAlgorithmChoice, &fmOperatorEditLabel, &fmOperatorEditChoice, &fmModeLabel, &fmModeChoice }) fmPage.addAndMakeVisible (*c);
    for (size_t i = 0; i < fmDetailSliders.size(); ++i) { fmPage.addAndMakeVisible (fmDetailLabels[i]); fmPage.addAndMakeVisible (fmDetailSliders[i]); }
    filterAmpPage.addAndMakeVisible (filterLabel); filterAmpPage.addAndMakeVisible (filterChoice);

    for (int i = 0; i < VoiceParameters::modSlotCount; ++i)
    {
        modPage.addAndMakeVisible (modSlotLabels[(size_t) i]);
        modPage.addAndMakeVisible (modSourceChoices[(size_t) i]);
        modPage.addAndMakeVisible (modDestinationChoices[(size_t) i]);
        modPage.addAndMakeVisible (modAmountSliders[(size_t) i]);
    }

    for (const auto& id : synthOscKnobs) moveKnobToPage (id, synthPage);
    for (const auto& id : synthTextureKnobs) moveKnobToPage (id, synthPage);
    for (const auto& id : fmCoreKnobs) moveKnobToPage (id, fmPage);
    for (const auto& id : fmOperatorKnobs) moveKnobToPage (id, fmPage);
    for (const auto& id : filterKnobs) moveKnobToPage (id, filterAmpPage);
    for (const auto& id : ampKnobs) moveKnobToPage (id, filterAmpPage);
    for (const auto& id : modLfoKnobs) moveKnobToPage (id, modPage);
    for (const auto& id : chorusKnobs) moveKnobToPage (id, fxPage);
    for (const auto& id : delayKnobs) moveKnobToPage (id, fxPage);
    for (const auto& id : reverbKnobs) moveKnobToPage (id, fxPage);

    for (auto* c : { (juce::Component*) &aiEnabled, &aiProviderLabel, &aiProvider, &aiModelLabel, &aiModel,
                     &aiEndpointLabel, &aiEndpoint, &aiKeyEnvLabel, &aiKeyEnvironment,
                     &aiSessionKeyLabel, &aiSessionKey, &aiSaveSettings, &aiStatus,
                     &resynthBackendLabel, &resynthBackend, &resynthInfo, &privacyInfo })
        settingsPage.addAndMakeVisible (*c);
}

//==============================================================================
void RetroMatchSynthAudioProcessorEditor::layoutKnobGrid (const juce::StringArray& ids, juce::Rectangle<int> area, int maxColumns)
{
    if (ids.isEmpty() || area.isEmpty()) return;
    const int count = ids.size();
    const int columns = juce::jmax (1, juce::jmin (count, juce::jmin (maxColumns, juce::jmax (2, area.getWidth() / 118))));
    const int rows = (count + columns - 1) / columns;
    const int cellW = juce::jmax (1, area.getWidth() / columns);
    const int cellH = juce::jmax (1, area.getHeight() / rows);

    for (int i = 0; i < count; ++i)
    {
        const int index = findKnobIndex (ids[i]);
        if (! juce::isPositiveAndBelow (index, (int) knobs.size())) continue;
        const int row = i / columns, column = i % columns;
        auto cell = juce::Rectangle<int> (area.getX() + column * cellW, area.getY() + row * cellH,
                                          column == columns - 1 ? area.getRight() - (area.getX() + column * cellW) : cellW,
                                          cellH).reduced (4, 2);
        labels[(size_t) index]->setBounds (cell.removeFromTop (18));
        knobs[(size_t) index]->setBounds (cell.reduced (3, 0));
    }
}

void RetroMatchSynthAudioProcessorEditor::layoutFmDetailGrid (juce::Rectangle<int> area)
{
    const int count = (int) fmDetailSliders.size();
    const int cellW = juce::jmax (1, area.getWidth() / count);
    for (int i = 0; i < count; ++i)
    {
        auto cell = juce::Rectangle<int> (area.getX() + i * cellW, area.getY(), i == count - 1 ? area.getRight() - (area.getX() + i * cellW) : cellW, area.getHeight()).reduced (4, 1);
        fmDetailLabels[(size_t) i].setBounds (cell.removeFromTop (18));
        fmDetailSliders[(size_t) i].setBounds (cell.reduced (2, 0));
    }
}

void RetroMatchSynthAudioProcessorEditor::layoutPages()
{
    auto synth = synthPage.getLocalBounds().reduced (12);
    synthOscSection.setBounds (synth.removeFromTop (24)); synth.removeFromTop (7);
    auto selectors = synth.removeFromTop (38);
    const int half = selectors.getWidth() / 2;
    auto osc1 = selectors.removeFromLeft (half).reduced (4, 2);
    osc1Label.setBounds (osc1.removeFromLeft (92)); osc1Choice.setBounds (osc1.reduced (4, 0));
    auto osc2 = selectors.reduced (4, 2);
    osc2Label.setBounds (osc2.removeFromLeft (92)); osc2Choice.setBounds (osc2.reduced (4, 0));
    synth.removeFromTop (6);
    auto oscGrid = synth.removeFromTop (juce::jmax (170, synth.getHeight() / 2 - 15));
    layoutKnobGrid (synthOscKnobs, oscGrid, 5);
    synth.removeFromTop (7); synthTextureSection.setBounds (synth.removeFromTop (24)); synth.removeFromTop (5);
    layoutKnobGrid (synthTextureKnobs, synth, 5);

    auto fm = fmPage.getLocalBounds().reduced (12);
    fmCoreSection.setBounds (fm.removeFromTop (24)); fm.removeFromTop (6);
    auto fmHeader = fm.removeFromTop (38);
    auto a = fmHeader.removeFromLeft (fmHeader.getWidth() / 3).reduced (4, 2); fmAlgorithmLabel.setBounds (a.removeFromLeft (105)); fmAlgorithmChoice.setBounds (a);
    auto b = fmHeader.removeFromLeft (fmHeader.getWidth() / 2).reduced (4, 2); fmOperatorEditLabel.setBounds (b.removeFromLeft (95)); fmOperatorEditChoice.setBounds (b);
    auto c = fmHeader.reduced (4, 2); fmModeLabel.setBounds (c.removeFromLeft (100)); fmModeChoice.setBounds (c);
    auto fmCore = fm.removeFromTop (juce::jmax (105, fm.getHeight() / 5)); layoutKnobGrid (fmCoreKnobs, fmCore, 4);
    fm.removeFromTop (5); fmOperatorsSection.setBounds (fm.removeFromTop (24));
    auto ops = fm.removeFromTop (juce::jmax (165, fm.getHeight() / 2)); layoutKnobGrid (fmOperatorKnobs, ops, 6);
    fm.removeFromTop (5); fmDetailSection.setBounds (fm.removeFromTop (24)); layoutFmDetailGrid (fm.reduced (0, 3));

    auto fa = filterAmpPage.getLocalBounds().reduced (12);
    const int split = fa.getHeight() / 2;
    auto filterArea = fa.removeFromTop (split).reduced (0, 2); filterSection.setBounds (filterArea.removeFromTop (24));
    auto filterSelector = filterArea.removeFromTop (38).reduced (4, 4); filterLabel.setBounds (filterSelector.removeFromLeft (95)); filterChoice.setBounds (filterSelector.removeFromLeft (220));
    layoutKnobGrid (filterKnobs, filterArea, 3);
    ampSection.setBounds (fa.removeFromTop (24)); layoutKnobGrid (ampKnobs, fa.reduced (0, 5), 5);

    auto mod = modPage.getLocalBounds().reduced (12);
    modLfoSection.setBounds (mod.removeFromTop (24)); auto lfo = mod.removeFromTop (juce::jmax (135, mod.getHeight() / 3)); layoutKnobGrid (modLfoKnobs, lfo, 4);
    mod.removeFromTop (6); modMatrixSection.setBounds (mod.removeFromTop (24)); mod.removeFromTop (5);
    const int rowH = juce::jmax (50, mod.getHeight() / VoiceParameters::modSlotCount);
    for (int i = 0; i < VoiceParameters::modSlotCount; ++i)
    {
        auto row = mod.removeFromTop (i == VoiceParameters::modSlotCount - 1 ? mod.getHeight() : rowH).reduced (4, 5);
        modSlotLabels[(size_t) i].setBounds (row.removeFromLeft (58));
        modSourceChoices[(size_t) i].setBounds (row.removeFromLeft (juce::jmax (130, row.getWidth() / 3)).reduced (4, 0));
        modDestinationChoices[(size_t) i].setBounds (row.removeFromLeft (juce::jmax (150, row.getWidth() / 2)).reduced (4, 0));
        modAmountSliders[(size_t) i].setBounds (row.reduced (4, 0));
    }

    auto fx = fxPage.getLocalBounds().reduced (12);
    const int third = fx.getWidth() / 3;
    auto chorusArea = fx.removeFromLeft (third).reduced (4); auto delayArea = fx.removeFromLeft (third).reduced (4); auto reverbArea = fx.reduced (4);
    fxChorusSection.setBounds (chorusArea.removeFromTop (24)); layoutKnobGrid (chorusKnobs, chorusArea.reduced (0, 5), 2);
    fxDelaySection.setBounds (delayArea.removeFromTop (24)); layoutKnobGrid (delayKnobs, delayArea.reduced (0, 5), 2);
    fxReverbSection.setBounds (reverbArea.removeFromTop (24)); layoutKnobGrid (reverbKnobs, reverbArea.reduced (0, 5), 2);

    auto settings = settingsPage.getLocalBounds().reduced (12);
    aiSection.setBounds (settings.removeFromTop (24)); settings.removeFromTop (8);
    aiEnabled.setBounds (settings.removeFromTop (30).removeFromLeft (260));
    settings.removeFromTop (3);
    auto providerRow = settings.removeFromTop (34); aiProviderLabel.setBounds (providerRow.removeFromLeft (105)); aiProvider.setBounds (providerRow.removeFromLeft (260).reduced (3, 2));
    auto modelRow = settings.removeFromTop (34); aiModelLabel.setBounds (modelRow.removeFromLeft (105)); aiModel.setBounds (modelRow.reduced (3, 2));
    auto endpointRow = settings.removeFromTop (34); aiEndpointLabel.setBounds (endpointRow.removeFromLeft (105)); aiEndpoint.setBounds (endpointRow.reduced (3, 2));
    auto envRow = settings.removeFromTop (34); aiKeyEnvLabel.setBounds (envRow.removeFromLeft (205)); aiKeyEnvironment.setBounds (envRow.reduced (3, 2));
    auto keyRow = settings.removeFromTop (34); aiSessionKeyLabel.setBounds (keyRow.removeFromLeft (140)); aiSessionKey.setBounds (keyRow.reduced (3, 2));
    auto saveRow = settings.removeFromTop (42); aiSaveSettings.setBounds (saveRow.removeFromLeft (170).reduced (2, 4)); aiStatus.setBounds (saveRow.reduced (10, 1));
    settings.removeFromTop (8); backendSection.setBounds (settings.removeFromTop (24)); settings.removeFromTop (6);
    auto backendRow = settings.removeFromTop (36); resynthBackendLabel.setBounds (backendRow.removeFromLeft (135)); resynthBackend.setBounds (backendRow.reduced (3, 2));
    resynthInfo.setBounds (settings.removeFromTop (juce::jmin (82, settings.getHeight() / 2)).reduced (4, 5));
    settings.removeFromTop (5); privacySection.setBounds (settings.removeFromTop (24)); privacyInfo.setBounds (settings.reduced (4, 6));
}

void RetroMatchSynthAudioProcessorEditor::resized()
{
    auto outer = getLocalBounds().reduced (14);
    auto header = outer.removeFromTop (58);
    title.setBounds (header.removeFromLeft (210));
    subtitle.setBounds (header.removeFromLeft (juce::jmax (220, header.getWidth() - 430)));
    const int actionW = juce::jmax (78, header.getWidth() / 4);
    keyboardToggle.setBounds (header.removeFromRight (actionW).reduced (3, 9));
    exportPreview.setBounds (header.removeFromRight (actionW).reduced (3, 9));
    savePatch.setBounds (header.removeFromRight (actionW).reduced (3, 9));
    loadPatch.setBounds (header.reduced (3, 9));
    outer.removeFromTop (8);

    if (keyboardVisible)
    {
        auto keyboardArea = outer.removeFromBottom (112);
        keyboard.setBounds (keyboardArea.reduced (2, 5));
        outer.removeFromBottom (5);
    }
    else keyboard.setBounds (0, 0, 0, 0);

    const int workspaceWidth = juce::jlimit (410, 530, (int) std::round (outer.getWidth() * 0.35));
    workspaceBounds = outer.removeFromLeft (workspaceWidth).reduced (0, 0);
    outer.removeFromLeft (10);
    tabs.setBounds (outer);

    auto w = workspaceBounds.reduced (12);
    load.setBounds (w.removeFromTop (34)); w.removeFromTop (7);
    const int analyzerHeight = juce::jlimit (180, 255, (int) std::round (w.getHeight() * 0.34));
    analyzerBounds = w.removeFromTop (analyzerHeight); w.removeFromTop (6);
    pipelineBounds = w.removeFromTop (36); w.removeFromTop (7);

    auto actionRow = w.removeFromTop (34);
    const int buttonW = actionRow.getWidth() / 3;
    quick.setBounds (actionRow.removeFromLeft (buttonW).reduced (2, 0));
    refine.setBounds (actionRow.removeFromLeft (buttonW).reduced (2, 0));
    aiVariants.setBounds (actionRow.reduced (2, 0));
    w.removeFromTop (7);

    auto footer = w.removeFromBottom (88);
    auto morphRow = footer.removeFromTop (36); candidateMorphLabel.setBounds (morphRow.removeFromLeft (128)); candidateMorph.setBounds (morphRow);
    footer.removeFromTop (4); progressBar.setBounds (footer.removeFromTop (20)); footer.removeFromTop (4); status.setBounds (footer);

    const int candidateGap = 5;
    const int cardH = juce::jmax (50, (w.getHeight() - candidateGap * 2) / 3);
    candidateA.setBounds (w.removeFromTop (cardH)); w.removeFromTop (candidateGap);
    candidateB.setBounds (w.removeFromTop (cardH)); w.removeFromTop (candidateGap);
    candidateC.setBounds (w);

    layoutPages();
}

//==============================================================================
void RetroMatchSynthAudioProcessorEditor::drawWorkspaceBackground (juce::Graphics& g)
{
    auto r = workspaceBounds.toFloat();
    g.setColour (juce::Colour (0xff111719));
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (juce::Colour (0xff334145));
    g.drawRoundedRectangle (r, 10.0f, 1.0f);
    g.setColour (goldColour());
    g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
    g.drawText ("REFERENCE + RESYNTH WORKSPACE", workspaceBounds.getX() + 12, workspaceBounds.getY() - 1, workspaceBounds.getWidth() - 24, 16, juce::Justification::centredRight);
}

void RetroMatchSynthAudioProcessorEditor::drawAnalyzer (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour (juce::Colour (0xff04120f));
    g.fillRoundedRectangle (area, 8.0f);
    g.setColour (juce::Colour (0xff2e675c));
    g.drawRoundedRectangle (area, 8.0f, 1.2f);

    auto header = area.removeFromTop (27.0f).reduced (10.0f, 0.0f);
    g.setColour (tealColour());
    g.setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)));
    g.drawText (proc.loadedSampleName.isNotEmpty() ? proc.loadedSampleName : "DROP WAV / AIFF / FLAC HERE", header, juce::Justification::centredLeft, true);

    if (! proc.currentFeatures)
    {
        g.setColour (juce::Colour (0xff58746b));
        g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        g.drawText ("REFERENCE AUDIO", area, juce::Justification::centred);
        return;
    }

    const auto& reference = *proc.currentFeatures;
    auto stats = area.removeFromTop (23.0f).reduced (10.0f, 0.0f);
    g.setColour (juce::Colour (0xff9ec9bd));
    g.setFont (juce::Font (juce::FontOptions (9.0f)));
    g.drawText (juce::String (reference.fundamentalHz, 1) + " Hz  |  centroid " + juce::String (reference.spectralCentroidHz / 1000.0f, 1)
                + " kHz  |  harmonic " + juce::String (reference.harmonicity, 2)
                + "  |  transient " + juce::String (reference.transientScore, 2), stats, juce::Justification::centredLeft, true);

    auto graph = area.reduced (10.0f, 6.0f);
    auto wave = graph.removeFromTop (graph.getHeight() * 0.52f);
    auto spectrum = graph.reduced (0.0f, 3.0f);
    g.setColour (juce::Colour (0xff17352f));
    g.drawHorizontalLine ((int) wave.getCentreY(), wave.getX(), wave.getRight());

    auto drawWave = [&] (const SoundFeatures& features, juce::Colour colour, float alpha, float width)
    {
        juce::Path path;
        for (int i = 0; i < SoundFeatures::waveformPointCount; ++i)
        {
            const float x = juce::jmap ((float) i, 0.0f, (float) (SoundFeatures::waveformPointCount - 1), wave.getX(), wave.getRight());
            const float y = wave.getCentreY() - features.waveformPreview[(size_t) i] * wave.getHeight() * 0.44f;
            if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
        }
        g.setColour (colour.withAlpha (alpha));
        g.strokePath (path, juce::PathStrokeType (width));
    };
    drawWave (reference, tealColour(), 0.95f, 1.35f);
    if (proc.currentCandidateFeatures) drawWave (*proc.currentCandidateFeatures, goldColour(), 0.90f, 1.15f);

    const float bandW = spectrum.getWidth() / SoundFeatures::spectralBandCount;
    for (int i = 0; i < SoundFeatures::spectralBandCount; ++i)
    {
        const float x = spectrum.getX() + i * bandW;
        const float rh = reference.spectralBands[(size_t) i] * spectrum.getHeight();
        g.setColour (juce::Colour (0xff348b77).withAlpha (0.55f));
        g.fillRect (x, spectrum.getBottom() - rh, juce::jmax (1.0f, bandW - 1.0f), rh);
        if (proc.currentCandidateFeatures)
        {
            const float ch = proc.currentCandidateFeatures->spectralBands[(size_t) i] * spectrum.getHeight();
            g.setColour (goldColour().withAlpha (0.68f));
            g.fillRect (x + bandW * 0.28f, spectrum.getBottom() - ch, juce::jmax (1.0f, bandW * 0.42f), ch);
        }
    }

    if (proc.lastMatch.similarity.total > 0.0f)
    {
        auto badge = juce::Rectangle<float> (area.getRight() - 72.0f, area.getY() + 2.0f, 62.0f, 28.0f);
        g.setColour (juce::Colour (0xff102925)); g.fillRoundedRectangle (badge, 5.0f);
        g.setColour (goldColour()); g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
        g.drawText (scoreText (proc.lastMatch.similarity.total), badge, juce::Justification::centred);
    }
}

void RetroMatchSynthAudioProcessorEditor::drawPipeline (juce::Graphics& g, juce::Rectangle<float> area)
{
    const std::array<const char*, 5> stages { "ANALYZE", "SEED", "RENDER", "SCORE", "VARIANTS" };
    const bool running = worker != nullptr && worker->isThreadRunning();
    const float progress = matchProgress.load();
    const int activeStage = running ? juce::jlimit (0, 4, (int) std::floor (progress * 5.0f)) : (proc.candidateBank[0].confidence > 0.0f ? 4 : (proc.currentFeatures ? 0 : -1));
    const float gap = 4.0f;
    const float stageW = (area.getWidth() - gap * 4.0f) / 5.0f;

    for (int i = 0; i < 5; ++i)
    {
        auto stage = juce::Rectangle<float> (area.getX() + i * (stageW + gap), area.getY(), stageW, area.getHeight());
        const bool complete = running ? i < activeStage : i <= activeStage;
        const bool active = running && i == activeStage;
        g.setColour (active ? juce::Colour (0xff204f48) : (complete ? juce::Colour (0xff172d29) : juce::Colour (0xff151c1e)));
        g.fillRoundedRectangle (stage, 5.0f);
        g.setColour (active ? tealColour() : (complete ? juce::Colour (0xff4f8a7c) : juce::Colour (0xff344044)));
        g.drawRoundedRectangle (stage, 5.0f, active ? 1.6f : 1.0f);
        auto led = juce::Rectangle<float> (stage.getX() + 7.0f, stage.getCentreY() - 3.0f, 6.0f, 6.0f);
        g.setColour (active ? tealColour() : (complete ? goldColour() : juce::Colour (0xff384643)));
        g.fillEllipse (led);
        g.setColour (juce::Colour (0xffb6c4bf));
        g.setFont (juce::Font (juce::FontOptions (7.8f, juce::Font::bold)));
        g.drawText (stages[(size_t) i], stage.reduced (15.0f, 0.0f), juce::Justification::centred);
    }
}

void RetroMatchSynthAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff070a0c));
    auto body = getLocalBounds().toFloat().reduced (7.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff22292b), body.getTopLeft(), juce::Colour (0xff101416), body.getBottomLeft(), false));
    g.fillRoundedRectangle (body, 12.0f);
    g.setColour (juce::Colour (0xff3c474a));
    g.drawRoundedRectangle (body, 12.0f, 1.0f);
    g.setColour (goldColour().withAlpha (0.65f));
    g.fillRect (14.0f, 70.0f, (float) getWidth() - 28.0f, 1.0f);

    drawWorkspaceBackground (g);
    if (! analyzerBounds.isEmpty()) drawAnalyzer (g, analyzerBounds.toFloat());
    if (! pipelineBounds.isEmpty()) drawPipeline (g, pipelineBounds.toFloat());
}

//==============================================================================
void RetroMatchSynthAudioProcessorEditor::startVariantSearch (WorkMode mode)
{
    if (! proc.currentFeatures)
    {
        status.setText ("Load a reference before matching.", juce::dontSendNotification);
        return;
    }
    if (worker != nullptr && worker->isThreadRunning()) return;

    if (mode == WorkMode::ai)
    {
        syncAISettingsFromControls();
        if (! aiSettings.hasUsableConfiguration())
        {
            status.setText (aiSettings.configurationHint(), juce::dontSendNotification);
            tabs.setCurrentTabIndex (5);
            return;
        }
    }

    activeWorkMode = mode;
    matchProgress.store (0.0f);
    progressDisplay = 0.0;
    progressBar.setVisible (true);
    for (auto* b : { &load, &quick, &refine, &aiVariants }) b->setEnabled (false);
    status.setText (mode == WorkMode::quick ? "Quick matching: rendering three distinct local variants..."
                   : mode == WorkMode::refine ? "Refining three variant families with the closed-loop optimizer..."
                                              : "AI is proposing three seeds; RetroMatch will render and score them locally...",
                    juce::dontSendNotification);
    worker = std::make_unique<VariantThread> (*this, mode);
    worker->startThread();
}

std::array<MatchResult, 3> RetroMatchSynthAudioProcessorEditor::createLocalVariants (bool refined, VariantThread& thread)
{
    std::array<MatchResult, 3> results {};
    if (! proc.currentFeatures) return results;
    const auto reference = *proc.currentFeatures;
    auto base = refined && proc.lastMatch.confidence > 0.0f ? proc.getCurrentVoiceParameters() : SoundMatcher::initialFit (reference).params;
    base.referenceWavetable = proc.referenceWavetable;

    std::array<VoiceParameters, 3> seeds { base, base, base };
    seeds[0].referenceWavetableMix = proc.referenceWavetable ? juce::jmax (0.28f, base.referenceWavetableMix) : 0.0f;
    seeds[0].additiveMix = juce::jmax (base.additiveMix, 0.10f);

    seeds[1].fmMix = juce::jmax (0.35f, base.fmMix);
    seeds[1].fmFeedback = juce::jmax (0.08f, base.fmFeedback);
    seeds[1].fmAlgorithm = (base.fmAlgorithm + 2) % 6;
    seeds[1].referenceWavetableMix *= 0.45f;

    seeds[2].wavetableMix = juce::jmax (0.38f, base.wavetableMix);
    seeds[2].supersawMix = juce::jmax (0.30f, base.supersawMix);
    seeds[2].unisonSpread = juce::jmax (0.62f, base.unisonSpread);
    seeds[2].wavefold = juce::jmax (0.08f, base.wavefold);
    seeds[2].referenceWavetableMix = proc.referenceWavetable ? juce::jmax (0.16f, base.referenceWavetableMix * 0.7f) : 0.0f;

    auto settings = proc.matchSettings;
    if (! refined)
    {
        settings.iterations = juce::jmin (24, juce::jmax (14, settings.iterations / 7));
        settings.topologyTrials = juce::jmin (6, juce::jmax (3, settings.topologyTrials / 5));
        settings.populationSize = juce::jmin (4, settings.populationSize);
        settings.maxRenderSeconds = juce::jmin (1.5f, settings.maxRenderSeconds);
    }
    else
    {
        settings.iterations = juce::jmax (72, settings.iterations / 2);
        settings.topologyTrials = juce::jmax (12, settings.topologyTrials / 2);
        settings.populationSize = juce::jmax (5, settings.populationSize);
    }

    const juce::String familyNames[] { "Natural / spectral", "FM / harmonic", "Wavetable / texture" };
    for (int i = 0; i < 3; ++i)
    {
        if (thread.threadShouldExit()) return {};
        auto progress = [this, i] (float p) { matchProgress.store (((float) i + juce::jlimit (0.0f, 1.0f, p)) / 3.0f); };
        auto cancel = [&thread] { return thread.threadShouldExit(); };
        results[(size_t) i] = SoundMatcher::refineFit (reference, seeds[(size_t) i], settings, std::move (progress), std::move (cancel));
        results[(size_t) i].explanation = familyNames[i] + ". " + results[(size_t) i].explanation;
    }
    return results;
}

void RetroMatchSynthAudioProcessorEditor::runVariantSearch (WorkMode mode, VariantThread& thread)
{
    if (mode == WorkMode::ai)
    {
        const auto settingsCopy = aiSettings;
        auto base = proc.lastMatch.confidence > 0.0f ? proc.getCurrentVoiceParameters() : SoundMatcher::initialFit (*proc.currentFeatures).params;
        base.referenceWavetable = proc.referenceWavetable;
        auto batch = AISeedProvider::generateVariants (*proc.currentFeatures, base, proc.matchSettings, settingsCopy,
                                                       [this] (float p) { matchProgress.store (p); },
                                                       [&thread] { return thread.threadShouldExit(); });
        if (thread.threadShouldExit()) return;
        juce::Component::SafePointer<RetroMatchSynthAudioProcessorEditor> safe (this);
        juce::MessageManager::callAsync ([safe, batch = std::move (batch)] () mutable
        {
            if (safe != nullptr) safe->finishVariantSearch (std::move (batch.candidates), batch.providerSummary, batch.error);
        });
        return;
    }

    auto variants = createLocalVariants (mode == WorkMode::refine, thread);
    if (thread.threadShouldExit()) return;
    juce::Component::SafePointer<RetroMatchSynthAudioProcessorEditor> safe (this);
    const auto source = mode == WorkMode::refine ? juce::String ("REFINE") : juce::String ("QUICK");
    juce::MessageManager::callAsync ([safe, variants = std::move (variants), source] () mutable
    {
        if (safe != nullptr) safe->finishVariantSearch (std::move (variants), source);
    });
}

void RetroMatchSynthAudioProcessorEditor::finishVariantSearch (std::array<MatchResult, 3> results, const juce::String& sourceLabel, const juce::String& error)
{
    for (auto* b : { &load, &quick, &refine, &aiVariants }) b->setEnabled (true);
    progressDisplay = error.isEmpty() ? 1.0 : 0.0;
    progressBar.setVisible (false);

    if (error.isNotEmpty())
    {
        status.setText ("AI match failed: " + error, juce::dontSendNotification);
        updateAIStatus();
        return;
    }

    proc.candidateBank = results;
    int best = 0;
    for (int i = 1; i < 3; ++i)
        if (results[(size_t) i].similarity.total > results[(size_t) best].similarity.total) best = i;
    proc.selectCandidate (best);
    candidateMorph.setValue (best == 0 ? 0.0 : (best == 1 ? 0.5 : 1.0), juce::dontSendNotification);
    updateCandidateButtons();

    status.setText (sourceLabel + " variants ready: A " + scoreText (results[0].similarity.total)
                    + "  B " + scoreText (results[1].similarity.total)
                    + "  C " + scoreText (results[2].similarity.total)
                    + ". Selected best-scoring variant " + juce::String::charToString ((juce_wchar) ('A' + best)) + ".",
                    juce::dontSendNotification);
    repaint();
}

void RetroMatchSynthAudioProcessorEditor::updateCandidateButtons()
{
    candidateA.setResult (proc.candidateBank[0].confidence > 0.0f ? &proc.candidateBank[0] : nullptr, proc.selectedCandidate == 0);
    candidateB.setResult (proc.candidateBank[1].confidence > 0.0f ? &proc.candidateBank[1] : nullptr, proc.selectedCandidate == 1);
    candidateC.setResult (proc.candidateBank[2].confidence > 0.0f ? &proc.candidateBank[2] : nullptr, proc.selectedCandidate == 2);
}

void RetroMatchSynthAudioProcessorEditor::selectCandidate (int index)
{
    if (! proc.selectCandidate (index)) return;
    candidateMorph.setValue (index == 0 ? 0.0 : (index == 1 ? 0.5 : 1.0), juce::dontSendNotification);
    updateCandidateButtons();
    status.setText ("Variant " + juce::String::charToString ((juce_wchar) ('A' + index)) + " selected. Use the keyboard or host MIDI to audition it.", juce::dontSendNotification);
    repaint();
}

//==============================================================================
void RetroMatchSynthAudioProcessorEditor::syncAISettingsFromControls()
{
    aiSettings.enabled = aiEnabled.getToggleState() && aiProvider.getSelectedId() > 1;
    aiSettings.provider = static_cast<AIProvider> (juce::jlimit (0, 4, aiProvider.getSelectedId() - 1));
    aiSettings.model = aiModel.getText().trim();
    aiSettings.endpoint = aiEndpoint.getText().trim();
    aiSettings.apiKeyEnvironment = aiKeyEnvironment.getText().trim();
    aiSettings.sessionApiKey = aiSessionKey.getText();
    aiSettings.featuresOnly = true;
}

void RetroMatchSynthAudioProcessorEditor::updateAIControlsFromSettings()
{
    aiEnabled.setToggleState (aiSettings.enabled, juce::dontSendNotification);
    aiProvider.setSelectedId ((int) aiSettings.provider + 1, juce::dontSendNotification);
    aiModel.setText (aiSettings.model, false);
    aiEndpoint.setText (aiSettings.endpoint, false);
    aiKeyEnvironment.setText (aiSettings.apiKeyEnvironment, false);
    updateAIStatus();
}

void RetroMatchSynthAudioProcessorEditor::updateAIStatus()
{
    syncAISettingsFromControls();
    aiStatus.setText (aiSettings.configurationHint(), juce::dontSendNotification);
    aiVariants.setEnabled (worker == nullptr || ! worker->isThreadRunning());
}

//==============================================================================
void RetroMatchSynthAudioProcessorEditor::chooseFile()
{
    auto chooser = std::make_shared<juce::FileChooser> ("Choose reference sound", juce::File {}, "*.wav;*.aif;*.aiff;*.flac");
    juce::Component::SafePointer<RetroMatchSynthAudioProcessorEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe, chooser] (const juce::FileChooser& c)
    {
        if (safe == nullptr) return;
        const auto file = c.getResult();
        if (! file.existsAsFile()) return;
        const bool ok = safe->proc.loadReferenceSample (file);
        safe->proc.candidateBank = {};
        safe->updateCandidateButtons();
        safe->status.setText (ok ? "Reference analysed. Choose Quick x3, Refine x3 or AI x3."
                                 : "Could not analyse this reference.", juce::dontSendNotification);
        safe->repaint();
    });
}

void RetroMatchSynthAudioProcessorEditor::filesDropped (const juce::StringArray& files, int, int)
{
    if (files.isEmpty() || (worker != nullptr && worker->isThreadRunning())) return;
    const auto file = juce::File (files[0]);
    if (proc.loadReferenceSample (file))
    {
        proc.candidateBank = {};
        updateCandidateButtons();
        status.setText ("Reference analysed. Choose Quick x3, Refine x3 or AI x3.", juce::dontSendNotification);
    }
    repaint();
}

void RetroMatchSynthAudioProcessorEditor::chooseSavePatch()
{
    auto chooser = std::make_shared<juce::FileChooser> ("Save RetroMatch patch", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("RetroMatch-Patch.rmsynth"), "*.rmsynth");
    juce::Component::SafePointer<RetroMatchSynthAudioProcessorEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe, chooser] (const juce::FileChooser& c)
    {
        if (safe == nullptr) return;
        auto file = c.getResult();
        if (file.getFileExtension().isEmpty()) file = file.withFileExtension (".rmsynth");
        safe->status.setText (safe->proc.savePreset (file) ? "Patch saved: " + file.getFileName() : "Could not save patch.", juce::dontSendNotification);
    });
}

void RetroMatchSynthAudioProcessorEditor::chooseLoadPatch()
{
    auto chooser = std::make_shared<juce::FileChooser> ("Load RetroMatch patch", juce::File {}, "*.rmsynth");
    juce::Component::SafePointer<RetroMatchSynthAudioProcessorEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe, chooser] (const juce::FileChooser& c)
    {
        if (safe == nullptr) return;
        const auto file = c.getResult();
        if (! file.existsAsFile()) return;
        safe->status.setText (safe->proc.loadPreset (file) ? "Patch loaded: " + file.getFileName() : "Invalid RetroMatch patch.", juce::dontSendNotification);
    });
}

void RetroMatchSynthAudioProcessorEditor::chooseExportPreview()
{
    auto chooser = std::make_shared<juce::FileChooser> ("Export synth preview", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("RetroMatch-Preview.wav"), "*.wav");
    juce::Component::SafePointer<RetroMatchSynthAudioProcessorEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe, chooser] (const juce::FileChooser& c)
    {
        if (safe == nullptr) return;
        auto file = c.getResult();
        if (file.getFileExtension().isEmpty()) file = file.withFileExtension (".wav");
        safe->status.setText (safe->proc.exportPreviewWav (file) ? "Preview exported: " + file.getFileName() : "Could not export preview.", juce::dontSendNotification);
    });
}

//==============================================================================
void RetroMatchSynthAudioProcessorEditor::rebindFmOperatorEditor()
{
    selectedFmOperator = juce::jlimit (0, VoiceParameters::fmOperatorCount - 1, fmOperatorEditChoice.getSelectedId() - 1);
    const auto prefix = "fmOp" + juce::String (selectedFmOperator + 1);
    fmModeAttachment.reset();
    for (auto& attachment : fmDetailAttachments) attachment.reset();
    fmModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, prefix + "Mode", fmModeChoice);
    const juce::String suffixes[] { "FixedHz", "Attack", "Decay", "Sustain", "Release", "KeyScale", "Velocity" };
    for (size_t i = 0; i < fmDetailAttachments.size(); ++i)
        fmDetailAttachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, prefix + suffixes[i], fmDetailSliders[i]);
}

void RetroMatchSynthAudioProcessorEditor::timerCallback()
{
    progressDisplay = matchProgress.load();
    if (worker != nullptr && ! worker->isThreadRunning())
        progressBar.setVisible (false);
    repaint (workspaceBounds);
}

void RetroMatchSynthAudioProcessorEditor::handleNoteOn (juce::MidiKeyboardState*, int, int midiNoteNumber, float velocity)
{
    proc.noteOnFromEditor (midiNoteNumber, velocity);
}

void RetroMatchSynthAudioProcessorEditor::handleNoteOff (juce::MidiKeyboardState*, int, int midiNoteNumber, float velocity)
{
    proc.noteOffFromEditor (midiNoteNumber, velocity);
}
