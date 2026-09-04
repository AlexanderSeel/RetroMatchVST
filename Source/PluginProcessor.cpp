#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

RetroMatchSynthAudioProcessor::RetroMatchSynthAudioProcessor()
 : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
   apvts (*this, nullptr, "STATE", createLayout())
{
}

void RetroMatchSynthAudioProcessor::prepareToPlay (double sr, int bs)
{
    const int channels = getTotalNumOutputChannels();
    engine.prepare (sr, bs, channels);
    referencePlayer.prepare (sr);
    referenceScratch.setSize (juce::jmax (1, channels), juce::jmax (1, bs), false, false, true);
}

bool RetroMatchSynthAudioProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

VoiceParameters RetroMatchSynthAudioProcessor::readParams() const
{
    VoiceParameters p;
    auto v = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    p.osc1Wave = (int) v ("osc1Wave");
    p.osc2Wave = (int) v ("osc2Wave");
    p.osc1Mix = v ("osc1Mix");
    p.osc2Mix = v ("osc2Mix");
    p.subMix = v ("subMix");
    p.noiseMix = v ("noise");
    p.ringMix = v ("ringMix");
    p.additiveMix = v ("additiveMix");
    p.masterTuneCents = v ("masterTune");
    p.osc2Semitones = v ("osc2Semi");
    p.osc2Detune = v ("osc2Detune");
    p.pulseWidth = v ("pulseWidth");
    p.wavetableMix = v ("wavetableMix");
    p.wavetablePosition = v ("wavetablePosition");
    p.wavetableWarp = v ("wavetableWarp");
    p.referenceWavetableMix = v ("referenceWavetableMix");
    p.referenceWavetable = referenceWavetable;
    p.supersawMix = v ("supersawMix");
    p.unisonDetune = v ("unisonDetune");
    p.unisonSpread = v ("unisonSpread");
    p.wavefold = v ("wavefold");
    p.fmAmount = v ("fmAmount");
    p.fmRatio = v ("fmRatio");
    p.fmMix = v ("fmMix");
    p.fmFeedback = v ("fmFeedback");
    p.fmAlgorithm = (int) v ("fmAlgorithm");
    for (int i = 0; i < VoiceParameters::fmOperatorCount; ++i)
    {
        const auto index = juce::String (i + 1);
        p.fmOpRatio[(size_t) i] = apvts.getRawParameterValue ("fmOp" + index + "Ratio")->load();
        p.fmOpLevel[(size_t) i] = apvts.getRawParameterValue ("fmOp" + index + "Level")->load();
        p.fmOpFixedMode[(size_t) i] = (int) apvts.getRawParameterValue ("fmOp" + index + "Mode")->load();
        p.fmOpFixedHz[(size_t) i] = apvts.getRawParameterValue ("fmOp" + index + "FixedHz")->load();
        p.fmOpAttack[(size_t) i] = apvts.getRawParameterValue ("fmOp" + index + "Attack")->load();
        p.fmOpDecay[(size_t) i] = apvts.getRawParameterValue ("fmOp" + index + "Decay")->load();
        p.fmOpSustain[(size_t) i] = apvts.getRawParameterValue ("fmOp" + index + "Sustain")->load();
        p.fmOpRelease[(size_t) i] = apvts.getRawParameterValue ("fmOp" + index + "Release")->load();
        p.fmOpKeyScale[(size_t) i] = apvts.getRawParameterValue ("fmOp" + index + "KeyScale")->load();
        p.fmOpVelocity[(size_t) i] = apvts.getRawParameterValue ("fmOp" + index + "Velocity")->load();
    }
    p.harmonicTilt = v ("harmonicTilt");
    p.oddEvenBalance = v ("oddEven");

    p.attack = v ("attack");
    p.decay = v ("decay");
    p.sustain = v ("sustain");
    p.release = v ("release");
    p.cutoff = v ("cutoff");
    p.resonance = v ("resonance");
    p.filterType = (int) v ("filterType");

    p.lfoRate = v ("lfoRate");
    p.lfoPitch = v ("lfoPitch");
    p.lfoCutoff = v ("lfoCutoff");
    p.lfoAmp = v ("lfoAmp");
    for (int i = 0; i < VoiceParameters::modSlotCount; ++i)
    {
        const auto index = juce::String (i + 1);
        p.modSlots[(size_t) i].source = (int) apvts.getRawParameterValue ("mod" + index + "Source")->load();
        p.modSlots[(size_t) i].destination = (int) apvts.getRawParameterValue ("mod" + index + "Dest")->load();
        p.modSlots[(size_t) i].amount = apvts.getRawParameterValue ("mod" + index + "Amount")->load();
    }

    p.drive = v ("drive");
    p.chorusMix = v ("chorusMix");
    p.chorusRate = v ("chorusRate");
    p.chorusDepth = v ("chorusDepth");
    p.delayMix = v ("delayMix");
    p.delayTime = v ("delayTime");
    p.delayFeedback = v ("delayFeedback");
    p.reverbMix = v ("reverbMix");
    p.reverbSize = v ("reverbSize");
    p.reverbDamping = v ("reverbDamping");
    p.stereoWidth = v ("stereoWidth");
    p.outputGainDb = v ("outputGain");
    return p;
}

