#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "UI/MsegPage.h"
#include "UI/UserWavetablePage.h"
#include "UI/MidiMappingPage.h"
#include "UI/LayersPage.h"
#include "UI/FxRackPage.h"
#include "UI/ModulatorsPage.h"
#include "Engine/PresetLibrary.h"
#include "UI/PresetsPage.h"
#include <cmath>

namespace
{
class OversamplingQualityEditor final : public RetroMatchSynthAudioProcessorEditor
{
public:
    explicit OversamplingQualityEditor (RetroMatchSynthAudioProcessor& processor)
        : RetroMatchSynthAudioProcessorEditor (processor), proc (processor)
    {
        qualityLabel.setText ("NONLINEAR OS", juce::dontSendNotification);
        qualityLabel.setColour (juce::Label::textColourId, getLookAndFeel().findColour (RetroLookAndFeel::secondaryLed));
        qualityLabel.setFont (juce::Font (juce::FontOptions (9.0f, juce::Font::bold)));
        qualityLabel.setJustificationType (juce::Justification::centredRight);

        qualityChoice.addItemList ({ "1x", "2x", "4x" }, 1);
        qualityChoice.setTooltip ("Oversampling quality for the per-voice wavefolder and global drive stage. Higher modes reduce aliasing at increased CPU cost.");
        qualityAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
            proc.apvts, "oversamplingQuality", qualityChoice);

        if (auto* tabbed = findTabbedComponent())
        {
            if (tabbed->getNumTabs() > 5)
            {
                settingsPage = tabbed->getTabContentComponent (5);
                if (settingsPage != nullptr)
                {
                    settingsPage->addAndMakeVisible (qualityLabel);
                    settingsPage->addAndMakeVisible (qualityChoice);
                }
            }

            // Keep the legacy MOD tab unchanged for automation/UI compatibility and
            // add post-1.0 editors as dedicated full-size pages.
            tabbed->addTab ("MSEG", juce::Colour (0xff10201d), new MsegPage (proc.apvts), true);
            tabbed->addTab ("WAVETABLE", juce::Colour (0xff101b20), new UserWavetablePage (proc), true);
            tabbed->addTab ("MIDI MAP", juce::Colour (0xff171b20), new MidiMappingPage (proc), true);
            tabbed->addTab ("LAYERS", juce::Colour (0xff101719), new LayersPage (proc), true);
            tabbed->addTab ("PRESETS", juce::Colour (0xff101719), new PresetsPage (proc), true);
            auto* builtInFx = tabbed->getTabContentComponent (4);
            tabbed->removeTab (4);
            tabbed->addTab ("FX", juce::Colour (0xff101719), new FxRackPage (proc, builtInFx), true, 4);
            auto* builtInMod = tabbed->getTabContentComponent (3);
            tabbed->removeTab (3);
            tabbed->addTab ("MOD", juce::Colour (0xff101719), new ModulatorsPage (proc, builtInMod), true, 3);
        }
        resized();
    }

    void resized() override
    {
        RetroMatchSynthAudioProcessorEditor::resized();
        if (settingsPage == nullptr) return;

        auto header = settingsPage->getLocalBounds().reduced (12).removeFromTop (24);
        auto qualityArea = header.removeFromRight (220);
        qualityLabel.setBounds (qualityArea.removeFromLeft (92));
        qualityChoice.setBounds (qualityArea.reduced (3, 1));
    }

private:
    juce::TabbedComponent* findTabbedComponent() const
    {
        for (int i = 0; i < getNumChildComponents(); ++i)
            if (auto* tabbed = dynamic_cast<juce::TabbedComponent*> (getChildComponent (i)))
                return tabbed;
        return nullptr;
    }

    RetroMatchSynthAudioProcessor& proc;
    juce::Component* settingsPage = nullptr;
    juce::Label qualityLabel;
    juce::ComboBox qualityChoice;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> qualityAttachment;
};

juce::ValueTree stateWithPost10Defaults (const juce::XmlElement& xml)
{
    auto state = juce::ValueTree::fromXml (xml);
    if (! state.isValid()) return state;

    auto setDefault = [&state] (const juce::String& id, const juce::var& value)
    {
        if (! state.getChildWithProperty ("id", id).isValid())
        {
            juce::ValueTree parameter ("PARAM");
            parameter.setProperty ("id", id, nullptr);
            parameter.setProperty ("value", state.getProperty (id, value), nullptr);
            state.appendChild (parameter, nullptr);
        }
    };

    setDefault ("oversamplingQuality", 0);
    setDefault ("msegEnabled", false);
    setDefault ("msegLoopEnabled", false);
    setDefault ("msegLoopStart", 1);
    setDefault ("msegLoopEnd", 2);
    setDefault ("userWavetableMix", 0.0f);
    setDefault ("distortionMode", 0); setDefault ("distortionMix", 1.0f); setDefault ("mainLayerGain", 1.0f);
    for (int i = 1; i <= VoiceParameters::extraLayerCount; ++i)
    {
        const auto prefix = "layer" + juce::String (i);
        setDefault (prefix + "Enabled", false); setDefault (prefix + "Gain", 0.5f);
        setDefault (prefix + "Pan", 0.0f); setDefault (prefix + "Tune", 0.0f);
    }

    const std::array<float, MsegParameters::pointCount> levels {{ 0.0f, 1.0f, 0.78f, 0.58f, 0.28f, 0.0f }};
    const std::array<float, MsegParameters::segmentCount> times {{ 0.025f, 0.090f, 0.180f, 0.320f, 0.420f }};
    const std::array<float, MsegParameters::segmentCount> curves {{ 0.15f, -0.10f, 0.0f, 0.10f, -0.15f }};
    for (int i = 0; i < MsegParameters::pointCount; ++i)
        setDefault ("msegLevel" + juce::String (i + 1), levels[(size_t) i]);
    for (int i = 0; i < MsegParameters::segmentCount; ++i)
    {
        setDefault ("msegTime" + juce::String (i + 1), times[(size_t) i]);
        setDefault ("msegCurve" + juce::String (i + 1), curves[(size_t) i]);
    }
    for (int i = 0; i < VoiceParameters::modGraphSlotCount; ++i)
    {
        const auto index = juce::String (i + 1);
        setDefault ("modGraph" + index + "Source", 0);
        setDefault ("modGraph" + index + "Dest", 0);
        setDefault ("modGraph" + index + "Amount", 0.0f);
    }
    for (int i = 1; i <= FxModuleParameters::slotCount; ++i)
    {
        const auto prefix = "fxModule" + juce::String (i);
        setDefault (prefix + "Type", 0); setDefault (prefix + "Stage", 0); setDefault (prefix + "Bypass", false);
        setDefault (prefix + "Amount", 0.5f); setDefault (prefix + "Rate", 0.25f); setDefault (prefix + "Feedback", 0.25f); setDefault (prefix + "Mix", 0.5f);
    }
    for (int i = 2; i <= 4; ++i)
    {
        const auto prefix = "lfoModule" + juce::String (i); setDefault (prefix + "Rate", i == 2 ? 0.5f : i == 3 ? 2.0f : 5.0f); setDefault (prefix + "Shape", 0);
    }
    for (int i = 1; i <= 4; ++i)
    {
        const auto prefix = "moduleMod" + juce::String (i); setDefault (prefix + "Source", 0); setDefault (prefix + "Dest", 0); setDefault (prefix + "Amount", 0.0f);
    }
    return state;
}
}

