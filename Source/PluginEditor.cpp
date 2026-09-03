#include "PluginEditor.h"

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
    title.setFont (juce::Font (juce::FontOptions (25.0f, juce::Font::bold)));
    title.setColour (juce::Label::textColourId, juce::Colour (0xffe2c57f));
    addAndMakeVisible (title);

    status.setText ("Drop WAV / AIFF / FLAC into the analyzer or load a reference sample.", juce::dontSendNotification);
    status.setColour (juce::Label::textColourId, juce::Colour (0xff9fb9b0));
    addAndMakeVisible (status);

    load.onClick = [this] { chooseFile(); };
    match.onClick = [this] { applyQuickMatch(); };
    refine.onClick = [this] { startRefine(); };
    savePatch.onClick = [this] { chooseSavePatch(); };
    loadPatch.onClick = [this] { chooseLoadPatch(); };
    exportPreview.onClick = [this] { chooseExportPreview(); };
    for (auto* b : { &load, &match, &refine, &savePatch, &loadPatch, &exportPreview }) addAndMakeVisible (*b);
    makeCandidates.onClick = [this] { auto bank = proc.buildCandidateBank(); status.setText ("A/B/C built: " + juce::String (bank[0].confidence*100.0f,1) + "% / " + juce::String (bank[1].confidence*100.0f,1) + "% / " + juce::String (bank[2].confidence*100.0f,1) + "%", juce::dontSendNotification); repaint(); };
    candidateA.onClick = [this] { proc.selectCandidate (0); candidateMorph.setValue (0.0, juce::dontSendNotification); repaint(); };
    candidateB.onClick = [this] { proc.selectCandidate (1); candidateMorph.setValue (0.5, juce::dontSendNotification); repaint(); };
    candidateC.onClick = [this] { proc.selectCandidate (2); candidateMorph.setValue (1.0, juce::dontSendNotification); repaint(); };
    for (auto* b : { &makeCandidates, &candidateA, &candidateB, &candidateC }) addAndMakeVisible (*b);
    candidateMorph.setSliderStyle (juce::Slider::LinearHorizontal); candidateMorph.setRange (0.0, 1.0, 0.001); candidateMorph.setValue (0.0); candidateMorph.setTextBoxStyle (juce::Slider::TextBoxRight, false, 54, 20);
    candidateMorph.onValueChange = [this] { const float v=(float)candidateMorph.getValue(); if(v <= 0.5f) proc.morphCandidates(0,1,v*2.0f); else proc.morphCandidates(1,2,(v-0.5f)*2.0f); repaint(); };
    candidateMorphLabel.setText ("A  ◀  MORPH  ▶  C", juce::dontSendNotification); candidateMorphLabel.setColour (juce::Label::textColourId, juce::Colour (0xffc6a865));
    addAndMakeVisible(candidateMorph); addAndMakeVisible(candidateMorphLabel);

    const juce::String lockNames[] { "LOCK PITCH", "LOCK OSC", "LOCK FM", "LOCK ENV", "LOCK FILTER", "LOCK MOD", "LOCK FX" };
    for (size_t i = 0; i < matchLockButtons.size(); ++i)
    {
        matchLockButtons[i] = std::make_unique<juce::TextButton> (lockNames[i]);
        matchLockButtons[i]->setClickingTogglesState (true);
        matchLockButtons[i]->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff6b5130));
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
    fmAlgorithmLabel.setText ("6-OP FM ALGORITHM", juce::dontSendNotification);
    for (auto* l : { &osc1Label, &osc2Label, &filterLabel, &fmAlgorithmLabel })
    {
        l->setColour (juce::Label::textColourId, juce::Colour (0xffaeb9b8));
        l->setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (*l);
    }

    osc1Choice.addItemList ({ "Sine", "Saw", "Square", "Triangle", "Pulse" }, 1);
    osc2Choice.addItemList ({ "Sine", "Saw", "Square", "Triangle", "Pulse" }, 1);
    filterChoice.addItemList ({ "Low-pass", "High-pass", "Band-pass" }, 1);
    fmAlgorithmChoice.addItemList ({ "Stack", "Dual Stack", "Triple Pair", "Star", "Branch", "Six Carriers" }, 1);
    addAndMakeVisible (osc1Choice); addAndMakeVisible (osc2Choice); addAndMakeVisible (filterChoice); addAndMakeVisible (fmAlgorithmChoice);
    osc1Attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "osc1Wave", osc1Choice);
    osc2Attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "osc2Wave", osc2Choice);
    filterAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "filterType", filterChoice);
    fmAlgorithmAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "fmAlgorithm", fmAlgorithmChoice);

    fmOperatorEditLabel.setText ("FM OPERATOR EDIT", juce::dontSendNotification);
    fmModeLabel.setText ("FREQ MODE", juce::dontSendNotification);
    for (auto* l : { &fmOperatorEditLabel, &fmModeLabel })
    {
        l->setColour (juce::Label::textColourId, juce::Colour (0xffc6a865));
        l->setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
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
        addAndMakeVisible (slider);
    }

    const std::tuple<const char*, const char*, const char*> knobDefs[] = {
        { "osc1Mix", "OSC 1", "" }, { "osc2Mix", "OSC 2", "" }, { "subMix", "SUB", "" }, { "noise", "NOISE", "" },
        { "ringMix", "RING", "" }, { "additiveMix", "ADDITIVE", "" }, { "wavetableMix", "WAVETABLE", "" }, { "referenceWavetableMix", "REF WT", "" }, { "wavetablePosition", "WT POSITION", "" }, { "wavetableWarp", "WT WARP", "" },
        { "supersawMix", "SUPERSAW", "" }, { "unisonDetune", "UNI DETUNE", " ct" }, { "unisonSpread", "UNI SPREAD", "" }, { "wavefold", "WAVEFOLD", "" },
        { "masterTune", "TUNE", " ct" }, { "osc2Semi", "OSC2 SEMI", " st" }, { "osc2Detune", "OSC2 FINE", " ct" }, { "pulseWidth", "PULSE", "" },
        { "fmAmount", "PM AMOUNT", "" }, { "fmRatio", "PM RATIO", "" }, { "fmMix", "6-OP FM", "" }, { "fmFeedback", "FM FEEDBACK", "" }, { "harmonicTilt", "HARM TILT", "" }, { "oddEven", "ODD/EVEN", "" }, { "cutoff", "CUTOFF", " Hz" }, { "resonance", "RESO", "" },
        { "attack", "ATTACK", " s" }, { "decay", "DECAY", " s" }, { "sustain", "SUSTAIN", "" }, { "release", "RELEASE", " s" },
        { "lfoRate", "LFO RATE", " Hz" }, { "lfoPitch", "LFO→PITCH", " st" }, { "lfoCutoff", "LFO→FILTER", "" }, { "lfoAmp", "LFO→AMP", "" },
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
        modSlotLabels[(size_t) i].setColour (juce::Label::textColourId, juce::Colour (0xffc6a865));
        modSlotLabels[(size_t) i].setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
        addAndMakeVisible (modSlotLabels[(size_t) i]);

        modSourceChoices[(size_t) i].addItemList (modSources, 1);
        modDestinationChoices[(size_t) i].addItemList (modDestinations, 1);
        addAndMakeVisible (modSourceChoices[(size_t) i]);
        addAndMakeVisible (modDestinationChoices[(size_t) i]);

        auto& amount = modAmountSliders[(size_t) i];
        amount.setSliderStyle (juce::Slider::LinearHorizontal);
        amount.setTextBoxStyle (juce::Slider::TextBoxRight, false, 54, 20);
        addAndMakeVisible (amount);

        modSourceAttachments[(size_t) i] = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "mod" + index + "Source", modSourceChoices[(size_t) i]);
        modDestinationAttachments[(size_t) i] = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "mod" + index + "Dest", modDestinationChoices[(size_t) i]);
        modAmountAttachments[(size_t) i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "mod" + index + "Amount", amount);
    }

    rebindFmOperatorEditor();

    // setSize() invokes resized() immediately. Defer all size/resizable setup until
    // every lazily-created control exists, especially matchLockButtons.
    setResizable (true, true);
    setResizeLimits (1260, 1200, 2048, 1720);
    setSize (1500, 1240);
    startTimerHz (20);
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
    knob->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 18);
    knob->setTextValueSuffix (suffix);
    addAndMakeVisible (*knob);

    auto label = std::make_unique<juce::Label>();
    label->setText (name, juce::dontSendNotification);
    label->setJustificationType (juce::Justification::centred);
    label->setColour (juce::Label::textColourId, juce::Colour (0xffaeb7b8));
    label->setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    addAndMakeVisible (*label);

    attachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, *knob));
    knobs.push_back (std::move (knob));
    labels.push_back (std::move (label));
}