void RetroMatchSynthAudioProcessor::processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer& m)
{
    juce::ScopedNoDenormals noDenormals;
    b.clear();

    const auto mode = getReferenceAuditionMode();
    if (mode != ReferenceAuditionMode::referenceOnly)
    {
        engine.setParameters (readParams());
        engine.render (b, m);
    }

    if (mode != ReferenceAuditionMode::synthOnly && referencePlayer.hasSample())
    {
        if (referenceScratch.getNumChannels() != b.getNumChannels() || referenceScratch.getNumSamples() < b.getNumSamples())
            referenceScratch.setSize (b.getNumChannels(), b.getNumSamples(), false, false, true);

        referenceScratch.clear();
        referencePlayer.render (referenceScratch, m, 0, b.getNumSamples());
        const float gain = referenceAuditionLevel.load();

        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            if (mode == ReferenceAuditionMode::referenceOnly)
            {
                b.copyFrom (ch, 0, referenceScratch, ch, 0, b.getNumSamples());
                b.applyGain (ch, 0, b.getNumSamples(), gain);
            }
            else
            {
                b.addFrom (ch, 0, referenceScratch, ch, 0, b.getNumSamples(), gain);
            }
        }
    }

    if (b.getNumSamples() > 0 && b.getNumChannels() > 0)
    {
        const float left = b.getMagnitude (0, 0, b.getNumSamples());
        const int rightChannel = juce::jmin (1, b.getNumChannels() - 1);
        const float right = b.getMagnitude (rightChannel, 0, b.getNumSamples());
        const float previousLeft = outputPeakLeft.load (std::memory_order_relaxed);
        const float previousRight = outputPeakRight.load (std::memory_order_relaxed);
        outputPeakLeft.store (juce::jmax (left, previousLeft * 0.88f), std::memory_order_relaxed);
        outputPeakRight.store (juce::jmax (right, previousRight * 0.88f), std::memory_order_relaxed);
    }
}

float RetroMatchSynthAudioProcessor::midiNoteToHz (int midiNote)
{
    const int note = juce::jlimit (0, 127, midiNote);
    return 440.0f * std::pow (2.0f, (note - 69) / 12.0f);
}

int RetroMatchSynthAudioProcessor::hzToNearestMidiNote (float hz)
{
    if (! std::isfinite (hz) || hz <= 0.0f) return 60;
    return juce::jlimit (0, 127, (int) std::lround (69.0 + 12.0 * std::log2 ((double) hz / 440.0)));
}

void RetroMatchSynthAudioProcessor::invalidateMatchesAfterReferencePitchChange()
{
    allEditorNotesOff();
    currentCandidateFeatures.reset();
    lastMatch = {};
    candidateBank = {};
    selectedCandidate = 0;
}

bool RetroMatchSynthAudioProcessor::loadReferenceSample (const juce::File& f)
{
    auto analysed = SampleAnalyzer::analyzeFile (f);
    if (! analysed) return false;

    detectedReferenceHz = analysed->fundamentalHz;
    detectedReferencePitchConfidence = analysed->pitchConfidence;
    detectedReferenceMidiNote = hzToNearestMidiNote (detectedReferenceHz);
    referenceBaseMidiNote.store (detectedReferenceMidiNote);
    loadedReferenceFile = f;

    currentFeatures = std::move (analysed);
    currentCandidateFeatures.reset();
    referenceWavetable = ReferenceWavetableExtractor::extract (f, currentFeatures->fundamentalHz);
    referencePlayer.load (f, detectedReferenceMidiNote);
    loadedSampleName = f.getFileName();
    lastMatch = {};
    candidateBank = {};
    selectedCandidate = 0;
    return true;
}

