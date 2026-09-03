#include "PluginEditor.h"
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
const juce::StringArray filterKnobs { "cutoff", "resonance" };
const juce::StringArray ampKnobs { "attack", "decay", "sustain", "release", "drive", "outputGain" };
const juce::StringArray modLfoKnobs { "lfoRate", "lfoPitch", "lfoCutoff", "lfoAmp" };
const juce::StringArray chorusKnobs { "chorusMix", "chorusRate", "chorusDepth" };
const juce::StringArray delayKnobs { "delayMix", "delayTime", "delayFeedback" };
const juce::StringArray reverbKnobs { "reverbMix", "reverbSize", "reverbDamping", "stereoWidth" };

juce::StringArray makeFmOperatorKnobs()
{
    juce::StringArray ids;
    for (int i = 1; i <= VoiceParameters::fmOperatorCount; ++i)
    {
        ids.add ("fmOp" + juce::String (i) + "Ratio");
        ids.add ("fmOp" + juce::String (i) + "Level");
    }
    return ids;
}

const juce::StringArray fmOperatorKnobs = makeFmOperatorKnobs();
}

RetroMatchSynthAudioProcessorEditor::MatchThread::MatchThread (RetroMatchSynthAudioProcessorEditor& ownerIn)
    : juce::Thread ("RetroMatch optimizer"), owner (ownerIn)
{
}

void RetroMatchSynthAudioProcessorEditor::MatchThread::run()
{
    auto result = owner.proc.refineReference (
        [this] (float p) { owner.matchProgress.store (p); },
        [this] { return threadShouldExit(); });

    if (threadShouldExit()) return;
    juce::Component::SafePointer<RetroMatchSynthAudioProcessorEditor> safe (&owner);
    juce::MessageManager::callAsync ([safe, result = std::move (result)] () mutable
    {
        if (safe != nullptr) safe->finishRefine (std::move (result));
    });
}

