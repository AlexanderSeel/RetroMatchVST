#pragma once
#include <JuceHeader.h>
#include <array>
#include <memory>
#include <vector>
#include "ReferenceWavetable.h"
#include "MSEG.h"
#include "ModuleRack.h"
#include "TempoSync.h"

enum class ModSource : int
{
    none = 0,
    lfo1,
    velocity,
    keyTrack,
    randomNote,
    ampEnvelope,
    mseg1,
    lfo2, lfo3, lfo4
};

enum class ModDestination : int
{
    none = 0,
    pitch,
    cutoff,
    amplitude,
    pulseWidth,
    fmAmount,
    fmMix,
    wavetablePosition,
    wavefold
};

struct ModSlotParameters
{
    int source = (int) ModSource::none;
    int destination = (int) ModDestination::none;
    float amount = 0.0f;
};

struct VoiceParameters
{
    static constexpr int fmOperatorCount = 6;
    static constexpr int modSlotCount = 4;
    static constexpr int modGraphSlotCount = 4;

    float osc1Mix = 0.75f, osc2Mix = 0.35f, subMix = 0.0f, noiseMix = 0.0f, ringMix = 0.0f, additiveMix = 0.0f;
    int osc1Wave = 1, osc2Wave = 0;
    float masterTuneCents = 0.0f;
    float osc2Semitones = 0.0f, osc2Detune = 0.0f, pulseWidth = 0.5f;

    float wavetableMix = 0.0f, wavetablePosition = 0.25f, wavetableWarp = 0.0f;
    float referenceWavetableMix = 0.0f;
    std::shared_ptr<const ReferenceWavetableData> referenceWavetable;
    float userWavetableMix = 0.0f;
    std::shared_ptr<const ReferenceWavetableData> userWavetable;

    float supersawMix = 0.0f, unisonDetune = 18.0f, unisonSpread = 0.72f;
    float wavefold = 0.0f;
    float fmAmount = 0.0f, fmRatio = 2.0f;

    float fmMix = 0.0f, fmFeedback = 0.0f;
    int fmAlgorithm = 0;
    std::array<float, fmOperatorCount> fmOpRatio {{ 1.0f, 2.0f, 3.0f, 1.0f, 1.0f, 1.0f }};
    std::array<float, fmOperatorCount> fmOpLevel {{ 1.0f, 0.55f, 0.35f, 0.25f, 0.18f, 0.12f }};
    std::array<int, fmOperatorCount> fmOpFixedMode {{ 0, 0, 0, 0, 0, 0 }};
    std::array<float, fmOperatorCount> fmOpFixedHz {{ 440.0f, 880.0f, 1320.0f, 440.0f, 440.0f, 440.0f }};
    std::array<float, fmOperatorCount> fmOpAttack {{ 0.005f, 0.003f, 0.003f, 0.005f, 0.005f, 0.005f }};
    std::array<float, fmOperatorCount> fmOpDecay {{ 0.45f, 0.32f, 0.22f, 0.35f, 0.30f, 0.25f }};
    std::array<float, fmOperatorCount> fmOpSustain {{ 1.0f, 0.72f, 0.52f, 0.65f, 0.55f, 0.45f }};
    std::array<float, fmOperatorCount> fmOpRelease {{ 0.30f, 0.20f, 0.16f, 0.25f, 0.22f, 0.18f }};
    std::array<float, fmOperatorCount> fmOpKeyScale {{ 0.0f, 0.12f, 0.18f, 0.08f, 0.16f, 0.22f }};
    std::array<float, fmOperatorCount> fmOpVelocity {{ 0.35f, 0.55f, 0.65f, 0.45f, 0.50f, 0.55f }};

    float harmonicTilt = 1.35f, oddEvenBalance = 0.5f;
    float attack = 0.01f, decay = 0.25f, sustain = 0.75f, release = 0.35f;
    float cutoff = 12000.0f, resonance = 0.15f;
    int filterType = 0;
    float lfoRate = 1.5f, lfoPitch = 0.0f, lfoCutoff = 0.0f, lfoAmp = 0.0f;
    std::array<ModSlotParameters, modSlotCount> modSlots {};