bool RetroMatchSynthAudioProcessor::setReferenceBaseMidiNote (int midiNote)
{
    if (! loadedReferenceFile.existsAsFile()) return false;

    const int note = juce::jlimit (0, 127, midiNote);
    const float expectedHz = midiNoteToHz (note);
    auto analysed = SampleAnalyzer::analyzeFile (loadedReferenceFile, expectedHz);
    if (! analysed) return false;

    currentFeatures = std::move (analysed);
    referenceBaseMidiNote.store (note);
    referencePlayer.setRootMidiNote (note);
    referenceWavetable = ReferenceWavetableExtractor::extract (loadedReferenceFile, expectedHz);
    invalidateMatchesAfterReferencePitchChange();
    return true;
}

bool RetroMatchSynthAudioProcessor::resetReferenceBaseMidiNote()
{
    return setReferenceBaseMidiNote (detectedReferenceMidiNote);
}

void RetroMatchSynthAudioProcessor::setReferenceAuditionMode (ReferenceAuditionMode mode)
{
    const int value = juce::jlimit ((int) ReferenceAuditionMode::synthOnly,
                                    (int) ReferenceAuditionMode::mixed,
                                    (int) mode);
    allEditorNotesOff();
    referenceAuditionMode.store (value);
}

void RetroMatchSynthAudioProcessor::noteOnFromEditor (int midiNote, float velocity)
{
    const auto mode = getReferenceAuditionMode();
    if (mode != ReferenceAuditionMode::referenceOnly)
        engine.noteOnFromUi (midiNote, velocity);
    if (mode != ReferenceAuditionMode::synthOnly)
        referencePlayer.noteOnFromUi (midiNote, velocity);
}

void RetroMatchSynthAudioProcessor::noteOffFromEditor (int midiNote, float velocity)
{
    engine.noteOffFromUi (midiNote, velocity);
    referencePlayer.noteOffFromUi (midiNote, velocity);
}

void RetroMatchSynthAudioProcessor::allEditorNotesOff()
{
    engine.allNotesOffFromUi();
    referencePlayer.allNotesOff();
}

void RetroMatchSynthAudioProcessor::applyMatchResult (const MatchResult& result)
{
    auto set = [this] (const juce::String& id, float x)
    {
        if (auto* param = apvts.getParameter (id))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->convertTo0to1 (x));
            param->endChangeGesture();
        }
    };

    const auto& q = result.params;
    set ("osc1Wave", (float) q.osc1Wave); set ("osc2Wave", (float) q.osc2Wave);
    set ("osc1Mix", q.osc1Mix); set ("osc2Mix", q.osc2Mix); set ("subMix", q.subMix);
    set ("noise", q.noiseMix); set ("ringMix", q.ringMix); set ("additiveMix", q.additiveMix); set ("masterTune", q.masterTuneCents);
    set ("osc2Semi", q.osc2Semitones); set ("osc2Detune", q.osc2Detune); set ("pulseWidth", q.pulseWidth);
    set ("wavetableMix", q.wavetableMix); set ("wavetablePosition", q.wavetablePosition); set ("wavetableWarp", q.wavetableWarp); set ("referenceWavetableMix", q.referenceWavetableMix);
    set ("supersawMix", q.supersawMix); set ("unisonDetune", q.unisonDetune); set ("unisonSpread", q.unisonSpread); set ("wavefold", q.wavefold);
    set ("fmAmount", q.fmAmount); set ("fmRatio", q.fmRatio); set ("fmMix", q.fmMix); set ("fmFeedback", q.fmFeedback); set ("fmAlgorithm", (float) q.fmAlgorithm);
    for (int i = 0; i < VoiceParameters::fmOperatorCount; ++i)
    {
        const auto index = juce::String (i + 1);
        set ("fmOp" + index + "Ratio", q.fmOpRatio[(size_t) i]);
        set ("fmOp" + index + "Level", q.fmOpLevel[(size_t) i]);
        set ("fmOp" + index + "Mode", (float) q.fmOpFixedMode[(size_t) i]);
        set ("fmOp" + index + "FixedHz", q.fmOpFixedHz[(size_t) i]);
        set ("fmOp" + index + "Attack", q.fmOpAttack[(size_t) i]);
        set ("fmOp" + index + "Decay", q.fmOpDecay[(size_t) i]);
        set ("fmOp" + index + "Sustain", q.fmOpSustain[(size_t) i]);
        set ("fmOp" + index + "Release", q.fmOpRelease[(size_t) i]);
        set ("fmOp" + index + "KeyScale", q.fmOpKeyScale[(size_t) i]);
        set ("fmOp" + index + "Velocity", q.fmOpVelocity[(size_t) i]);
    }
    set ("harmonicTilt", q.harmonicTilt); set ("oddEven", q.oddEvenBalance);

    set ("attack", q.attack); set ("decay", q.decay); set ("sustain", q.sustain); set ("release", q.release);
    set ("cutoff", q.cutoff); set ("resonance", q.resonance); set ("filterType", (float) q.filterType);
    set ("lfoRate", q.lfoRate); set ("lfoPitch", q.lfoPitch); set ("lfoCutoff", q.lfoCutoff); set ("lfoAmp", q.lfoAmp);
    for (int i = 0; i < VoiceParameters::modSlotCount; ++i)
    {
        const auto index = juce::String (i + 1);
        set ("mod" + index + "Source", (float) q.modSlots[(size_t) i].source);
        set ("mod" + index + "Dest", (float) q.modSlots[(size_t) i].destination);
        set ("mod" + index + "Amount", q.modSlots[(size_t) i].amount);
    }

    set ("drive", q.drive); set ("chorusMix", q.chorusMix); set ("chorusRate", q.chorusRate); set ("chorusDepth", q.chorusDepth);
    set ("delayMix", q.delayMix); set ("delayTime", q.delayTime); set ("delayFeedback", q.delayFeedback);
    set ("reverbMix", q.reverbMix); set ("reverbSize", q.reverbSize); set ("reverbDamping", q.reverbDamping);
    set ("stereoWidth", q.stereoWidth); set ("outputGain", q.outputGainDb);

    lastMatch = result;
    updateCandidatePreview (result);
}