RetroMatchSynthAudioProcessorEditor::RetroMatchSynthAudioProcessorEditor (RetroMatchSynthAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&laf);

    title.setText ("RETRO MATCH // HYBRID SYNTHESIS LAB", juce::dontSendNotification);
    title.setFont (juce::Font (juce::FontOptions (24.0f, juce::Font::bold)));
    title.setColour (juce::Label::textColourId, juce::Colour (0xffe5c878));
    title.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (title);

    status.setText ("Drop a reference sample or use LOAD SAMPLE.", juce::dontSendNotification);
    status.setColour (juce::Label::textColourId, juce::Colour (0xffa8bbb6));
    status.setJustificationType (juce::Justification::centredLeft);
    status.setMinimumHorizontalScale (0.65f);
    addAndMakeVisible (status);

    load.onClick = [this] { chooseFile(); };
    match.onClick = [this] { applyQuickMatch(); };
    refine.onClick = [this] { startRefine(); };
    savePatch.onClick = [this] { chooseSavePatch(); };
    loadPatch.onClick = [this] { chooseLoadPatch(); };
    exportPreview.onClick = [this] { chooseExportPreview(); };
    for (auto* b : { &load, &match, &refine, &savePatch, &loadPatch, &exportPreview }) addAndMakeVisible (*b);

    makeCandidates.onClick = [this]
    {
        auto bank = proc.buildCandidateBank();
        status.setText ("A/B/C: " + juce::String (bank[0].confidence * 100.0f, 1) + "% | "
                        + juce::String (bank[1].confidence * 100.0f, 1) + "% | "
                        + juce::String (bank[2].confidence * 100.0f, 1) + "%", juce::dontSendNotification);
        repaint();
    };
    candidateA.onClick = [this] { proc.selectCandidate (0); candidateMorph.setValue (0.0, juce::dontSendNotification); repaint(); };
    candidateB.onClick = [this] { proc.selectCandidate (1); candidateMorph.setValue (0.5, juce::dontSendNotification); repaint(); };
    candidateC.onClick = [this] { proc.selectCandidate (2); candidateMorph.setValue (1.0, juce::dontSendNotification); repaint(); };
    for (auto* b : { &makeCandidates, &candidateA, &candidateB, &candidateC }) addAndMakeVisible (*b);

    candidateMorph.setSliderStyle (juce::Slider::LinearHorizontal);
    candidateMorph.setRange (0.0, 1.0, 0.001);
    candidateMorph.setValue (0.0, juce::dontSendNotification);
    candidateMorph.setNumDecimalPlacesToDisplay (2);
    candidateMorph.setTextBoxStyle (juce::Slider::TextBoxRight, false, 58, 20);
    candidateMorph.onValueChange = [this]
    {
        const float v = (float) candidateMorph.getValue();
        if (v <= 0.5f) proc.morphCandidates (0, 1, v * 2.0f);
        else proc.morphCandidates (1, 2, (v - 0.5f) * 2.0f);
        repaint();
    };
    candidateMorphLabel.setText ("A  <  MORPH  >  C", juce::dontSendNotification);
    candidateMorphLabel.setColour (juce::Label::textColourId, juce::Colour (0xffd1b86e));
    candidateMorphLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (candidateMorph);
    addAndMakeVisible (candidateMorphLabel);

    const juce::String lockNames[] { "PITCH", "OSC", "FM", "ENVELOPE", "FILTER", "MOD", "FX" };
    for (size_t i = 0; i < matchLockButtons.size(); ++i)
    {
        matchLockButtons[i] = std::make_unique<juce::TextButton> (lockNames[i]);
        matchLockButtons[i]->setClickingTogglesState (true);
        matchLockButtons[i]->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff5e4b2b));
        matchLockButtons[i]->onClick = [this] { syncMatchLocks(); };
        addAndMakeVisible (*matchLockButtons[i]);
    }
    matchLockButtons[0]->setToggleState (proc.matchSettings.lockPitch, juce::dontSendNotification);
    matchLockButtons[1]->setToggleState (proc.matchSettings.lockOscillators, juce::dontSendNotification);
    matchLockButtons[2]->setToggleState (proc.matchSettings.lockFm, juce::dontSendNotification);
    matchLockButtons[3]->setToggleState (proc.matchSettings.lockEnvelope, juce::dontSendNotification);
    matchLockButtons[4]->setToggleState (proc.matchSettings.lockFilter, juce::dontSendNotification);
    matchLockButtons[5]->setToggleState (proc.matchSettings.lockModulation, juce::dontSendNotification);
    matchLockButtons[6]->setToggleState (proc.matchSettings.lockEffects, juce::dontSendNotification);

    progressBar.setPercentageDisplay (true);
    progressBar.setVisible (false);
    addAndMakeVisible (progressBar);

    osc1Label.setText ("OSC 1 WAVE", juce::dontSendNotification);
    osc2Label.setText ("OSC 2 WAVE", juce::dontSendNotification);
    filterLabel.setText ("FILTER MODE", juce::dontSendNotification);
    fmAlgorithmLabel.setText ("ALGORITHM", juce::dontSendNotification);
    for (auto* l : { &osc1Label, &osc2Label, &filterLabel, &fmAlgorithmLabel })
    {
        l->setColour (juce::Label::textColourId, juce::Colour (0xffaeb9b8));
        l->setJustificationType (juce::Justification::centredLeft);
        l->setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        addAndMakeVisible (*l);
    }

    osc1Choice.addItemList ({ "Sine", "Saw", "Square", "Triangle", "Pulse" }, 1);
    osc2Choice.addItemList ({ "Sine", "Saw", "Square", "Triangle", "Pulse" }, 1);
    filterChoice.addItemList ({ "Low-pass", "High-pass", "Band-pass" }, 1);
    fmAlgorithmChoice.addItemList ({ "Stack", "Dual Stack", "Triple Pair", "Star", "Branch", "Six Carriers" }, 1);
    addAndMakeVisible (osc1Choice);
    addAndMakeVisible (osc2Choice);
    addAndMakeVisible (filterChoice);
    addAndMakeVisible (fmAlgorithmChoice);
    osc1Attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "osc1Wave", osc1Choice);
    osc2Attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "osc2Wave", osc2Choice);
    filterAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "filterType", filterChoice);
    fmAlgorithmAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "fmAlgorithm", fmAlgorithmChoice);

    fmOperatorEditLabel.setText ("EDIT OPERATOR", juce::dontSendNotification);
    fmModeLabel.setText ("FREQUENCY", juce::dontSendNotification);
    for (auto* l : { &fmOperatorEditLabel, &fmModeLabel })
    {
        l->setColour (juce::Label::textColourId, juce::Colour (0xffc9b16d));
        l->setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)));
        l->setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (*l);
    }
    fmOperatorEditChoice.addItemList ({ "OP 1", "OP 2", "OP 3", "OP 4", "OP 5", "OP 6" }, 1);
    fmOperatorEditChoice.setSelectedId (1, juce::dontSendNotification);
    fmOperatorEditChoice.onChange = [this] { rebindFmOperatorEditor(); };
    fmModeChoice.addItemList ({ "Ratio", "Fixed" }, 1);
    addAndMakeVisible (fmOperatorEditChoice);
    addAndMakeVisible (fmModeChoice);

    const juce::String fmDetailNames[] { "FIXED HZ", "ATTACK", "DECAY", "SUSTAIN", "RELEASE", "KEY SCALE", "VELOCITY" };
    const juce::String fmDetailSuffix[] { " Hz", " s", " s", "", " s", "", "" };
    for (size_t i = 0; i < fmDetailSliders.size(); ++i)
    {
        auto& label = fmDetailLabels[i];
        label.setText (fmDetailNames[i], juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setColour (juce::Label::textColourId, juce::Colour (0xffaeb7b8));
        label.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
        addAndMakeVisible (label);

        auto& slider = fmDetailSliders[i];
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 18);
        slider.setTextValueSuffix (fmDetailSuffix[i]);
        slider.setNumDecimalPlacesToDisplay (i == 0 ? 0 : ((i == 1 || i == 2 || i == 4) ? 3 : 2));
        addAndMakeVisible (slider);
    }

    const std::tuple<const char*, const char*, const char*> knobDefs[] = {
        { "osc1Mix", "OSC 1", "" }, { "osc2Mix", "OSC 2", "" }, { "subMix", "SUB", "" }, { "noise", "NOISE", "" },
        { "ringMix", "RING", "" }, { "additiveMix", "ADDITIVE", "" }, { "wavetableMix", "WAVETABLE", "" }, { "referenceWavetableMix", "REF WT", "" }, { "wavetablePosition", "WT POSITION", "" }, { "wavetableWarp", "WT WARP", "" },
        { "supersawMix", "SUPERSAW", "" }, { "unisonDetune", "UNI DETUNE", " ct" }, { "unisonSpread", "UNI SPREAD", "" }, { "wavefold", "WAVEFOLD", "" },
        { "masterTune", "TUNE", " ct" }, { "osc2Semi", "OSC2 SEMI", " st" }, { "osc2Detune", "OSC2 FINE", " ct" }, { "pulseWidth", "PULSE", "" },
        { "fmAmount", "PM AMOUNT", "" }, { "fmRatio", "PM RATIO", " x" }, { "fmMix", "6-OP FM", "" }, { "fmFeedback", "FM FEEDBACK", "" }, { "harmonicTilt", "HARM TILT", "" }, { "oddEven", "ODD/EVEN", "" }, { "cutoff", "CUTOFF", " Hz" }, { "resonance", "RESONANCE", "" },
        { "attack", "ATTACK", " s" }, { "decay", "DECAY", " s" }, { "sustain", "SUSTAIN", "" }, { "release", "RELEASE", " s" },
        { "lfoRate", "LFO RATE", " Hz" }, { "lfoPitch", "LFO TO PITCH", " st" }, { "lfoCutoff", "LFO TO FILTER", "" }, { "lfoAmp", "LFO TO AMP", "" },
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
        modSlotLabels[(size_t) i].setColour (juce::Label::textColourId, juce::Colour (0xffc9b16d));
        modSlotLabels[(size_t) i].setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)));
        addAndMakeVisible (modSlotLabels[(size_t) i]);

        modSourceChoices[(size_t) i].addItemList (modSources, 1);
        modDestinationChoices[(size_t) i].addItemList (modDestinations, 1);
        addAndMakeVisible (modSourceChoices[(size_t) i]);
        addAndMakeVisible (modDestinationChoices[(size_t) i]);

        auto& amount = modAmountSliders[(size_t) i];
        amount.setSliderStyle (juce::Slider::LinearHorizontal);
        amount.setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 20);
        amount.setNumDecimalPlacesToDisplay (2);
        addAndMakeVisible (amount);

        modSourceAttachments[(size_t) i] = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "mod" + index + "Source", modSourceChoices[(size_t) i]);
        modDestinationAttachments[(size_t) i] = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "mod" + index + "Dest", modDestinationChoices[(size_t) i]);
        modAmountAttachments[(size_t) i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "mod" + index + "Amount", amount);
    }

    rebindFmOperatorEditor();
    configurePages();

    // Size changes call resized() synchronously, so do this only after all dynamic
    // components, attachments and tab pages have been created.
    setResizable (true, true);
    setResizeLimits (1080, 720, 2200, 1400);
    setSize (1400, 860);
    startTimerHz (20);
}