    MsegParameters mseg;
    // Dedicated MSEG destination makes ENABLE MSEG immediately audible.
    // The graph slots below remain available for additional parallel routes.
    int msegTarget = (int) ModDestination::amplitude;
    float msegDepth = 1.0f;
    std::array<ModSlotParameters, modGraphSlotCount> modGraphSlots {};
    std::array<ModSlotParameters, 4> moduleModSlots {};
    std::array<float, 3> extraLfoRate {{ 0.5f, 2.0f, 5.0f }};
    std::array<int, 3> extraLfoShape {};
    std::array<FxModuleParameters, FxModuleParameters::slotCount> fxModules {};

    float drive = 0.0f;
    int distortionMode = 0;
    float distortionMix = 1.0f;
    float chorusMix = 0.0f, chorusRate = 0.35f, chorusDepth = 0.25f;
    float delayMix = 0.0f, delayTime = 0.28f, delayFeedback = 0.22f;
    float reverbMix = 0.0f, reverbSize = 0.45f, reverbDamping = 0.45f;
    float stereoWidth = 1.0f;
    float outputGainDb = -3.0f;

    // Clock metadata is global at render time. Stored layer snapshots retain
    // their free-running values; SynthEngine overwrites these metadata fields from
    // the main instance before rendering a layer so every instance follows the
    // same DAW/manual clock without audio-thread allocations.
    float tempoBpm = 120.0f;
    std::array<bool, 4> lfoTempoSync {{ false, false, false, false }};
    std::array<int, 4> lfoTempoDivision {{ 3, 3, 3, 3 }};
    bool chorusTempoSync = false;
    int chorusTempoDivision = 3;
    bool delayTempoSync = false;
    int delayTempoDivision = 3;
    bool msegTempoSync = false;
    int msegTempoDivision = 3;

    void resolveTempo() noexcept
    {
        const float bpm = TempoSync::clampBpm (tempoBpm);
        if (lfoTempoSync[0]) lfoRate = TempoSync::frequencyHz (lfoTempoDivision[0], bpm);
        for (int i = 0; i < 3; ++i)
            if (lfoTempoSync[(size_t) i + 1]) extraLfoRate[(size_t) i] = TempoSync::frequencyHz (lfoTempoDivision[(size_t) i + 1], bpm);
        if (chorusTempoSync) chorusRate = TempoSync::frequencyHz (chorusTempoDivision, bpm);
        if (delayTempoSync) delayTime = TempoSync::seconds (delayTempoDivision, bpm);

        if (msegTempoSync)
        {
            float freeDuration = 0.0f;
            for (const auto time : mseg.times) freeDuration += juce::jmax (0.001f, time);
            const float targetDuration = TempoSync::seconds (msegTempoDivision, bpm);
            const float scale = targetDuration / juce::jmax (0.001f, freeDuration);
            for (auto& time : mseg.times) time = juce::jmax (0.001f, time * scale);
        }
    }

    void inheritTempoFrom (const VoiceParameters& master) noexcept
    {
        tempoBpm = master.tempoBpm;
        lfoTempoSync = master.lfoTempoSync;
        lfoTempoDivision = master.lfoTempoDivision;
        chorusTempoSync = master.chorusTempoSync;
        chorusTempoDivision = master.chorusTempoDivision;
        delayTempoSync = master.delayTempoSync;
        delayTempoDivision = master.delayTempoDivision;
        msegTempoSync = master.msegTempoSync;
        msegTempoDivision = master.msegTempoDivision;
    }

    int oversamplingQuality = 0;

    static constexpr int extraLayerCount = 7;
    std::array<std::shared_ptr<const VoiceParameters>, extraLayerCount> layers {};
    std::array<float, extraLayerCount> layerGain {{ 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f }}, layerPan {}, layerTune {};
    std::array<int, extraLayerCount> layerOperation {};
    std::array<float, extraLayerCount> layerAmount {{ 1, 1, 1, 1, 1, 1, 1 }};
    float mainLayerGain = 1.0f;
};

class HybridVoice : public juce::SynthesiserVoice
{
public:
    HybridVoice();
    bool canPlaySound (juce::SynthesiserSound*) override { return true; }
    void prepare (double sampleRate, int samplesPerBlock, int channels);
    void setParameters (const VoiceParameters& p);
    void setRandomSeed (int64 seed) { random.setSeed (seed); }
    float getOversamplingLatencySamples (int quality) const noexcept;
    void startNote (int midiNoteNumber, float velocity, juce::SynthesiserSound*, int) override;
    void stopNote (float velocity, bool allowTailOff) override;
    void pitchWheelMoved (int newValue) override;
    void controllerMoved (int, int) override {}
    void renderNextBlock (juce::AudioBuffer<float>&, int startSample, int numSamples) override;

private:
    static constexpr int supersawVoiceCount = 7;
    static constexpr int wavetableSize = 2048;
    static constexpr int wavetableFrameCount = 5;