RetroMatchSynthAudioProcessor::RetroMatchSynthAudioProcessor()
 : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
   apvts (*this, nullptr, "STATE", createLayout())
{
}

void RetroMatchSynthAudioProcessor::prepareToPlay (double sr, int bs)
{
    const int channels = getTotalNumOutputChannels();
    engine.prepare (sr, bs, channels);
    renderMidi.ensureSize (131072);
    { const juce::ScopedLock lock (editorMidiLock); editorMidi.clear(); editorMidi.ensureSize (16384); }
    melodyTransport.stop();
    setLatencySamples (engine.getLatencySamples());

    referencePlayer.prepare (sr);
    referenceScratch.setSize (juce::jmax (1, channels), juce::jmax (1, bs), false, false, true);
    const juce::dsp::ProcessSpec spec { sr, (juce::uint32) juce::jmax (1, bs), (juce::uint32) juce::jmax (1, channels) };
    referenceLatencyDelay.setMaximumDelayInSamples (juce::jmax (1, engine.getLatencySamples() + 8));
    referenceLatencyDelay.prepare (spec);
    referenceLatencyDelay.setDelay ((float) engine.getLatencySamples());
    referenceLatencyDelay.reset();
}

bool RetroMatchSynthAudioProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

VoiceParameters RetroMatchSynthAudioProcessor::readParams (const juce::ValueTree& snapshot) const
{
    VoiceParameters p;
    auto v = [this, &snapshot] (const juce::String& id)
    {
        if (! snapshot.isValid()) return apvts.getRawParameterValue (id)->load();
        auto* parameter = apvts.getParameter (id);
        return (float) snapshot.getProperty (id, parameter->convertFrom0to1 (parameter->getDefaultValue()));
    };

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
    p.userWavetableMix = v ("userWavetableMix");
    p.userWavetable = userWavetable;
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
        p.fmOpRatio[(size_t) i] = v ("fmOp" + index + "Ratio");
        p.fmOpLevel[(size_t) i] = v ("fmOp" + index + "Level");
        p.fmOpFixedMode[(size_t) i] = (int) v ("fmOp" + index + "Mode");
        p.fmOpFixedHz[(size_t) i] = v ("fmOp" + index + "FixedHz");
        p.fmOpAttack[(size_t) i] = v ("fmOp" + index + "Attack");
        p.fmOpDecay[(size_t) i] = v ("fmOp" + index + "Decay");
        p.fmOpSustain[(size_t) i] = v ("fmOp" + index + "Sustain");
        p.fmOpRelease[(size_t) i] = v ("fmOp" + index + "Release");
        p.fmOpKeyScale[(size_t) i] = v ("fmOp" + index + "KeyScale");
        p.fmOpVelocity[(size_t) i] = v ("fmOp" + index + "Velocity");
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
        p.modSlots[(size_t) i].source = (int) v ("mod" + index + "Source");
        p.modSlots[(size_t) i].destination = (int) v ("mod" + index + "Dest");
        p.modSlots[(size_t) i].amount = v ("mod" + index + "Amount");
    }

    p.mseg.enabled = v ("msegEnabled") >= 0.5f;
    p.mseg.loopEnabled = v ("msegLoopEnabled") >= 0.5f;
    p.mseg.loopStartPoint = juce::jlimit (0, MsegParameters::pointCount - 2, (int) v ("msegLoopStart"));
    p.mseg.loopEndPoint = juce::jlimit (1, MsegParameters::pointCount - 1, (int) v ("msegLoopEnd") + 1);
    for (int i = 0; i < MsegParameters::pointCount; ++i)
        p.mseg.levels[(size_t) i] = v ("msegLevel" + juce::String (i + 1));
    for (int i = 0; i < MsegParameters::segmentCount; ++i)
    {
        p.mseg.times[(size_t) i] = v ("msegTime" + juce::String (i + 1));
        p.mseg.curves[(size_t) i] = v ("msegCurve" + juce::String (i + 1));
    }
    for (int i = 0; i < VoiceParameters::modGraphSlotCount; ++i)
    {
        const auto index = juce::String (i + 1);
        p.modGraphSlots[(size_t) i].source = (int) v ("modGraph" + index + "Source");
        p.modGraphSlots[(size_t) i].destination = (int) v ("modGraph" + index + "Dest");
        p.modGraphSlots[(size_t) i].amount = v ("modGraph" + index + "Amount");
    }

    for (int i = 0; i < FxModuleParameters::slotCount; ++i)
    {
        const auto prefix = "fxModule" + juce::String (i + 1); auto& module = p.fxModules[(size_t) i];
        module.type = (int) v (prefix + "Type"); module.stage = (int) v (prefix + "Stage"); module.bypass = v (prefix + "Bypass") >= 0.5f;
        module.amount = v (prefix + "Amount"); module.rate = v (prefix + "Rate"); module.feedback = v (prefix + "Feedback"); module.mix = v (prefix + "Mix");
    }
    for (int i = 0; i < 3; ++i)
    {
        const auto prefix = "lfoModule" + juce::String (i + 2);
        p.extraLfoRate[(size_t) i] = v (prefix + "Rate"); p.extraLfoShape[(size_t) i] = (int) v (prefix + "Shape");
    }
    for (int i = 0; i < 4; ++i)
    {
        const auto prefix = "moduleMod" + juce::String (i + 1); auto& slot = p.moduleModSlots[(size_t) i];
        slot.source = (int) v (prefix + "Source"); slot.destination = (int) v (prefix + "Dest"); slot.amount = v (prefix + "Amount");
    }
    p.drive = v ("drive");
    p.distortionMode = (int) v ("distortionMode");
    p.distortionMix = v ("distortionMix");
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
    p.oversamplingQuality = juce::jlimit (0, 2, (int) v ("oversamplingQuality"));
    if (snapshot.isValid())
    {
        p.referenceWavetable = ReferenceWavetableData::fromBase64 (snapshot["referenceTable"].toString());
        p.userWavetable = ReferenceWavetableData::fromBase64 (snapshot["userTable"].toString());
    }
    else
    {
        p.mainLayerGain = v ("mainLayerGain");
        for (int i = 0; i < VoiceParameters::extraLayerCount; ++i)
        {
            const auto prefix = "layer" + juce::String (i + 1);
            if (v (prefix + "Enabled") >= 0.5f) p.layers[(size_t) i] = savedLayers[(size_t) i].load();
            p.layerGain[(size_t) i] = v (prefix + "Gain");
            p.layerPan[(size_t) i] = v (prefix + "Pan");
            p.layerTune[(size_t) i] = v (prefix + "Tune");
        }
    }
    return p;
}