void RetroMatchSynthAudioProcessorEditor::configureSectionLabel (juce::Label& label, const juce::String& text, juce::Component& parent)
{
    label.setText (text, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centredLeft);
    label.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    label.setColour (juce::Label::textColourId, juce::Colour (0xffd5bd75));
    label.setColour (juce::Label::backgroundColourId, juce::Colour (0xff141b1d));
    parent.addAndMakeVisible (label);
}

void RetroMatchSynthAudioProcessorEditor::configurePages()
{
    addAndMakeVisible (tabs);
    tabs.setTabBarDepth (36);
    tabs.addTab ("SYNTH", juce::Colour (0xff172221), &synthPage, false);
    tabs.addTab ("FM", juce::Colour (0xff211c16), &fmPage, false);
    tabs.addTab ("FILTER + AMP", juce::Colour (0xff171e20), &filterAmpPage, false);
    tabs.addTab ("MOD", juce::Colour (0xff151d20), &modPage, false);
    tabs.addTab ("FX", juce::Colour (0xff191b20), &fxPage, false);
    tabs.addTab ("MATCH", juce::Colour (0xff17221f), &matchPage, false);
    tabs.setCurrentTabIndex (0);

    configureSectionLabel (synthOscSection, "OSCILLATORS + PITCH", synthPage);
    configureSectionLabel (synthTextureSection, "WAVETABLE + UNISON + HARMONICS", synthPage);
    configureSectionLabel (fmCoreSection, "FM / PHASE MOD CORE", fmPage);
    configureSectionLabel (fmOperatorsSection, "6-OP OVERVIEW", fmPage);
    configureSectionLabel (fmDetailSection, "SELECTED OPERATOR DETAIL", fmPage);
    configureSectionLabel (filterSection, "FILTER", filterAmpPage);
    configureSectionLabel (ampSection, "AMPLITUDE + OUTPUT", filterAmpPage);
    configureSectionLabel (modLfoSection, "LFO", modPage);
    configureSectionLabel (modMatrixSection, "MODULATION MATRIX", modPage);
    configureSectionLabel (fxChorusSection, "CHORUS", fxPage);
    configureSectionLabel (fxDelaySection, "DELAY", fxPage);
    configureSectionLabel (fxReverbSection, "REVERB + STEREO", fxPage);
    configureSectionLabel (matchCandidatesSection, "CANDIDATES + MORPH", matchPage);
    configureSectionLabel (matchLocksSection, "MATCH LOCKS", matchPage);

    matchHelp.setText ("Load a reference on the left, build A/B/C alternatives, then lock the parts you want Refine Match to preserve.", juce::dontSendNotification);
    matchHelp.setColour (juce::Label::textColourId, juce::Colour (0xff9fb5ae));
    matchHelp.setJustificationType (juce::Justification::centredLeft);
    matchHelp.setMinimumHorizontalScale (0.7f);
    matchPage.addAndMakeVisible (matchHelp);

    synthPage.addAndMakeVisible (osc1Label);
    synthPage.addAndMakeVisible (osc1Choice);
    synthPage.addAndMakeVisible (osc2Label);
    synthPage.addAndMakeVisible (osc2Choice);

    fmPage.addAndMakeVisible (fmAlgorithmLabel);
    fmPage.addAndMakeVisible (fmAlgorithmChoice);
    fmPage.addAndMakeVisible (fmOperatorEditLabel);
    fmPage.addAndMakeVisible (fmOperatorEditChoice);
    fmPage.addAndMakeVisible (fmModeLabel);
    fmPage.addAndMakeVisible (fmModeChoice);
    for (size_t i = 0; i < fmDetailSliders.size(); ++i)
    {
        fmPage.addAndMakeVisible (fmDetailLabels[i]);
        fmPage.addAndMakeVisible (fmDetailSliders[i]);
    }

    filterAmpPage.addAndMakeVisible (filterLabel);
    filterAmpPage.addAndMakeVisible (filterChoice);

    for (int i = 0; i < VoiceParameters::modSlotCount; ++i)
    {
        modPage.addAndMakeVisible (modSlotLabels[(size_t) i]);
        modPage.addAndMakeVisible (modSourceChoices[(size_t) i]);
        modPage.addAndMakeVisible (modDestinationChoices[(size_t) i]);
        modPage.addAndMakeVisible (modAmountSliders[(size_t) i]);
    }

    for (auto* b : { &makeCandidates, &candidateA, &candidateB, &candidateC }) matchPage.addAndMakeVisible (*b);
    matchPage.addAndMakeVisible (candidateMorphLabel);
    matchPage.addAndMakeVisible (candidateMorph);
    for (auto& button : matchLockButtons)
        if (button != nullptr) matchPage.addAndMakeVisible (*button);

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
}

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