void RetroMatchSynthAudioProcessor::updateCandidatePreview (const MatchResult& result)
{
    if (result.candidateFeatures.duration > 0.0f) currentCandidateFeatures = result.candidateFeatures;
}

MatchResult RetroMatchSynthAudioProcessor::fitReference()
{
    if (! currentFeatures) return {};
    auto seed = SoundMatcher::initialFit (*currentFeatures);
    seed.params.referenceWavetable = referenceWavetable;
    seed.params.referenceWavetableMix = referenceWavetable ? 0.32f : 0.0f;
    auto evaluated = SoundMatcher::evaluateFit (*currentFeatures, seed.params);
    evaluated.explanation = seed.explanation + " Initial rendered similarity: " + juce::String (evaluated.similarity.total * 100.0f, 1) + "%";
    applyMatchResult (evaluated);
    return evaluated;
}

MatchResult RetroMatchSynthAudioProcessor::refineReference (SoundMatcher::ProgressCallback progress, SoundMatcher::CancelCallback cancel)
{
    if (! currentFeatures) return {};
    const auto settings = matchSettings;
    const auto reference = *currentFeatures;
    auto seed = lastMatch.confidence > 0.0f ? readParams() : SoundMatcher::initialFit (reference).params;
    seed.referenceWavetable = referenceWavetable;
    if (referenceWavetable && seed.referenceWavetableMix <= 0.0f) seed.referenceWavetableMix = 0.22f;
    auto result = SoundMatcher::refineFit (reference, seed, settings, std::move (progress), std::move (cancel));
    return result;
}