void RetroMatchSynthAudioProcessorEditor::drawPanel (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text)
{
    g.setColour (juce::Colour (0xff090c0e));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (juce::Colour (0xff364044));
    g.drawRoundedRectangle (r, 8.0f, 1.0f);
    g.setColour (juce::Colour (0xff8e9a97));
    g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
    g.drawText (text, r.removeFromTop (18).reduced (8, 0), juce::Justification::centredLeft);
}

void RetroMatchSynthAudioProcessorEditor::drawAnalyzer (juce::Graphics& g, juce::Rectangle<float> display)
{
    g.setColour (juce::Colour (0xff061412));
    g.fillRoundedRectangle (display, 7.0f);
    g.setColour (juce::Colour (0xff315d53));
    g.drawRoundedRectangle (display, 7.0f, 1.5f);

    auto header = display.removeFromTop (28).reduced (12, 0);
    g.setColour (juce::Colour (0xff63c8ae));
    g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
    g.drawText ("REFERENCE / RESYNTH ANALYZER", header, juce::Justification::centredLeft);

    if (! proc.currentFeatures)
    {
        g.setColour (juce::Colour (0xff6f8c83));
        g.drawText ("DROP REFERENCE AUDIO HERE", display, juce::Justification::centred);
        return;
    }

    const auto& ref = *proc.currentFeatures;
    auto stats = display.removeFromTop (28).reduced (12, 0);
    const juce::String statsText = juce::String::formatted (
        "%s   F0 %.1fHz [%.0f%%]   CENT %.0fHz   FLAT %.2f   INH %.2f   MOTION %.2f   ATT %.3fs   WIDTH %.2f",
        proc.loadedSampleName.toRawUTF8(), ref.fundamentalHz, ref.pitchConfidence * 100.0f,
        ref.spectralCentroidHz, ref.spectralFlatness, ref.inharmonicity, ref.spectralMotion, ref.attackSeconds, ref.stereoWidth);
    g.setColour (juce::Colour (0xffb7efdd));
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText (statsText, stats, juce::Justification::centredLeft, true);

    auto graph = display.reduced (12, 6);
    auto waveArea = graph.removeFromTop (graph.getHeight() * 0.52f);
    auto spectrumArea = graph.reduced (0, 4);

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

    if (proc.lastMatch.similarity.total > 0.0f)
    {
        auto scoreBox = juce::Rectangle<float> (display.getRight() - 122.0f, display.getY() - 55.0f, 110.0f, 42.0f);
        g.setColour (juce::Colour (0xff0a211c)); g.fillRoundedRectangle (scoreBox, 5.0f);
        g.setColour (juce::Colour (0xffe0c476));
        g.setFont (juce::Font (juce::FontOptions (17.0f, juce::Font::bold)));
        g.drawText (juce::String (proc.lastMatch.similarity.total * 100.0f, 1) + "%", scoreBox, juce::Justification::centred);
    }
}

void RetroMatchSynthAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff090b0d));
    auto body = getLocalBounds().toFloat().reduced (12.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a3032), body.getTopLeft(),
                                             juce::Colour (0xff15191b), body.getBottomLeft(), false));
    g.fillRoundedRectangle (body, 13.0f);
    g.setColour (juce::Colour (0xff495154));
    g.drawRoundedRectangle (body, 13.0f, 1.0f);

    drawAnalyzer (g, juce::Rectangle<float> (28.0f, 88.0f, getWidth() - 56.0f, 190.0f));

    auto modFrame = juce::Rectangle<float> (24.0f, 405.0f, getWidth() - 48.0f, 108.0f);
    drawPanel (g, modFrame, "MODULATION MATRIX // SOURCE → DESTINATION // BIPOLAR AMOUNT");
    auto fmDetailFrame = juce::Rectangle<float> (24.0f, 522.0f, getWidth() - 48.0f, 132.0f);
    drawPanel (g, fmDetailFrame, "FM OPERATOR DETAIL // PER-OP ENVELOPE // RATIO/FIXED // KEY & VELOCITY SCALING");
    auto controlFrame = juce::Rectangle<float> (24.0f, 663.0f, getWidth() - 48.0f, getHeight() - 685.0f);
    drawPanel (g, controlFrame, "HYBRID VOICE // WAVETABLE // SUPER/UNISON // WAVEFOLD // 6-OP FM // FILTER // FX");

    g.setColour (juce::Colour (0xffc6a865));
    g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
    g.drawText ("REFERENCE", getWidth() - 208, 105, 78, 15, juce::Justification::centredRight);
    g.setColour (juce::Colour (0xff72d8bd)); g.fillEllipse ((float) getWidth() - 122.0f, 110.0f, 6.0f, 6.0f);
    g.setColour (juce::Colour (0xffc6a865)); g.drawText ("SYNTH", getWidth() - 110, 105, 65, 15, juce::Justification::centredRight);
}