RetroMatchSynthAudioProcessorEditor::~RetroMatchSynthAudioProcessorEditor()
{
    stopTimer();
    if (worker != nullptr)
    {
        worker->signalThreadShouldExit();
        worker->stopThread (10000);
    }
    setLookAndFeel (nullptr);
}

void RetroMatchSynthAudioProcessorEditor::addKnob (const juce::String& id, const juce::String& name, const juce::String& suffix)
{
    auto knob = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
    knob->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 20);
    knob->setTextValueSuffix (suffix);
    knob->setMouseDragSensitivity (180);

    int decimals = 2;
    if (suffix == " s") decimals = 3;
    else if (suffix == " ct" || suffix == " st" || suffix == " dB") decimals = 1;
    else if (suffix == " Hz") decimals = (id == "cutoff" ? 0 : 2);
    else if (suffix == " x") decimals = 2;
    knob->setNumDecimalPlacesToDisplay (decimals);
    addAndMakeVisible (*knob);

    auto label = std::make_unique<juce::Label>();
    label->setText (name, juce::dontSendNotification);
    label->setJustificationType (juce::Justification::centred);
    label->setColour (juce::Label::textColourId, juce::Colour (0xffb8c3c0));
    label->setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)));
    label->setMinimumHorizontalScale (0.72f);
    addAndMakeVisible (*label);

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