juce::AudioProcessorValueTreeState::ParameterLayout RetroMatchSynthAudioProcessor::createLayout()
{
    using P = juce::AudioParameterFloat;
    using C = juce::AudioParameterChoice;
    juce::AudioProcessorValueTreeState::ParameterLayout l;

    l.add (std::make_unique<C> ("osc1Wave", "OSC 1 Wave", juce::StringArray { "Sine", "Saw", "Square", "Triangle", "Pulse" }, 1));
    l.add (std::make_unique<C> ("osc2Wave", "OSC 2 Wave", juce::StringArray { "Sine", "Saw", "Square", "Triangle", "Pulse" }, 0));
    l.add (std::make_unique<P> ("osc1Mix", "OSC 1 Mix", juce::NormalisableRange<float> (0, 1), 0.75f));
    l.add (std::make_unique<P> ("osc2Mix", "OSC 2 Mix", juce::NormalisableRange<float> (0, 1), 0.35f));
    l.add (std::make_unique<P> ("subMix", "Sub Mix", juce::NormalisableRange<float> (0, 0.65f), 0.0f));
    l.add (std::make_unique<P> ("noise", "Noise", juce::NormalisableRange<float> (0, 0.65f), 0.0f));
    l.add (std::make_unique<P> ("ringMix", "Ring Mod", juce::NormalisableRange<float> (0, 0.55f), 0.0f));
    l.add (std::make_unique<P> ("additiveMix", "Additive Mix", juce::NormalisableRange<float> (0, 0.85f), 0.0f));
    l.add (std::make_unique<P> ("masterTune", "Master Tune", juce::NormalisableRange<float> (-100, 100, 0.1f), 0.0f));
    l.add (std::make_unique<P> ("osc2Semi", "OSC 2 Semitones", juce::NormalisableRange<float> (-24, 24, 1), 0));
    l.add (std::make_unique<P> ("osc2Detune", "OSC 2 Detune", juce::NormalisableRange<float> (-50, 50, 0.1f), 0));
    l.add (std::make_unique<P> ("pulseWidth", "Pulse Width", juce::NormalisableRange<float> (0.08f, 0.92f), 0.5f));
    l.add (std::make_unique<P> ("wavetableMix", "Wavetable Mix", juce::NormalisableRange<float> (0, 1), 0.0f));
    l.add (std::make_unique<P> ("wavetablePosition", "Wavetable Position", juce::NormalisableRange<float> (0, 1), 0.25f));
    l.add (std::make_unique<P> ("wavetableWarp", "Wavetable Warp", juce::NormalisableRange<float> (-1, 1), 0.0f));
    l.add (std::make_unique<P> ("referenceWavetableMix", "Reference Wavetable Mix", juce::NormalisableRange<float> (0, 1), 0.0f));
    l.add (std::make_unique<P> ("supersawMix", "Supersaw Mix", juce::NormalisableRange<float> (0, 1), 0.0f));
    l.add (std::make_unique<P> ("unisonDetune", "Unison Detune", juce::NormalisableRange<float> (0, 70, 0.1f, 0.55f), 18.0f));
    l.add (std::make_unique<P> ("unisonSpread", "Unison Spread", juce::NormalisableRange<float> (0, 1), 0.72f));
    l.add (std::make_unique<P> ("wavefold", "Wavefold", juce::NormalisableRange<float> (0, 1), 0.0f));
    l.add (std::make_unique<P> ("fmAmount", "FM Amount", juce::NormalisableRange<float> (0, 0.65f), 0));
    l.add (std::make_unique<P> ("fmRatio", "FM Ratio", juce::NormalisableRange<float> (0.25f, 8, 0.01f, 0.45f), 2));
    l.add (std::make_unique<P> ("fmMix", "6-OP FM Mix", juce::NormalisableRange<float> (0, 1), 0));
    l.add (std::make_unique<P> ("fmFeedback", "FM Feedback", juce::NormalisableRange<float> (0, 1), 0));
    l.add (std::make_unique<C> ("fmAlgorithm", "FM Algorithm", juce::StringArray { "Stack", "Dual Stack", "Triple Pair", "Star", "Branch", "Six Carriers" }, 0));
    for (int i = 0; i < VoiceParameters::fmOperatorCount; ++i)
    {
        const auto index = juce::String (i + 1);
        l.add (std::make_unique<P> ("fmOp" + index + "Ratio", "FM OP" + index + " Ratio", juce::NormalisableRange<float> (0.125f, 16.0f, 0.001f, 0.42f), i == 0 ? 1.0f : (i == 1 ? 2.0f : (i == 2 ? 3.0f : 1.0f))));
        const float defaultLevel[] = { 1.0f, 0.55f, 0.35f, 0.25f, 0.18f, 0.12f };
        l.add (std::make_unique<P> ("fmOp" + index + "Level", "FM OP" + index + " Level", juce::NormalisableRange<float> (0, 1.25f), defaultLevel[i]));
        l.add (std::make_unique<C> ("fmOp" + index + "Mode", "FM OP" + index + " Frequency Mode", juce::StringArray { "Ratio", "Fixed" }, 0));
        const float defaultFixedHz[] = { 440.0f, 880.0f, 1320.0f, 440.0f, 440.0f, 440.0f };
        const float defaultDecay[] = { 0.45f, 0.32f, 0.22f, 0.35f, 0.30f, 0.25f };
        const float defaultSustain[] = { 1.0f, 0.72f, 0.52f, 0.65f, 0.55f, 0.45f };
        const float defaultRelease[] = { 0.30f, 0.20f, 0.16f, 0.25f, 0.22f, 0.18f };
        const float defaultKeyScale[] = { 0.0f, 0.12f, 0.18f, 0.08f, 0.16f, 0.22f };
        const float defaultVelocity[] = { 0.35f, 0.55f, 0.65f, 0.45f, 0.50f, 0.55f };
        l.add (std::make_unique<P> ("fmOp" + index + "FixedHz", "FM OP" + index + " Fixed Hz", juce::NormalisableRange<float> (10.0f, 16000.0f, 0.0f, 0.25f), defaultFixedHz[i]));
        l.add (std::make_unique<P> ("fmOp" + index + "Attack", "FM OP" + index + " Attack", juce::NormalisableRange<float> (0.001f, 5.0f, 0.0f, 0.3f), 0.005f));
        l.add (std::make_unique<P> ("fmOp" + index + "Decay", "FM OP" + index + " Decay", juce::NormalisableRange<float> (0.001f, 5.0f, 0.0f, 0.3f), defaultDecay[i]));
        l.add (std::make_unique<P> ("fmOp" + index + "Sustain", "FM OP" + index + " Sustain", juce::NormalisableRange<float> (0, 1), defaultSustain[i]));
        l.add (std::make_unique<P> ("fmOp" + index + "Release", "FM OP" + index + " Release", juce::NormalisableRange<float> (0.001f, 8.0f, 0.0f, 0.3f), defaultRelease[i]));
        l.add (std::make_unique<P> ("fmOp" + index + "KeyScale", "FM OP" + index + " Key Scale", juce::NormalisableRange<float> (0, 1), defaultKeyScale[i]));
        l.add (std::make_unique<P> ("fmOp" + index + "Velocity", "FM OP" + index + " Velocity", juce::NormalisableRange<float> (0, 1), defaultVelocity[i]));
    }
    l.add (std::make_unique<P> ("harmonicTilt", "Harmonic Tilt", juce::NormalisableRange<float> (0.45f, 3.5f, 0.01f, 0.6f), 1.35f));
    l.add (std::make_unique<P> ("oddEven", "Odd Even Balance", juce::NormalisableRange<float> (0, 1), 0.5f));

    l.add (std::make_unique<P> ("attack", "Attack", juce::NormalisableRange<float> (0.001f, 5, 0, 0.3f), 0.01f));
    l.add (std::make_unique<P> ("decay", "Decay", juce::NormalisableRange<float> (0.001f, 5, 0, 0.3f), 0.25f));
    l.add (std::make_unique<P> ("sustain", "Sustain", juce::NormalisableRange<float> (0, 1), 0.75f));
    l.add (std::make_unique<P> ("release", "Release", juce::NormalisableRange<float> (0.001f, 8, 0, 0.3f), 0.35f));
    l.add (std::make_unique<P> ("cutoff", "Cutoff", juce::NormalisableRange<float> (20, 20000, 0, 0.22f), 12000));
    l.add (std::make_unique<P> ("resonance", "Resonance", juce::NormalisableRange<float> (0.01f, 0.99f), 0.15f));
    l.add (std::make_unique<C> ("filterType", "Filter", juce::StringArray { "Low-pass", "High-pass", "Band-pass" }, 0));

    l.add (std::make_unique<P> ("lfoRate", "LFO Rate", juce::NormalisableRange<float> (0.02f, 30, 0, 0.3f), 1.5f));
    l.add (std::make_unique<P> ("lfoPitch", "LFO Pitch", juce::NormalisableRange<float> (0, 2), 0));
    l.add (std::make_unique<P> ("lfoCutoff", "LFO Cutoff", juce::NormalisableRange<float> (0, 3), 0));
    l.add (std::make_unique<P> ("lfoAmp", "LFO Amp", juce::NormalisableRange<float> (0, 1), 0));

    const juce::StringArray modSources { "Off", "LFO 1", "Velocity", "Key Track", "Random Note", "Amp Env" };
    const juce::StringArray modDestinations { "Off", "Pitch", "Cutoff", "Amplitude", "Pulse Width", "FM Amount", "6-OP FM Mix", "Wavetable Position", "Wavefold" };
    for (int i = 0; i < VoiceParameters::modSlotCount; ++i)
    {
        const auto index = juce::String (i + 1);
        l.add (std::make_unique<C> ("mod" + index + "Source", "Mod " + index + " Source", modSources, 0));
        l.add (std::make_unique<C> ("mod" + index + "Dest", "Mod " + index + " Destination", modDestinations, 0));
        l.add (std::make_unique<P> ("mod" + index + "Amount", "Mod " + index + " Amount", juce::NormalisableRange<float> (-1, 1), 0));
    }

    l.add (std::make_unique<P> ("drive", "Drive", juce::NormalisableRange<float> (0, 1), 0));
    l.add (std::make_unique<P> ("chorusMix", "Chorus Mix", juce::NormalisableRange<float> (0, 1), 0));
    l.add (std::make_unique<P> ("chorusRate", "Chorus Rate", juce::NormalisableRange<float> (0.02f, 10.0f, 0, 0.35f), 0.35f));
    l.add (std::make_unique<P> ("chorusDepth", "Chorus Depth", juce::NormalisableRange<float> (0, 1), 0.25f));
    l.add (std::make_unique<P> ("delayMix", "Delay Mix", juce::NormalisableRange<float> (0, 1), 0));
    l.add (std::make_unique<P> ("delayTime", "Delay Time", juce::NormalisableRange<float> (0.02f, 1.8f, 0, 0.35f), 0.28f));
    l.add (std::make_unique<P> ("delayFeedback", "Delay Feedback", juce::NormalisableRange<float> (0, 0.92f), 0.22f));
    l.add (std::make_unique<P> ("reverbMix", "Reverb Mix", juce::NormalisableRange<float> (0, 1), 0));
    l.add (std::make_unique<P> ("reverbSize", "Reverb Size", juce::NormalisableRange<float> (0, 1), 0.45f));
    l.add (std::make_unique<P> ("reverbDamping", "Reverb Damping", juce::NormalisableRange<float> (0, 1), 0.45f));
    l.add (std::make_unique<P> ("stereoWidth", "Stereo Width", juce::NormalisableRange<float> (0, 2), 1.0f));
    l.add (std::make_unique<P> ("outputGain", "Output Gain", juce::NormalisableRange<float> (-18, 6, 0.1f), -3.0f));
    return l;
}