void RetroMatchSynthAudioProcessorEditor::resized()
{
    title.setBounds (30, 22, getWidth() - 60, 34);
    status.setBounds (30, 56, getWidth() - 60, 24);

    const int buttonY = 292;
    load.setBounds (34, buttonY, 130, 34);
    match.setBounds (172, buttonY, 130, 34);
    refine.setBounds (310, buttonY, 140, 34);
    savePatch.setBounds (466, buttonY, 120, 34);
    loadPatch.setBounds (594, buttonY, 120, 34);
    exportPreview.setBounds (722, buttonY, 118, 34);
    progressBar.setBounds (852, buttonY + 3, getWidth() - 886, 28);
    makeCandidates.setBounds (34, buttonY + 42, 120, 28); candidateA.setBounds (162, buttonY + 42, 34, 28); candidateB.setBounds (201, buttonY + 42, 34, 28); candidateC.setBounds (240, buttonY + 42, 34, 28);
    candidateMorphLabel.setBounds (286, buttonY + 42, 126, 28); candidateMorph.setBounds (414, buttonY + 42, 260, 28);

    const int lockY = 370;
    const int lockW = juce::jmax (92, juce::jmin (120, (getWidth() - 76) / (int) matchLockButtons.size()));
    for (size_t i = 0; i < matchLockButtons.size(); ++i)
        if (matchLockButtons[i] != nullptr)
            matchLockButtons[i]->setBounds (34 + (int) i * (lockW + 6), lockY, lockW, 25);

    const int choiceY = 408;
    const int choiceW = juce::jmax (200, (getWidth() - 100) / 4);
    osc1Label.setBounds (40, choiceY, 90, 22); osc1Choice.setBounds (132, choiceY, choiceW - 112, 24);
    osc2Label.setBounds (40 + choiceW, choiceY, 90, 22); osc2Choice.setBounds (132 + choiceW, choiceY, choiceW - 112, 24);
    filterLabel.setBounds (40 + choiceW * 2, choiceY, 90, 22); filterChoice.setBounds (132 + choiceW * 2, choiceY, choiceW - 112, 24);
    fmAlgorithmLabel.setBounds (40 + choiceW * 3, choiceY, 130, 22); fmAlgorithmChoice.setBounds (174 + choiceW * 3, choiceY, choiceW - 154, 24);

    const int modTop = 468;
    const int modColumnWidth = (getWidth() - 84) / VoiceParameters::modSlotCount;
    for (int i = 0; i < VoiceParameters::modSlotCount; ++i)
    {
        const int x = 38 + i * modColumnWidth;
        modSlotLabels[(size_t) i].setBounds (x, modTop, 46, 22);
        modSourceChoices[(size_t) i].setBounds (x + 50, modTop, juce::jmax (82, modColumnWidth / 3), 24);
        modDestinationChoices[(size_t) i].setBounds (x + 55 + juce::jmax (82, modColumnWidth / 3), modTop,
                                                      juce::jmax (92, modColumnWidth / 3), 24);
        modAmountSliders[(size_t) i].setBounds (x + 50, modTop + 31, modColumnWidth - 62, 24);
    }

    const int detailY = 584;
    fmOperatorEditLabel.setBounds (38, detailY, 108, 22);
    fmOperatorEditChoice.setBounds (146, detailY, 92, 24);
    fmModeLabel.setBounds (250, detailY, 72, 22);
    fmModeChoice.setBounds (322, detailY, 94, 24);
    const int detailStartX = 430;
    const int detailAvailableW = getWidth() - detailStartX - 42;
    const int detailCellW = detailAvailableW / (int) fmDetailSliders.size();
    for (size_t i = 0; i < fmDetailSliders.size(); ++i)
    {
        const int x = detailStartX + (int) i * detailCellW;
        fmDetailLabels[i].setBounds (x, detailY - 2, detailCellW, 18);
        fmDetailSliders[i].setBounds (x + 6, detailY + 15, detailCellW - 12, 76);
    }

    const int cols = 9;
    const int rows = juce::jmax (1, ((int) knobs.size() + cols - 1) / cols);
    const int left = 34;
    const int top = 726;
    const int availableW = getWidth() - 68;
    const int availableH = getHeight() - top - 28;
    const int cellW = availableW / cols;
    const int cellH = juce::jmax (64, availableH / rows);

    for (size_t i = 0; i < knobs.size(); ++i)
    {
        const int row = (int) i / cols;
        const int col = (int) i % cols;
        const int x = left + col * cellW;
        const int y = top + row * cellH;
        labels[i]->setBounds (x, y, cellW, 18);
        knobs[i]->setBounds (x + 8, y + 17, cellW - 16, cellH - 20);
    }

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
        safe->status.setText (ok ? "Reference analyzed. QUICK MATCH seeds the patch; REFINE MATCH runs the closed-loop optimizer."
                                 : "Could not analyze this file.", juce::dontSendNotification);
        safe->repaint();
    });
}