void RetroMatchSynthAudioProcessorEditor::layoutKnobGrid (const juce::StringArray& ids, juce::Rectangle<int> area, int maxColumns)
{
    if (ids.isEmpty() || area.isEmpty()) return;

    const int count = ids.size();
    const int suggestedColumns = juce::jmax (2, area.getWidth() / 122);
    const int columns = juce::jmax (1, juce::jmin (count, juce::jmin (maxColumns, suggestedColumns)));
    const int rows = (count + columns - 1) / columns;
    const int cellW = juce::jmax (1, area.getWidth() / columns);
    const int cellH = juce::jmax (1, area.getHeight() / rows);

    for (int i = 0; i < count; ++i)
    {
        const int index = findKnobIndex (ids[i]);
        if (! juce::isPositiveAndBelow (index, (int) knobs.size())) continue;

        const int row = i / columns;
        const int column = i % columns;
        auto cell = juce::Rectangle<int> (area.getX() + column * cellW,
                                          area.getY() + row * cellH,
                                          column == columns - 1 ? area.getRight() - (area.getX() + column * cellW) : cellW,
                                          cellH).reduced (4, 2);
        labels[(size_t) index]->setBounds (cell.removeFromTop (19));
        knobs[(size_t) index]->setBounds (cell.reduced (3, 0));
    }
}

void RetroMatchSynthAudioProcessorEditor::layoutFmDetailGrid (juce::Rectangle<int> area)
{
    if (area.isEmpty()) return;
    const int count = (int) fmDetailSliders.size();
    const int columns = area.getWidth() >= 650 ? count : 4;
    const int rows = (count + columns - 1) / columns;
    const int cellW = juce::jmax (1, area.getWidth() / columns);
    const int cellH = juce::jmax (1, area.getHeight() / rows);

    for (int i = 0; i < count; ++i)
    {
        const int row = i / columns;
        const int column = i % columns;
        auto cell = juce::Rectangle<int> (area.getX() + column * cellW,
                                          area.getY() + row * cellH,
                                          column == columns - 1 ? area.getRight() - (area.getX() + column * cellW) : cellW,
                                          cellH).reduced (4, 2);
        fmDetailLabels[(size_t) i].setBounds (cell.removeFromTop (18));
        fmDetailSliders[(size_t) i].setBounds (cell.reduced (3, 0));
    }
}