bool RetroMatchSynthAudioProcessor::savePreset (const juce::File& file)
{
    auto xml = apvts.copyState().createXml();
    if (! xml) return false;
    xml->setAttribute ("presetVersion", "1.0");
    if (referenceWavetable && referenceWavetable->valid) xml->setAttribute ("referenceWavetable", referenceWavetable->toBase64());
    xml->setAttribute ("product", "RetroMatchSynth");
    return xml->writeTo (file, {});
}

bool RetroMatchSynthAudioProcessor::loadPreset (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (! xml || ! xml->hasTagName (apvts.state.getType())) return false;
    apvts.replaceState (juce::ValueTree::fromXml (*xml));
    if (xml->hasAttribute ("referenceWavetable")) referenceWavetable = ReferenceWavetableData::fromBase64 (xml->getStringAttribute ("referenceWavetable"));
    return true;
}

bool RetroMatchSynthAudioProcessor::exportPreviewWav (const juce::File& file, float seconds) const
{
    auto params = readParams(); params.referenceWavetable = referenceWavetable;
    const double sr = getSampleRate() > 1000.0 ? getSampleRate() : 44100.0;
    const float f0 = currentFeatures && currentFeatures->fundamentalHz > 20.0f ? currentFeatures->fundamentalHz : 261.6256f;
    auto audio = OfflineRenderer::renderPatch (params, sr, juce::jlimit (0.25f, 12.0f, seconds), f0, 256);
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    if (! stream) return false;

    juce::WavAudioFormat format;
    const auto options = juce::AudioFormatWriter::Options {}
                             .withSampleRate (sr)
                             .withNumChannels (audio.getNumChannels())
                             .withBitsPerSample (24);
    auto writer = format.createWriterFor (stream, options);
    if (! writer) return false;
    return writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
}