void RetroMatchSynthAudioProcessorEditor::filesDropped (const juce::StringArray& files, int, int)
{
    if (files.isEmpty()) return;
    if (worker != nullptr && worker->isThreadRunning()) return;
    const auto file = juce::File (files[0]);
    if (proc.loadReferenceSample (file))
        status.setText ("Reference analyzed. Ready to match.", juce::dontSendNotification);
    repaint();
}

void RetroMatchSynthAudioProcessorEditor::applyQuickMatch()
{
    const auto result = proc.fitReference();
    status.setText (result.confidence > 0.0f
                        ? "Quick match applied — rendered similarity " + juce::String (result.similarity.total * 100.0f, 1) + "%"
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
    for (auto& b : matchLockButtons) b->setEnabled (false);
    status.setText ("Refining: rendering and comparing candidate synth patches…", juce::dontSendNotification);
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
    for (auto& b : matchLockButtons) b->setEnabled (true);
    progressBar.setVisible (false);
    status.setText ("Refine complete — " + juce::String (result.evaluatedCandidates)
                    + " candidates, similarity " + juce::String (result.similarity.total * 100.0f, 1) + "%",
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
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles, [safe, chooser] (const juce::FileChooser& c)
    {
        if (safe == nullptr) return; auto file = c.getResult(); if (file.getFileExtension().isEmpty()) file = file.withFileExtension (".wav");
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