void RetroMatchSynthAudioProcessorEditor::layoutPages()
{
    {
        auto area = synthPage.getLocalBounds().reduced (14);
        synthOscSection.setBounds (area.removeFromTop (24));
        area.removeFromTop (6);

        auto selectors = area.removeFromTop (36);
        const int selectorW = selectors.getWidth() / 2;
        auto osc1Area = selectors.removeFromLeft (selectorW).reduced (2, 3);
        osc1Label.setBounds (osc1Area.removeFromLeft (86));
        osc1Choice.setBounds (osc1Area);
        auto osc2Area = selectors.reduced (2, 3);
        osc2Label.setBounds (osc2Area.removeFromLeft (86));
        osc2Choice.setBounds (osc2Area);
        area.removeFromTop (6);

        const int oscHeight = juce::jmin (240, juce::jmax (190, area.getHeight() / 2 - 16));
        layoutKnobGrid (synthOscKnobs, area.removeFromTop (oscHeight), 5);
        area.removeFromTop (8);
        synthTextureSection.setBounds (area.removeFromTop (24));
        area.removeFromTop (6);
        layoutKnobGrid (synthTextureKnobs, area, 5);
    }

    {
        auto area = fmPage.getLocalBounds().reduced (14);
        fmCoreSection.setBounds (area.removeFromTop (24));
        area.removeFromTop (6);

        auto algorithmRow = area.removeFromTop (34).reduced (2, 2);
        fmAlgorithmLabel.setBounds (algorithmRow.removeFromLeft (82));
        fmAlgorithmChoice.setBounds (algorithmRow.removeFromLeft (juce::jmin (260, algorithmRow.getWidth())));
        area.removeFromTop (6);
        layoutKnobGrid (fmCoreKnobs, area.removeFromTop (105), 4);
        area.removeFromTop (8);

        fmOperatorsSection.setBounds (area.removeFromTop (24));
        area.removeFromTop (6);
        layoutKnobGrid (fmOperatorKnobs, area.removeFromTop (170), 6);
        area.removeFromTop (8);

        fmDetailSection.setBounds (area.removeFromTop (24));
        area.removeFromTop (6);
        auto detailSelectors = area.removeFromTop (34);
        auto left = detailSelectors.removeFromLeft (juce::jmin (250, detailSelectors.getWidth() / 2)).reduced (2, 2);
        fmOperatorEditLabel.setBounds (left.removeFromLeft (88));
        fmOperatorEditChoice.setBounds (left);
        auto mode = detailSelectors.removeFromLeft (juce::jmin (250, detailSelectors.getWidth())).reduced (8, 2);
        fmModeLabel.setBounds (mode.removeFromLeft (78));
        fmModeChoice.setBounds (mode);
        area.removeFromTop (6);
        layoutFmDetailGrid (area);
    }

    {
        auto area = filterAmpPage.getLocalBounds().reduced (14);
        filterSection.setBounds (area.removeFromTop (24));
        area.removeFromTop (6);
        auto filterRow = area.removeFromTop (34).reduced (2, 2);
        filterLabel.setBounds (filterRow.removeFromLeft (92));
        filterChoice.setBounds (filterRow.removeFromLeft (juce::jmin (300, filterRow.getWidth())));
        area.removeFromTop (8);
        layoutKnobGrid (filterKnobs, area.removeFromTop (175), 2);
        area.removeFromTop (10);
        ampSection.setBounds (area.removeFromTop (24));
        area.removeFromTop (6);
        layoutKnobGrid (ampKnobs, area, 3);
    }

    {
        auto area = modPage.getLocalBounds().reduced (14);
        modLfoSection.setBounds (area.removeFromTop (24));
        area.removeFromTop (6);
        layoutKnobGrid (modLfoKnobs, area.removeFromTop (165), 4);
        area.removeFromTop (10);
        modMatrixSection.setBounds (area.removeFromTop (24));
        area.removeFromTop (6);

        const int rowH = juce::jmax (54, area.getHeight() / VoiceParameters::modSlotCount);
        for (int i = 0; i < VoiceParameters::modSlotCount; ++i)
        {
            auto row = juce::Rectangle<int> (area.getX(), area.getY() + i * rowH, area.getWidth(), rowH).reduced (4, 8);
            modSlotLabels[(size_t) i].setBounds (row.removeFromLeft (58));
            row.removeFromLeft (6);
            const int choiceW = juce::jlimit (110, 170, row.getWidth() / 4);
            modSourceChoices[(size_t) i].setBounds (row.removeFromLeft (choiceW));
            row.removeFromLeft (8);
            modDestinationChoices[(size_t) i].setBounds (row.removeFromLeft (choiceW + 20));
            row.removeFromLeft (10);
            modAmountSliders[(size_t) i].setBounds (row.withSizeKeepingCentre (row.getWidth(), juce::jmin (30, row.getHeight())));
        }
    }

    {
        auto area = fxPage.getLocalBounds().reduced (14);
        const int groupHeight = juce::jmax (130, (area.getHeight() - 76) / 3);

        fxChorusSection.setBounds (area.removeFromTop (24));
        area.removeFromTop (4);
        layoutKnobGrid (chorusKnobs, area.removeFromTop (groupHeight), 3);
        area.removeFromTop (8);

        fxDelaySection.setBounds (area.removeFromTop (24));
        area.removeFromTop (4);
        layoutKnobGrid (delayKnobs, area.removeFromTop (groupHeight), 3);
        area.removeFromTop (8);

        fxReverbSection.setBounds (area.removeFromTop (24));
        area.removeFromTop (4);
        layoutKnobGrid (reverbKnobs, area, 4);
    }

    {
        auto area = matchPage.getLocalBounds().reduced (16);
        matchHelp.setBounds (area.removeFromTop (38));
        area.removeFromTop (8);
        matchCandidatesSection.setBounds (area.removeFromTop (24));
        area.removeFromTop (10);

        auto candidateRow = area.removeFromTop (36);
        makeCandidates.setBounds (candidateRow.removeFromLeft (150));
        candidateRow.removeFromLeft (10);
        candidateA.setBounds (candidateRow.removeFromLeft (42));
        candidateRow.removeFromLeft (6);
        candidateB.setBounds (candidateRow.removeFromLeft (42));
        candidateRow.removeFromLeft (6);
        candidateC.setBounds (candidateRow.removeFromLeft (42));

        area.removeFromTop (10);
        auto morphRow = area.removeFromTop (34);
        candidateMorphLabel.setBounds (morphRow.removeFromLeft (145));
        candidateMorph.setBounds (morphRow.removeFromLeft (juce::jmin (430, morphRow.getWidth())));

        area.removeFromTop (22);
        matchLocksSection.setBounds (area.removeFromTop (24));
        area.removeFromTop (10);

        const int columns = juce::jlimit (2, 4, area.getWidth() / 165);
        const int rows = ((int) matchLockButtons.size() + columns - 1) / columns;
        const int cellW = area.getWidth() / columns;
        const int cellH = juce::jmin (52, juce::jmax (38, area.getHeight() / juce::jmax (1, rows)));
        for (size_t i = 0; i < matchLockButtons.size(); ++i)
        {
            if (matchLockButtons[i] == nullptr) continue;
            const int row = (int) i / columns;
            const int column = (int) i % columns;
            matchLockButtons[i]->setBounds (area.getX() + column * cellW + 4,
                                             area.getY() + row * cellH + 4,
                                             cellW - 8, cellH - 8);
        }
    }
}