void RetroMatchSynthAudioProcessor::delayReferenceForLatency (juce::AudioBuffer<float>& buffer)
{
    const int latency = engine.getLatencySamples();
    if (latency <= 0) return;
    referenceLatencyDelay.setDelay ((float) latency);

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* samples = buffer.getWritePointer (ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            referenceLatencyDelay.pushSample (ch, samples[i]);
            samples[i] = referenceLatencyDelay.popSample (ch);
        }
    }
}

void RetroMatchSynthAudioProcessor::processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer& m)
{
    juce::ScopedNoDenormals noDenormals;
    b.clear();
    for (const auto metadata : m)
    {
        const auto message = metadata.getMessage();
        if (! message.isController()) continue;
        const int cc = message.getControllerNumber();
        const float value = message.getControllerValue() / 127.0f;
        const juce::ScopedLock lock (midiMappingLock);
        if (midiLearning.load())
        {
            midiMappings.erase (std::remove_if (midiMappings.begin(), midiMappings.end(), [this] (const auto& x) { return x.parameterId == midiLearnParameter; }), midiMappings.end());
            midiMappings.push_back ({ midiLearnParameter, cc }); midiLearning.store (false);
        }
        else
            for (const auto& mapping : midiMappings)
                if (mapping.cc == cc)
                    if (auto* parameter = apvts.getParameter (mapping.parameterId)) parameter->setValue (parameter->convertTo0to1 (value));
    }
    renderMidi.clear();
    renderMidi.addEvents (m, 0, b.getNumSamples(), 0);
    {
        const juce::ScopedTryLock lock (editorMidiLock);
        if (lock.isLocked()) { renderMidi.addEvents (editorMidi, 0, -1, 0); editorMidi.clear(); }
    }
    melodyTransport.process (renderMidi, b.getNumSamples(), getSampleRate());

    const auto mode = getReferenceAuditionMode();
    engine.setParameters (readParams());
    engine.render (b, renderMidi);
    if (mode == ReferenceAuditionMode::referenceOnly) b.clear();

    if (mode != ReferenceAuditionMode::synthOnly && referencePlayer.hasSample())
    {
        if (referenceScratch.getNumChannels() != b.getNumChannels() || referenceScratch.getNumSamples() < b.getNumSamples())
            referenceScratch.setSize (b.getNumChannels(), b.getNumSamples(), false, false, true);

        referenceScratch.clear();
        referencePlayer.render (referenceScratch, renderMidi, 0, b.getNumSamples());
        delayReferenceForLatency (referenceScratch);
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
    else if (mode == ReferenceAuditionMode::synthOnly)
    {
        referenceLatencyDelay.reset();
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
    visualAudio.push (b);
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

void RetroMatchSynthAudioProcessor::beginMidiLearn (const juce::String& parameterId)
{
    const juce::ScopedLock lock (midiMappingLock);
    midiLearnParameter = parameterId;
    midiLearning.store (parameterId.isNotEmpty());
}

void RetroMatchSynthAudioProcessor::removeMidiMapping (const juce::String& parameterId)
{
    const juce::ScopedLock lock (midiMappingLock);
    midiMappings.erase (std::remove_if (midiMappings.begin(), midiMappings.end(), [&parameterId] (const auto& x) { return x.parameterId == parameterId; }), midiMappings.end());
}

std::vector<RetroMatchSynthAudioProcessor::MidiMapping> RetroMatchSynthAudioProcessor::getMidiMappings() const
{
    const juce::ScopedLock lock (midiMappingLock);
    return midiMappings;
}

bool RetroMatchSynthAudioProcessor::loadReferenceSample (const juce::File& f)
{
    auto analysed = SampleAnalyzer::analyzeFile (f);
    if (! analysed) return false;
    setMelodyClip ({});

    detectedReferenceHz = analysed->fundamentalHz;
    detectedReferencePitchConfidence = analysed->pitchConfidence;
    detectedReferenceMidiNote = hzToNearestMidiNote (detectedReferenceHz);
    referenceBaseMidiNote.store (detectedReferenceMidiNote);
    loadedReferenceFile = f;
    analysisSourceDuration.store (analysed->duration);
    analysisStartSeconds.store (0.0f);
    analysisEndSeconds.store (analysed->duration);

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

bool RetroMatchSynthAudioProcessor::setReferenceAnalysisRegion (float startSeconds, float endSeconds)
{
    if (! loadedReferenceFile.existsAsFile()) return false;
    const float duration = analysisSourceDuration.load();
    const float start = juce::jlimit (0.0f, juce::jmax (0.0f, duration - 0.002f), startSeconds);
    const float end = juce::jlimit (start + 0.002f, juce::jmax (start + 0.002f, duration), endSeconds);
    auto analysed = SampleAnalyzer::analyzeFile (loadedReferenceFile, midiNoteToHz (referenceBaseMidiNote.load()), start, end);
    if (! analysed) return false;
    analysisStartSeconds.store (start);
    analysisEndSeconds.store (end);
    currentFeatures = std::move (analysed);
    referenceWavetable = ReferenceWavetableExtractor::extract (loadedReferenceFile, currentFeatures->fundamentalHz, start, end);
    invalidateMatchesAfterReferencePitchChange();
    return true;
}

bool RetroMatchSynthAudioProcessor::createUserWavetableFromReference (float start, float end, bool chop)
{
    if (! loadedReferenceFile.existsAsFile()) return false;
    auto table = chop ? ReferenceWavetableExtractor::chop (loadedReferenceFile, start, end)
                      : ReferenceWavetableExtractor::extract (loadedReferenceFile, midiNoteToHz (referenceBaseMidiNote.load()), start, end);
    if (! table || ! table->valid) return false;
    userWavetable = std::move (table);
    userWavetableName = loadedReferenceFile.getFileNameWithoutExtension() + " [" + juce::String (start, 3) + " - " + juce::String (end, 3) + " s]";
    userWavetableDescription = chop ? "5 sample slices / pitch-normalized frames / click a frame or scan WT POSITION" : "Sample selection / 5 frames / adjust USER WT MIX and WT POSITION";
    auto* mix = apvts.getParameter ("userWavetableMix");
    mix->setValueNotifyingHost (mix->convertTo0to1 (0.8f));
    return true;
}

bool RetroMatchSynthAudioProcessor::hasLayer (int index) const
{
    return juce::isPositiveAndBelow (index, VoiceParameters::extraLayerCount)
        && savedLayers[(size_t) index].load() != nullptr;
}

juce::String RetroMatchSynthAudioProcessor::getLayerName (int index) const
{
    return apvts.state.getChildWithName ("SYNTH_LAYERS").getChildWithProperty ("index", index)["name"].toString();
}

void RetroMatchSynthAudioProcessor::captureLayer (int index)
{
    if (! juce::isPositiveAndBelow (index, VoiceParameters::extraLayerCount)) return;
    juce::ValueTree snapshot ("LAYER");
    for (auto* parameter : getParameters())
        if (auto* identified = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
            snapshot.setProperty (identified->paramID, apvts.getRawParameterValue (identified->paramID)->load(), nullptr);
    snapshot.setProperty ("index", index, nullptr);
    snapshot.setProperty ("name", loadedSampleName.isEmpty() ? "Current patch" : loadedSampleName + " / " + juce::String (getAnalysisStartSeconds(), 2) + " s", nullptr);
    if (referenceWavetable) snapshot.setProperty ("referenceTable", referenceWavetable->toBase64(), nullptr);
    if (userWavetable) snapshot.setProperty ("userTable", userWavetable->toBase64(), nullptr);
    auto bank = apvts.state.getOrCreateChildWithName ("SYNTH_LAYERS", nullptr);
    auto previous = bank.getChildWithProperty ("index", index);
    if (previous.isValid()) bank.removeChild (previous, nullptr);
    bank.appendChild (snapshot, nullptr);
    std::shared_ptr<const VoiceParameters> parameters = std::make_shared<VoiceParameters> (readParams (snapshot));
    savedLayers[(size_t) index].store (parameters);
    apvts.getParameter ("layer" + juce::String (index + 1) + "Enabled")->setValueNotifyingHost (1.0f);
}

void RetroMatchSynthAudioProcessor::clearLayer (int index)
{
    if (! juce::isPositiveAndBelow (index, VoiceParameters::extraLayerCount)) return;
    auto bank = apvts.state.getChildWithName ("SYNTH_LAYERS");
    auto previous = bank.getChildWithProperty ("index", index);
    if (previous.isValid()) bank.removeChild (previous, nullptr);
    savedLayers[(size_t) index].store (std::shared_ptr<const VoiceParameters> {});
    apvts.getParameter ("layer" + juce::String (index + 1) + "Enabled")->setValueNotifyingHost (0.0f);
}

bool RetroMatchSynthAudioProcessor::loadLayerToMain (int index)
{
    if (! hasLayer (index)) return false;
    auto snapshot = apvts.state.getChildWithName ("SYNTH_LAYERS").getChildWithProperty ("index", index);
    for (auto* parameter : getParameters())
        if (auto* identified = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
            if (! identified->paramID.startsWith ("layer") && identified->paramID != "mainLayerGain" && snapshot.hasProperty (identified->paramID))
            {
                auto* ranged = apvts.getParameter (identified->paramID);
                ranged->setValueNotifyingHost (ranged->convertTo0to1 ((float) snapshot[identified->paramID]));
            }
    referenceWavetable = ReferenceWavetableData::fromBase64 (snapshot["referenceTable"].toString());
    userWavetable = ReferenceWavetableData::fromBase64 (snapshot["userTable"].toString());
    userWavetableName = userWavetable ? getLayerName (index) : juce::String {};
    userWavetableDescription = userWavetable ? "Stored layer wavetable" : juce::String {};
    return true;
}

void RetroMatchSynthAudioProcessor::restoreLayers()
{
    const auto bank = apvts.state.getChildWithName ("SYNTH_LAYERS");
    for (int i = 0; i < VoiceParameters::extraLayerCount; ++i)
    {
        const auto snapshot = bank.getChildWithProperty ("index", i);
        std::shared_ptr<const VoiceParameters> parameters;
        if (snapshot.isValid()) parameters = std::make_shared<VoiceParameters> (readParams (snapshot));
        savedLayers[(size_t) i].store (parameters);
    }
}

bool RetroMatchSynthAudioProcessor::loadUserWavetable (const juce::File& file, int sourceFrameSize)
{
    juce::String description;
    auto imported = ReferenceWavetableExtractor::importSet (file, sourceFrameSize, &description);
    if (imported == nullptr || ! imported->valid) return false;

    userWavetable = std::move (imported);
    userWavetableName = file.getFileName();
    userWavetableDescription = description;

    if (auto* parameter = apvts.getParameter ("userWavetableMix"))
    {
        const float current = parameter->convertFrom0to1 (parameter->getValue());
        if (current <= 0.0001f)
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (0.65f));
    }
    return true;
}

void RetroMatchSynthAudioProcessor::clearUserWavetable()
{
    userWavetable.reset();
    userWavetableName.clear();
    userWavetableDescription.clear();
    if (auto* parameter = apvts.getParameter ("userWavetableMix"))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (0.0f));
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
    melodyTransport.stop();
    const int value = juce::jlimit ((int) ReferenceAuditionMode::synthOnly,
                                    (int) ReferenceAuditionMode::mixed,
                                    (int) mode);
    allEditorNotesOff();
    referenceAuditionMode.store (value);
}

void RetroMatchSynthAudioProcessor::noteOnFromEditor (int midiNote, float velocity)
{
    const juce::ScopedLock lock (editorMidiLock);
    editorMidi.addEvent (juce::MidiMessage::noteOn (1, juce::jlimit (0, 127, midiNote), juce::jlimit (0.0f, 1.0f, velocity)), 0);
}

void RetroMatchSynthAudioProcessor::noteOffFromEditor (int midiNote, float velocity)
{
    const juce::ScopedLock lock (editorMidiLock);
    editorMidi.addEvent (juce::MidiMessage::noteOff (1, juce::jlimit (0, 127, midiNote), velocity), 0);
}

void RetroMatchSynthAudioProcessor::allEditorNotesOff()
{
    const juce::ScopedLock lock (editorMidiLock);
    editorMidi.clear();
    editorMidi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
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

    for (int i = 0; i < FxModuleParameters::slotCount; ++i)
    {
        const auto prefix = "fxModule" + juce::String (i + 1); const auto& module = q.fxModules[(size_t) i];
        set (prefix + "Type", (float) module.type); set (prefix + "Stage", (float) module.stage); set (prefix + "Bypass", module.bypass ? 1.0f : 0.0f);
        set (prefix + "Amount", module.amount); set (prefix + "Rate", module.rate); set (prefix + "Feedback", module.feedback); set (prefix + "Mix", module.mix);
    }
    for (int i = 0; i < 3; ++i)
    {
        const auto prefix = "lfoModule" + juce::String (i + 2); set (prefix + "Rate", q.extraLfoRate[(size_t) i]); set (prefix + "Shape", (float) q.extraLfoShape[(size_t) i]);
    }
    for (int i = 0; i < 4; ++i)
    {
        const auto prefix = "moduleMod" + juce::String (i + 1); const auto& slot = q.moduleModSlots[(size_t) i];
        set (prefix + "Source", (float) slot.source); set (prefix + "Dest", (float) slot.destination); set (prefix + "Amount", slot.amount);
    }
    set ("userWavetableMix", q.userWavetableMix);
    set ("msegEnabled", q.mseg.enabled ? 1.0f : 0.0f);
    set ("msegLoopEnabled", q.mseg.loopEnabled ? 1.0f : 0.0f);
    set ("msegLoopStart", (float) q.mseg.loopStartPoint); set ("msegLoopEnd", (float) q.mseg.loopEndPoint - 1);
    for (int i = 0; i < MsegParameters::pointCount; ++i) set ("msegLevel" + juce::String (i + 1), q.mseg.levels[(size_t) i]);
    for (int i = 0; i < MsegParameters::segmentCount; ++i)
    { set ("msegTime" + juce::String (i + 1), q.mseg.times[(size_t) i]); set ("msegCurve" + juce::String (i + 1), q.mseg.curves[(size_t) i]); }
    for (int i = 0; i < VoiceParameters::modGraphSlotCount; ++i)
    {
        const auto prefix = "modGraph" + juce::String (i + 1); const auto& slot = q.modGraphSlots[(size_t) i];
        set (prefix + "Source", (float) slot.source); set (prefix + "Dest", (float) slot.destination); set (prefix + "Amount", slot.amount);
    }
    set ("drive", q.drive); set ("chorusMix", q.chorusMix); set ("chorusRate", q.chorusRate); set ("chorusDepth", q.chorusDepth);
    set ("delayMix", q.delayMix); set ("delayTime", q.delayTime); set ("delayFeedback", q.delayFeedback);
    set ("reverbMix", q.reverbMix); set ("reverbSize", q.reverbSize); set ("reverbDamping", q.reverbDamping);
    set ("stereoWidth", q.stereoWidth); set ("outputGain", q.outputGainDb);

    // Render quality and stored synth instances remain user-authored.
    // The matcher now applies modulation and module routing from its winning patch.
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
    const auto authored = readParams();
    seed.params.referenceWavetable = referenceWavetable;
    seed.params.referenceWavetableMix = referenceWavetable && seed.params.osc1Wave != 0 ? 0.32f : 0.0f;
    seed.params.userWavetable = userWavetable;
    seed.params.userWavetableMix = authored.userWavetableMix;
    seed.params.distortionMode = authored.distortionMode; seed.params.distortionMix = authored.distortionMix;
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
    const auto authored = readParams();
    auto seed = lastMatch.confidence > 0.0f ? lastMatch.params : SoundMatcher::initialFit (reference).params;
    seed.referenceWavetable = referenceWavetable;
    seed.userWavetable = userWavetable;
    seed.userWavetableMix = authored.userWavetableMix;
    seed.layers.fill (nullptr); seed.mainLayerGain = 1.0f;
    seed.distortionMode = authored.distortionMode; seed.distortionMix = authored.distortionMix;
    return SoundMatcher::refineFit (reference, seed, settings, std::move (progress), std::move (cancel));
}

juce::AudioProcessorValueTreeState::ParameterLayout RetroMatchSynthAudioProcessor::createLayout()
{
    using P = juce::AudioParameterFloat;
    using C = juce::AudioParameterChoice;
    using B = juce::AudioParameterBool;
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
    const juce::StringArray actualModDestinations { "Off", "Pitch", "Cutoff", "Amplitude", "Pulse Width", "FM Amount", "6-OP FM Mix", "Wavetable Position", "Wavefold" };
    for (int i = 0; i < VoiceParameters::modSlotCount; ++i)
    {
        const auto index = juce::String (i + 1);
        l.add (std::make_unique<C> ("mod" + index + "Source", "Mod " + index + " Source", modSources, 0));
        l.add (std::make_unique<C> ("mod" + index + "Dest", "Mod " + index + " Destination", actualModDestinations, 0));
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

    // Existing post-1.0 quality parameter remains first after the frozen v1.0 surface.
    l.add (std::make_unique<C> ("oversamplingQuality", "Nonlinear Oversampling", juce::StringArray { "1x", "2x", "4x" }, 0));

    l.add (std::make_unique<B> ("msegEnabled", "MSEG 1 Enabled", false));
    l.add (std::make_unique<B> ("msegLoopEnabled", "MSEG 1 Loop Enabled", false));
    l.add (std::make_unique<C> ("msegLoopStart", "MSEG 1 Loop Start", juce::StringArray { "P1", "P2", "P3", "P4", "P5" }, 1));
    l.add (std::make_unique<C> ("msegLoopEnd", "MSEG 1 Loop End", juce::StringArray { "P2", "P3", "P4", "P5", "P6" }, 2));

    const float defaultMsegLevels[] = { 0.0f, 1.0f, 0.78f, 0.58f, 0.28f, 0.0f };
    for (int i = 0; i < MsegParameters::pointCount; ++i)
    {
        const auto index = juce::String (i + 1);
        l.add (std::make_unique<P> ("msegLevel" + index, "MSEG 1 Point " + index + " Level", juce::NormalisableRange<float> (0, 1), defaultMsegLevels[i]));
    }

    const float defaultMsegTimes[] = { 0.025f, 0.090f, 0.180f, 0.320f, 0.420f };
    const float defaultMsegCurves[] = { 0.15f, -0.10f, 0.0f, 0.10f, -0.15f };
    for (int i = 0; i < MsegParameters::segmentCount; ++i)
    {
        const auto index = juce::String (i + 1);
        l.add (std::make_unique<P> ("msegTime" + index, "MSEG 1 Segment " + index + " Time", juce::NormalisableRange<float> (0.001f, 12.0f, 0.0f, 0.30f), defaultMsegTimes[i]));
        l.add (std::make_unique<P> ("msegCurve" + index, "MSEG 1 Segment " + index + " Curve", juce::NormalisableRange<float> (-1, 1), defaultMsegCurves[i]));
    }

    const juce::StringArray graphSources { "Off", "LFO 1", "Velocity", "Key Track", "Random Note", "Amp Env", "MSEG 1" };
    const juce::StringArray graphDestinations { "Off", "Pitch", "Cutoff", "Amplitude", "Pulse Width", "FM Amount", "6-OP FM Mix", "Wavetable Position", "Wavefold" };
    for (int i = 0; i < VoiceParameters::modGraphSlotCount; ++i)
    {
        const auto index = juce::String (i + 1);
        l.add (std::make_unique<C> ("modGraph" + index + "Source", "Graph " + index + " Source", graphSources, 0));
        l.add (std::make_unique<C> ("modGraph" + index + "Dest", "Graph " + index + " Destination", graphDestinations, 0));
        l.add (std::make_unique<P> ("modGraph" + index + "Amount", "Graph " + index + " Amount", juce::NormalisableRange<float> (-1, 1), 0));
    }

    // User wavetable is appended after every previously released parameter.
    l.add (std::make_unique<P> ("userWavetableMix", "User Wavetable Mix", juce::NormalisableRange<float> (0, 1), 0.0f));
    l.add (std::make_unique<juce::AudioParameterChoice> ("distortionMode", "Distortion Mode", juce::StringArray { "Soft saturation", "Hard clip", "Sine fold" }, 0));
    l.add (std::make_unique<P> ("distortionMix", "Distortion Mix", juce::NormalisableRange<float> (0, 1), 1.0f));
    l.add (std::make_unique<P> ("mainLayerGain", "Main Layer Level", juce::NormalisableRange<float> (0, 1), 1.0f));
    for (int i = 1; i <= VoiceParameters::extraLayerCount; ++i)
    {
        const auto prefix = "layer" + juce::String (i);
        const auto name = "Layer " + juce::String (i + 1);
        l.add (std::make_unique<juce::AudioParameterBool> (prefix + "Enabled", name + " Enabled", false));
        l.add (std::make_unique<P> (prefix + "Gain", name + " Level", juce::NormalisableRange<float> (0, 1), 0.5f));
        l.add (std::make_unique<P> (prefix + "Pan", name + " Pan", juce::NormalisableRange<float> (-1, 1), 0.0f));
        l.add (std::make_unique<P> (prefix + "Tune", name + " Tune", juce::NormalisableRange<float> (-24, 24, 0.01f), 0.0f));
    }
    juce::StringArray moduleTypes;
    for (const auto& descriptor : fxModuleCatalog) moduleTypes.add (descriptor.name);
    for (int i = 1; i <= FxModuleParameters::slotCount; ++i)
    {
        const auto prefix = "fxModule" + juce::String (i);
        l.add (std::make_unique<C> (prefix + "Type", prefix + " Type", moduleTypes, 0));
        l.add (std::make_unique<C> (prefix + "Stage", prefix + " Stage", juce::StringArray { "PRE", "POST" }, 0));
        l.add (std::make_unique<juce::AudioParameterBool> (prefix + "Bypass", prefix + " Bypass", false));
        for (const auto* suffix : { "Amount", "Rate", "Feedback", "Mix" })
            l.add (std::make_unique<P> (prefix + suffix, prefix + " " + suffix, juce::NormalisableRange<float> (0, 1), juce::String (suffix) == "Rate" || juce::String (suffix) == "Feedback" ? 0.25f : 0.5f));
    }
    for (int i = 2; i <= 4; ++i)
    {
        const auto prefix = "lfoModule" + juce::String (i);
        l.add (std::make_unique<P> (prefix + "Rate", prefix + " Rate", juce::NormalisableRange<float> (0.01f, 30.0f, 0, 0.35f), i == 2 ? 0.5f : i == 3 ? 2.0f : 5.0f));
        l.add (std::make_unique<C> (prefix + "Shape", prefix + " Shape", juce::StringArray { "Sine", "Triangle", "Square", "Ramp" }, 0));
    }
    for (int i = 1; i <= 4; ++i)
    {
        const auto prefix = "moduleMod" + juce::String (i);
        l.add (std::make_unique<C> (prefix + "Source", prefix + " Source", juce::StringArray { "Off", "LFO 1", "Velocity", "Key Track", "Random Note", "Amp Env", "MSEG", "LFO 2", "LFO 3", "LFO 4" }, 0));
        l.add (std::make_unique<C> (prefix + "Dest", prefix + " Destination", actualModDestinations, 0));
        l.add (std::make_unique<P> (prefix + "Amount", prefix + " Amount", juce::NormalisableRange<float> (-1, 1), 0.0f));
    }
    return l;
}

void RetroMatchSynthAudioProcessor::applyPresetParameters (const VoiceParameters& parameters, const juce::String& name)
{
    melodyTransport.stop(); setReferenceAuditionMode (ReferenceAuditionMode::synthOnly);
    for (auto* parameter : getParameters())
        if (auto* identified = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
            if (identified->paramID != "oversamplingQuality") parameter->setValueNotifyingHost (parameter->getDefaultValue());
    for (int i = 0; i < VoiceParameters::extraLayerCount; ++i) clearLayer (i);
    for (int i = 0; i < VoiceParameters::extraLayerCount; ++i)
        if (parameters.layers[(size_t) i])
        {
            MatchResult layer; layer.params = *parameters.layers[(size_t) i]; applyMatchResult (layer); captureLayer (i);
            const auto prefix = "layer" + juce::String (i + 1);
            auto set = [this, &prefix] (const char* suffix, float value) { auto* p = apvts.getParameter (prefix + suffix); p->setValueNotifyingHost (p->convertTo0to1 (value)); };
            set ("Gain", parameters.layerGain[(size_t) i]); set ("Pan", parameters.layerPan[(size_t) i]); set ("Tune", parameters.layerTune[(size_t) i]);
        }
    MatchResult main; main.params = parameters; applyMatchResult (main);
    apvts.getParameter ("distortionMode")->setValueNotifyingHost (apvts.getParameter ("distortionMode")->convertTo0to1 ((float) parameters.distortionMode));
    apvts.getParameter ("distortionMix")->setValueNotifyingHost (parameters.distortionMix);
    apvts.state.setProperty ("patchName", name, nullptr);
    candidateBank = {}; currentCandidateFeatures.reset();
}

void RetroMatchSynthAudioProcessor::loadFactoryPreset (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) factoryPresetCatalog.size())) return;
    applyPresetParameters (makeFactoryPreset (index), factoryPresetCatalog[(size_t) index].name);
}

void RetroMatchSynthAudioProcessor::randomizePreset()
{
    auto& random = juce::Random::getSystemRandom(); const auto seed = random.nextInt64();
    const int family = random.nextInt ((int) factoryPresetCatalog.size());
    auto patch = SoundMatcher::makeVariation (makeFactoryPreset (family), seed, 0.10f + random.nextFloat() * 0.15f);
    patch.outputGainDb = -12; patch.noiseMix = juce::jmin (patch.noiseMix, 0.15f);
    applyPresetParameters (patch, "Random / " + juce::String (factoryPresetCatalog[(size_t) family].name) + " / " + juce::String::toHexString (seed).substring (0, 6));
}

bool RetroMatchSynthAudioProcessor::savePreset (const juce::File& file)
{
    auto xml = apvts.copyState().createXml();
    if (! xml) return false;
    xml->setAttribute ("presetVersion", "1.3");
    if (referenceWavetable && referenceWavetable->valid) xml->setAttribute ("referenceWavetable", referenceWavetable->toBase64());
    if (userWavetable && userWavetable->valid)
    {
        xml->setAttribute ("userWavetable", userWavetable->toBase64());
        xml->setAttribute ("userWavetableName", userWavetableName);
        xml->setAttribute ("userWavetableDescription", userWavetableDescription);
    }
    xml->setAttribute ("product", "RetroMatchSynth");
    xml->setAttribute ("analysisStartSeconds", (double) analysisStartSeconds.load());
    xml->setAttribute ("analysisEndSeconds", (double) analysisEndSeconds.load());
    const auto mappings = getMidiMappings(); xml->setAttribute ("midiMapCount", (int) mappings.size());
    for (int i = 0; i < (int) mappings.size(); ++i) { xml->setAttribute ("midiMap" + juce::String (i) + "Id", mappings[(size_t) i].parameterId); xml->setAttribute ("midiMap" + juce::String (i) + "CC", mappings[(size_t) i].cc); }
    return xml->writeTo (file, {});
}

bool RetroMatchSynthAudioProcessor::loadPreset (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (! xml || ! xml->hasTagName (apvts.state.getType())) return false;
    melodyTransport.stop();
    apvts.replaceState (stateWithPost10Defaults (*xml));
    restoreLayers();
    analysisStartSeconds.store ((float) xml->getDoubleAttribute ("analysisStartSeconds", 0.0));
    analysisEndSeconds.store ((float) xml->getDoubleAttribute ("analysisEndSeconds", -1.0));
    { const juce::ScopedLock lock (midiMappingLock); midiMappings.clear(); for (int i = 0; i < xml->getIntAttribute ("midiMapCount", 0); ++i) midiMappings.push_back ({ xml->getStringAttribute ("midiMap" + juce::String (i) + "Id"), xml->getIntAttribute ("midiMap" + juce::String (i) + "CC", 0) }); }

    referenceWavetable = xml->hasAttribute ("referenceWavetable")
        ? ReferenceWavetableData::fromBase64 (xml->getStringAttribute ("referenceWavetable")) : nullptr;
    userWavetable = xml->hasAttribute ("userWavetable")
        ? ReferenceWavetableData::fromBase64 (xml->getStringAttribute ("userWavetable")) : nullptr;
    userWavetableName = userWavetable ? xml->getStringAttribute ("userWavetableName", "Embedded wavetable") : juce::String {};
    userWavetableDescription = userWavetable ? xml->getStringAttribute ("userWavetableDescription", "Embedded 5 x 2048 table") : juce::String {};
    return true;
}

bool RetroMatchSynthAudioProcessor::exportPreviewWav (const juce::File& file, float seconds) const
{
    auto params = readParams();
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
        xml->setAttribute ("lightPalette", lightPalette.load());
        if (referenceWavetable && referenceWavetable->valid)
            xml->setAttribute ("referenceWavetable", referenceWavetable->toBase64());
        if (userWavetable && userWavetable->valid)
        {
            xml->setAttribute ("userWavetable", userWavetable->toBase64());
            xml->setAttribute ("userWavetableName", userWavetableName);
            xml->setAttribute ("userWavetableDescription", userWavetableDescription);
        }
        xml->setAttribute ("referenceAuditionMode", referenceAuditionMode.load());
        xml->setAttribute ("referenceAuditionLevel", (double) referenceAuditionLevel.load());
        xml->setAttribute ("analysisStartSeconds", (double) analysisStartSeconds.load());
        xml->setAttribute ("analysisEndSeconds", (double) analysisEndSeconds.load());
        const auto mappings = getMidiMappings(); xml->setAttribute ("midiMapCount", (int) mappings.size());
        for (int i = 0; i < (int) mappings.size(); ++i) { xml->setAttribute ("midiMap" + juce::String (i) + "Id", mappings[(size_t) i].parameterId); xml->setAttribute ("midiMap" + juce::String (i) + "CC", mappings[(size_t) i].cc); }
        copyXmlToBinary (*xml, d);
    }
}

void RetroMatchSynthAudioProcessor::setStateInformation (const void* d, int n)
{
    if (auto xml = getXmlFromBinary (d, n))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            melodyTransport.stop();
            lightPalette.store (juce::jlimit (0, 3, xml->getIntAttribute ("lightPalette", 0)));
            apvts.replaceState (stateWithPost10Defaults (*xml));
            restoreLayers();
            analysisStartSeconds.store ((float) xml->getDoubleAttribute ("analysisStartSeconds", 0.0));
            analysisEndSeconds.store ((float) xml->getDoubleAttribute ("analysisEndSeconds", -1.0));
            { const juce::ScopedLock lock (midiMappingLock); midiMappings.clear(); for (int i = 0; i < xml->getIntAttribute ("midiMapCount", 0); ++i) midiMappings.push_back ({ xml->getStringAttribute ("midiMap" + juce::String (i) + "Id"), xml->getIntAttribute ("midiMap" + juce::String (i) + "CC", 0) }); }
            referenceWavetable = xml->hasAttribute ("referenceWavetable")
                ? ReferenceWavetableData::fromBase64 (xml->getStringAttribute ("referenceWavetable")) : nullptr;
            userWavetable = xml->hasAttribute ("userWavetable")
                ? ReferenceWavetableData::fromBase64 (xml->getStringAttribute ("userWavetable")) : nullptr;
            userWavetableName = userWavetable ? xml->getStringAttribute ("userWavetableName", "Embedded wavetable") : juce::String {};
            userWavetableDescription = userWavetable ? xml->getStringAttribute ("userWavetableDescription", "Embedded 5 x 2048 table") : juce::String {};
            referenceAuditionMode.store (juce::jlimit (0, 2, xml->getIntAttribute ("referenceAuditionMode", 0)));
            referenceAuditionLevel.store (juce::jlimit (0.0f, 1.0f, (float) xml->getDoubleAttribute ("referenceAuditionLevel", 0.70)));
        }
    }
}

std::array<MatchResult, 3> RetroMatchSynthAudioProcessor::buildCandidateBank()
{
    if (! currentFeatures) return {};
    const auto authored = readParams();
    auto base = lastMatch.confidence > 0.0f ? lastMatch.params : authored;
    base.referenceWavetable = referenceWavetable;
    base.userWavetable = userWavetable;
    base.userWavetableMix = authored.userWavetableMix;
    base.layers.fill (nullptr); base.mainLayerGain = 1.0f;
    base.distortionMode = authored.distortionMode; base.distortionMix = authored.distortionMix;
    std::array<VoiceParameters, 3> seeds { base, base, base };
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
    const auto authored = readParams();
    q.referenceWavetable = referenceWavetable;
    q.userWavetable = userWavetable;
    q.userWavetableMix = authored.userWavetableMix;
    q.distortionMode = authored.distortionMode; q.distortionMix = authored.distortionMix;
    MatchResult r; r.params=q; if (currentFeatures) r=SoundMatcher::evaluateFit (*currentFeatures,q,matchSettings); applyMatchResult(r);
}

juce::AudioProcessorEditor* RetroMatchSynthAudioProcessor::createEditor()
{
    return new OversamplingQualityEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RetroMatchSynthAudioProcessor();
}