void RetroMatchSynthAudioProcessor::getStateInformation (juce::MemoryBlock& d)
{
    if (auto xml = apvts.copyState().createXml())
    {
        if (referenceWavetable && referenceWavetable->valid)
            xml->setAttribute ("referenceWavetable", referenceWavetable->toBase64());
        xml->setAttribute ("referenceAuditionMode", referenceAuditionMode.load());
        xml->setAttribute ("referenceAuditionLevel", (double) referenceAuditionLevel.load());
        copyXmlToBinary (*xml, d);
    }
}

void RetroMatchSynthAudioProcessor::setStateInformation (const void* d, int n)
{
    if (auto xml = getXmlFromBinary (d, n))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            if (xml->hasAttribute ("referenceWavetable"))
                referenceWavetable = ReferenceWavetableData::fromBase64 (xml->getStringAttribute ("referenceWavetable"));
            referenceAuditionMode.store (juce::jlimit (0, 2, xml->getIntAttribute ("referenceAuditionMode", 0)));
            referenceAuditionLevel.store (juce::jlimit (0.0f, 1.0f, (float) xml->getDoubleAttribute ("referenceAuditionLevel", 0.70)));
        }
    }
}

std::array<MatchResult, 3> RetroMatchSynthAudioProcessor::buildCandidateBank()
{
    if (! currentFeatures) return {};
    auto base = lastMatch.confidence > 0.0f ? lastMatch.params : readParams();
    base.referenceWavetable = referenceWavetable;
    std::array<VoiceParameters, 3> seeds { base, base, base };
    seeds[0].referenceWavetableMix = referenceWavetable ? juce::jmax (0.18f, base.referenceWavetableMix) : 0.0f;
    seeds[1].fmMix = juce::jmax (0.18f, base.fmMix); seeds[1].fmAlgorithm = (base.fmAlgorithm + 2) % 6; seeds[1].referenceWavetableMix *= 0.45f;
    seeds[2].supersawMix = juce::jmax (0.16f, base.supersawMix); seeds[2].wavetableMix = juce::jmax (0.20f, base.wavetableMix); seeds[2].referenceWavetableMix *= 0.70f;
    auto settings = matchSettings; settings.iterations = juce::jmax (36, settings.iterations / 2); settings.topologyTrials = juce::jmax (8, settings.topologyTrials / 2);
    for (int i = 0; i < 3; ++i) candidateBank[(size_t) i] = SoundMatcher::refineFit (*currentFeatures, seeds[(size_t) i], settings);
    selectedCandidate = 0; applyMatchResult (candidateBank[0]);
    return candidateBank;
}