void RetroMatchSynthAudioProcessorEditor::drawAnalyzer (juce::Graphics& g, juce::Rectangle<float> display)
{
    if (display.isEmpty()) return;

    g.setColour (juce::Colour (0xff061412));
    g.fillRoundedRectangle (display, 8.0f);
    g.setColour (juce::Colour (0xff315d53));
    g.drawRoundedRectangle (display, 8.0f, 1.5f);

    auto header = display.removeFromTop (32.0f).reduced (11.0f, 0.0f);
    g.setColour (juce::Colour (0xff67ceb4));
    g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    g.drawText ("REFERENCE ANALYZER", header.removeFromLeft (juce::jmax (120.0f, header.getWidth() - 80.0f)), juce::Justification::centredLeft);

    if (proc.lastMatch.similarity.total > 0.0f)
    {
        g.setColour (juce::Colour (0xffe0c476));
        g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        g.drawText (juce::String (proc.lastMatch.similarity.total * 100.0f, 1) + "%", header, juce::Justification::centredRight);
    }

    if (! proc.currentFeatures)
    {
        g.setColour (juce::Colour (0xff78958c));
        g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        g.drawText ("DROP WAV / AIFF / FLAC HERE", display.reduced (12.0f), juce::Justification::centred);
        return;
    }

    const auto& ref = *proc.currentFeatures;
    auto meta = display.removeFromTop (46.0f).reduced (10.0f, 2.0f);
    g.setColour (juce::Colour (0xffd6e5df));
    g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    g.drawFittedText (proc.loadedSampleName, meta.removeFromTop (20.0f).toNearestInt(), juce::Justification::centredLeft, 1);
    g.setColour (juce::Colour (0xff91aaa2));
    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    const auto stats = "F0 " + juce::String (ref.fundamentalHz, 1) + " Hz   |   CENT "
                     + juce::String (ref.spectralCentroidHz, 0) + " Hz   |   WIDTH " + juce::String (ref.stereoWidth, 2);
    g.drawFittedText (stats, meta.toNearestInt(), juce::Justification::centredLeft, 1);

    auto graph = display.reduced (10.0f, 7.0f);
    auto waveArea = graph.removeFromTop (graph.getHeight() * 0.55f);
    auto spectrumArea = graph.reduced (0.0f, 5.0f);

    g.setColour (juce::Colour (0xff15332d));
    g.drawHorizontalLine ((int) waveArea.getCentreY(), waveArea.getX(), waveArea.getRight());

    auto drawWave = [&] (const SoundFeatures& f, juce::Colour colour, float alpha)
    {
        juce::Path path;
        for (int i = 0; i < SoundFeatures::waveformPointCount; ++i)
        {
            const float x = juce::jmap ((float) i, 0.0f, (float) (SoundFeatures::waveformPointCount - 1), waveArea.getX(), waveArea.getRight());
            const float y = waveArea.getCentreY() - f.waveformPreview[(size_t) i] * waveArea.getHeight() * 0.43f;
            if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
        }
        g.setColour (colour.withAlpha (alpha));
        g.strokePath (path, juce::PathStrokeType (1.35f));
    };

    drawWave (ref, juce::Colour (0xff70dfbf), 0.95f);
    if (proc.currentCandidateFeatures) drawWave (*proc.currentCandidateFeatures, juce::Colour (0xffd9b36d), 0.85f);

    const float bandW = spectrumArea.getWidth() / SoundFeatures::spectralBandCount;
    for (int i = 0; i < SoundFeatures::spectralBandCount; ++i)
    {
        const float x = spectrumArea.getX() + i * bandW;
        const float rh = ref.spectralBands[(size_t) i] * spectrumArea.getHeight();
        g.setColour (juce::Colour (0xff3c8d7b).withAlpha (0.55f));
        g.fillRect (juce::Rectangle<float> (x, spectrumArea.getBottom() - rh, juce::jmax (1.0f, bandW - 1.0f), rh));
        if (proc.currentCandidateFeatures)
        {
            const float ch = proc.currentCandidateFeatures->spectralBands[(size_t) i] * spectrumArea.getHeight();
            g.setColour (juce::Colour (0xffc99b51).withAlpha (0.65f));
            g.fillRect (juce::Rectangle<float> (x + bandW * 0.28f, spectrumArea.getBottom() - ch, juce::jmax (1.0f, bandW * 0.44f), ch));
        }
    }
}

void RetroMatchSynthAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff080b0d));

    auto outer = getLocalBounds().toFloat().reduced (6.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff22292b), outer.getTopLeft(),
                                             juce::Colour (0xff101416), outer.getBottomLeft(), false));
    g.fillRoundedRectangle (outer, 12.0f);
    g.setColour (juce::Colour (0xff3b4548));
    g.drawRoundedRectangle (outer, 12.0f, 1.0f);

    g.setColour (juce::Colour (0xff30383b));
    g.drawHorizontalLine (68, 16.0f, (float) getWidth() - 16.0f);

    if (! sidebarBounds.isEmpty())
    {
        auto side = sidebarBounds.toFloat();
        g.setColour (juce::Colour (0xff101719));
        g.fillRoundedRectangle (side, 9.0f);
        g.setColour (juce::Colour (0xff293538));
        g.drawRoundedRectangle (side, 9.0f, 1.0f);
        drawAnalyzer (g, analyzerBounds.toFloat());
    }
}

void RetroMatchSynthAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced (14);
    auto header = bounds.removeFromTop (48);

    auto patchActions = header.removeFromRight (232);
    loadPatch.setBounds (patchActions.removeFromRight (112).reduced (2, 7));
    savePatch.setBounds (patchActions.removeFromRight (112).reduced (2, 7));
    title.setBounds (header);

    bounds.removeFromTop (10);
    const int sidebarWidth = juce::jlimit (320, 430, (int) std::round (bounds.getWidth() * 0.28));
    sidebarBounds = bounds.removeFromLeft (sidebarWidth);
    bounds.removeFromLeft (12);
    tabs.setBounds (bounds);

    auto side = sidebarBounds.reduced (12);
    const int analyzerHeight = juce::jlimit (240, 360, side.getHeight() - 210);
    analyzerBounds = side.removeFromTop (analyzerHeight);
    side.removeFromTop (10);

    auto row = side.removeFromTop (36);
    const int half = row.getWidth() / 2;
    load.setBounds (row.removeFromLeft (half).reduced (2, 1));
    match.setBounds (row.reduced (2, 1));
    side.removeFromTop (7);

    row = side.removeFromTop (36);
    refine.setBounds (row.removeFromLeft (half).reduced (2, 1));
    exportPreview.setBounds (row.reduced (2, 1));
    side.removeFromTop (8);

    progressBar.setBounds (side.removeFromTop (25).reduced (2, 1));
    side.removeFromTop (6);
    status.setBounds (side.removeFromTop (juce::jmax (30, side.getHeight())).reduced (2, 0));

    layoutPages();
}

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
        safe->status.setText (ok ? "Reference ready. Quick Match or Refine Match." : "Could not analyze this file.", juce::dontSendNotification);
        safe->repaint();
    });
}

void RetroMatchSynthAudioProcessorEditor::filesDropped (const juce::StringArray& files, int, int)
{
    if (files.isEmpty()) return;
    if (worker != nullptr && worker->isThreadRunning()) return;
    const auto file = juce::File (files[0]);
    if (proc.loadReferenceSample (file)) status.setText ("Reference ready. Quick Match or Refine Match.", juce::dontSendNotification);
    repaint();
}

void RetroMatchSynthAudioProcessorEditor::applyQuickMatch()
{
    const auto result = proc.fitReference();
    status.setText (result.confidence > 0.0f
                        ? "Quick Match: " + juce::String (result.similarity.total * 100.0f, 1) + "% similarity"
                        : "Load a reference first.",
                    juce::dontSendNotification);
    repaint();
}

void RetroMatchSynthAudioProcessorEditor::startRefine()
{
    if (! proc.currentFeatures)
    {
        status.setText ("Load a reference first.", juce::dontSendNotification);
        return;
    }
    if (worker != nullptr && worker->isThreadRunning()) return;

    matchProgress.store (0.0f);
    progressDisplay = 0.0;
    progressBar.setVisible (true);
    refine.setEnabled (false);
    match.setEnabled (false);
    load.setEnabled (false);
    loadPatch.setEnabled (false);
    for (auto& b : matchLockButtons) if (b != nullptr) b->setEnabled (false);
    status.setText ("Refining candidate population...", juce::dontSendNotification);
    worker = std::make_unique<MatchThread> (*this);
    worker->startThread();
}

void RetroMatchSynthAudioProcessorEditor::finishRefine (MatchResult result)
{
    proc.applyMatchResult (result);
    progressDisplay = 1.0;
    refine.setEnabled (true);
    match.setEnabled (true);
    load.setEnabled (true);
    loadPatch.setEnabled (true);
    for (auto& b : matchLockButtons) if (b != nullptr) b->setEnabled (true);
    progressBar.setVisible (false);
    status.setText ("Refine: " + juce::String (result.evaluatedCandidates) + " candidates | "
                    + juce::String (result.similarity.total * 100.0f, 1) + "% similarity",
                    juce::dontSendNotification);
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

void RetroMatchSynthAudioProcessorEditor::syncMatchLocks()
{
    proc.matchSettings.lockPitch = matchLockButtons[0]->getToggleState();
    proc.matchSettings.lockOscillators = matchLockButtons[1]->getToggleState();
    proc.matchSettings.lockFm = matchLockButtons[2]->getToggleState();
    proc.matchSettings.lockEnvelope = matchLockButtons[3]->getToggleState();
    proc.matchSettings.lockFilter = matchLockButtons[4]->getToggleState();
    proc.matchSettings.lockModulation = matchLockButtons[5]->getToggleState();
    proc.matchSettings.lockEffects = matchLockButtons[6]->getToggleState();
}

void RetroMatchSynthAudioProcessorEditor::timerCallback()
{
    progressDisplay = matchProgress.load();
    repaint();
}
