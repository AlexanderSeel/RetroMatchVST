#pragma once
#include <JuceHeader.h>
#include <array>
#include <memory>
#include <vector>
#include "ReferenceWavetable.h"
#include "MSEG.h"
#include "ModuleRack.h"

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

    // Morphing wavetable bank and dense unison/supersaw layer.
    float wavetableMix = 0.0f, wavetablePosition = 0.25f, wavetableWarp = 0.0f;
    float referenceWavetableMix = 0.0f;
    std::shared_ptr<const ReferenceWavetableData> referenceWavetable;

    // User-imported wavetable sets are intentionally independent from the
    // reference-derived table so loading a sound-design table never destroys the
    // matching reference representation.
    float userWavetableMix = 0.0f;
    std::shared_ptr<const ReferenceWavetableData> userWavetable;

    float supersawMix = 0.0f, unisonDetune = 18.0f, unisonSpread = 0.72f;
    float wavefold = 0.0f;

    // Legacy two-oscillator phase modulation.
    float fmAmount = 0.0f, fmRatio = 2.0f;

    // Six-operator FM block.
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

    // The original four-slot matrix is frozen for v1.0 automation compatibility.
    std::array<ModSlotParameters, modSlotCount> modSlots {};

    // Post-1.0 modulation layer. New source/destination choices live here so the
    // normalized values of the original mod slot choices never change.
    MsegParameters mseg;
    std::array<ModSlotParameters, modGraphSlotCount> modGraphSlots {};
    std::array<ModSlotParameters, 4> moduleModSlots {};
    std::array<float, 3> extraLfoRate {{ 0.5f, 2.0f, 5.0f }};
    std::array<int, 3> extraLfoShape {};
    std::array<FxModuleParameters, FxModuleParameters::slotCount> fxModules {};

    float drive = 0.0f;
    int distortionMode = 0; // soft saturation (legacy), hard clip, sine fold
    float distortionMix = 1.0f;
    float chorusMix = 0.0f, chorusRate = 0.35f, chorusDepth = 0.25f;
    float delayMix = 0.0f, delayTime = 0.28f, delayFeedback = 0.22f;
    float reverbMix = 0.0f, reverbSize = 0.45f, reverbDamping = 0.45f;
    float stereoWidth = 1.0f;
    float outputGainDb = -3.0f;

    // Global render-quality preference. This is deliberately not a matcher mutation
    // dimension: 0 = 1x, 1 = 2x, 2 = 4x nonlinear oversampling.
    int oversamplingQuality = 0;

    static constexpr int extraLayerCount = 7;
    std::array<std::shared_ptr<const VoiceParameters>, extraLayerCount> layers {};
    std::array<float, extraLayerCount> layerGain {{ 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f }}, layerPan {}, layerTune {};
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

    // Manual audition helpers for the editor's optional virtual keyboard.
    // juce::Synthesiser serialises these calls against rendering internally.
    void noteOnFromUi (int midiNote, float velocity)
    {
        synth.noteOn (1, juce::jlimit (0, 127, midiNote), juce::jlimit (0.0f, 1.0f, velocity));
    }

    void noteOffFromUi (int midiNote, float velocity = 0.0f)
    {
        synth.noteOff (1, juce::jlimit (0, 127, midiNote), juce::jlimit (0.0f, 1.0f, velocity), true);
    }

    void allNotesOffFromUi()
    {
        synth.allNotesOff (0, true);
    }

private:
    juce::Synthesiser synth;
    std::array<std::unique_ptr<SynthEngine>, VoiceParameters::extraLayerCount> layerEngines;
    std::array<bool, VoiceParameters::extraLayerCount> layerActive {};
    juce::AudioBuffer<float> layerScratch;
    VoiceParameters current;
    ModuleRack moduleRack;
    double sampleRate = 44100.0;

    juce::dsp::Chorus<float> chorus;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delay { 192000 };
    juce::Reverb reverb;
    std::unique_ptr<juce::dsp::Oversampling<float>> driveOversampling2x, driveOversampling4x;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> latencyCompensation { 512 };
    std::array<int, 3> intrinsicLatencySamples {{ 0, 0, 0 }};
    int fixedLatencySamples = 0;

    void processEffects (juce::AudioBuffer<float>& audio);
    void compensateLatency (juce::AudioBuffer<float>& audio);
};