    VoiceParameters params;
    juce::ADSR ampEnv;
    juce::ADSR::Parameters envParams;
    std::array<juce::ADSR, VoiceParameters::fmOperatorCount> fmEnvelopes;
    std::array<juce::ADSR::Parameters, VoiceParameters::fmOperatorCount> fmEnvelopeParams;
    MultiSegmentEnvelope mseg;
    juce::dsp::StateVariableTPTFilter<float> filter;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling2x, oversampling4x;
    juce::AudioBuffer<float> renderScratch;
    std::vector<float> cutoffScratch, gainScratch, wavefoldScratch;
    juce::Random random;
    double sr = 44100.0, phase1 = 0.0, phase2 = 0.0, subPhase = 0.0, lfoPhase = 0.0;
    std::array<double, 3> extraLfoPhase {};
    std::array<float, 3> extraLfoValue {};
    std::array<double, VoiceParameters::fmOperatorCount> fmPhase {};
    std::array<double, supersawVoiceCount> unisonPhase {};
    float fmFeedbackState = 0.0f;
    float baseHz = 440.0f, level = 0.0f, pitchWheelSemitones = 0.0f, randomNoteValue = 0.0f;
    int currentMidiNote = 69;

    static float polyBlep (double phase, double phaseIncrement);
    static float wave (int type, double phase, double phaseIncrement, float pulseWidth);
    static float wavetableWave (double phase, float position, float warp);
    static float foldSample (float sample, float amount);
    float renderSixOperatorFm (float fundamentalHz);
    void renderSupersaw (float fundamentalHz, float detuneCents, float spread, float& left, float& right);
    float getModSourceValue (int source, float lfo, float envelopeValue, float msegValue) const;
};

class BasicSound : public juce::SynthesiserSound
{
public:
    bool appliesToNote (int) override { return true; }
    bool appliesToChannel (int) override { return true; }
};

class SynthEngine
{
public:
    SynthEngine();
    void prepare (double sampleRate, int samplesPerBlock, int channels, bool withLayers = true);
    void setParameters (const VoiceParameters&);
    void setRandomSeed (int64 baseSeed)
    {
        for (int i = 0; i < synth.getNumVoices(); ++i)
            if (auto* v = dynamic_cast<HybridVoice*> (synth.getVoice (i)))
                v->setRandomSeed (baseSeed + (int64) i * (int64) 0x1f123bb5);
        for (size_t i = 0; i < layerEngines.size(); ++i)
            if (layerEngines[i]) layerEngines[i]->setRandomSeed (baseSeed + (int64) (i + 1) * 7919);
    }
    void render (juce::AudioBuffer<float>&, juce::MidiBuffer&);
    void reset();
    int getLatencySamples() const noexcept { return fixedLatencySamples; }

    void noteOnFromUi (int midiNote, float velocity)
    {
        synth.noteOn (1, juce::jlimit (0, 127, midiNote), juce::jlimit (0.0f, 1.0f, velocity));
    }

    void noteOffFromUi (int midiNote, float velocity = 0.0f)
    {
        synth.noteOff (1, juce::jlimit (0, 127, midiNote), juce::jlimit (0.0f, 1.0f, velocity), true);
    }

    void allNotesOffFromUi() { synth.allNotesOff (0, true); }

private:
    juce::Synthesiser synth;
    std::array<std::unique_ptr<SynthEngine>, VoiceParameters::extraLayerCount> layerEngines;
    std::array<bool, VoiceParameters::extraLayerCount> layerActive {};
    juce::AudioBuffer<float> layerScratch;
    VoiceParameters current;
    ModuleRack moduleRack;
    double sampleRate = 44100.0;

    juce::dsp::Chorus<float> chorus;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delay { 5000000 };
    juce::Reverb reverb;
    std::unique_ptr<juce::dsp::Oversampling<float>> driveOversampling2x, driveOversampling4x;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> latencyCompensation { 512 };
    std::array<int, 3> intrinsicLatencySamples {{ 0, 0, 0 }};
    int fixedLatencySamples = 0;

    void processEffects (juce::AudioBuffer<float>& audio);
    void compensateLatency (juce::AudioBuffer<float>& audio);
};