bool RetroMatchSynthAudioProcessor::selectCandidate (int index)
{
    if (! juce::isPositiveAndBelow (index, 3) || candidateBank[(size_t) index].confidence <= 0.0f) return false;
    selectedCandidate = index; applyMatchResult (candidateBank[(size_t) index]); return true;
}

void RetroMatchSynthAudioProcessor::morphCandidates (int a, int b, float amount)
{
    if (! juce::isPositiveAndBelow (a, 3) || ! juce::isPositiveAndBelow (b, 3)) return;
    if (candidateBank[(size_t) a].confidence <= 0.0f || candidateBank[(size_t) b].confidence <= 0.0f) return;
    const auto& x = candidateBank[(size_t) a].params; const auto& y = candidateBank[(size_t) b].params;
    auto q = x; amount = juce::jlimit (0.0f, 1.0f, amount);
    auto mix = [amount] (float aa, float bb) { return juce::jmap (amount, aa, bb); };
    q.osc1Mix=mix(x.osc1Mix,y.osc1Mix); q.osc2Mix=mix(x.osc2Mix,y.osc2Mix); q.subMix=mix(x.subMix,y.subMix); q.noiseMix=mix(x.noiseMix,y.noiseMix);
    q.additiveMix=mix(x.additiveMix,y.additiveMix); q.wavetableMix=mix(x.wavetableMix,y.wavetableMix); q.referenceWavetableMix=mix(x.referenceWavetableMix,y.referenceWavetableMix);
    q.wavetablePosition=mix(x.wavetablePosition,y.wavetablePosition); q.supersawMix=mix(x.supersawMix,y.supersawMix); q.unisonDetune=mix(x.unisonDetune,y.unisonDetune); q.unisonSpread=mix(x.unisonSpread,y.unisonSpread);
    q.fmMix=mix(x.fmMix,y.fmMix); q.fmFeedback=mix(x.fmFeedback,y.fmFeedback); q.cutoff=mix(x.cutoff,y.cutoff); q.resonance=mix(x.resonance,y.resonance);
    q.attack=mix(x.attack,y.attack); q.decay=mix(x.decay,y.decay); q.sustain=mix(x.sustain,y.sustain); q.release=mix(x.release,y.release);
    q.drive=mix(x.drive,y.drive); q.chorusMix=mix(x.chorusMix,y.chorusMix); q.delayMix=mix(x.delayMix,y.delayMix); q.reverbMix=mix(x.reverbMix,y.reverbMix); q.stereoWidth=mix(x.stereoWidth,y.stereoWidth);
    q.osc1Wave = amount < 0.5f ? x.osc1Wave : y.osc1Wave; q.osc2Wave = amount < 0.5f ? x.osc2Wave : y.osc2Wave; q.fmAlgorithm = amount < 0.5f ? x.fmAlgorithm : y.fmAlgorithm; q.filterType = amount < 0.5f ? x.filterType : y.filterType;
    q.referenceWavetable = referenceWavetable;
    MatchResult r; r.params=q; if (currentFeatures) r=SoundMatcher::evaluateFit (*currentFeatures,q,matchSettings); applyMatchResult(r);
}

juce::AudioProcessorEditor* RetroMatchSynthAudioProcessor::createEditor()
{
    return new RetroMatchSynthAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RetroMatchSynthAudioProcessor();
}
