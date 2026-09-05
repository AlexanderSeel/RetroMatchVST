#include "SoundMatcher.h"
#include "OfflineRenderer.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
void isolateOscillator (VoiceParameters& p, int waveform)
{
    p.osc1Wave = waveform;
    p.osc1Mix = 0.85f;
    p.osc2Mix = p.subMix = p.noiseMix = p.ringMix = p.additiveMix = 0.0f;
    p.wavetableMix = p.referenceWavetableMix = p.userWavetableMix = p.supersawMix = 0.0f;
    p.fmAmount = p.fmMix = p.wavefold = p.drive = 0.0f;
    p.chorusMix = p.delayMix = p.reverbMix = 0.0f;
    p.lfoPitch = p.lfoCutoff = p.lfoAmp = 0.0f;
    p.modSlots = {};
    p.filterType = 0;
    p.resonance = 0.014f; // Approximately Butterworth Q in the engine's mapping.
}

float tuneCentsForReference (float hz)
{
    if (hz < 25.0f || hz > 5000.0f) return 0.0f;
    const auto note = std::round (69.0 + 12.0 * std::log2 (hz / 440.0));
    const auto noteHz = 440.0 * std::pow (2.0, (note - 69.0) / 12.0);
    return juce::jlimit (-100.0f, 100.0f, (float) (1200.0 * std::log2 (hz / noteHz)));
}

float gaussian (juce::Random& r)
{
    const float u1 = juce::jmax (1.0e-6f, r.nextFloat());
    const float u2 = r.nextFloat();
    return std::sqrt (-2.0f * std::log (u1)) * std::cos (juce::MathConstants<float>::twoPi * u2);
}

float mutateLinear (float value, float minValue, float maxValue, float amount, juce::Random& r)
{
    return juce::jlimit (minValue, maxValue, value + gaussian (r) * (maxValue - minValue) * amount);
}

float mutateLog (float value, float minValue, float maxValue, float amount, juce::Random& r)
{
    const float lv = std::log (juce::jlimit (minValue, maxValue, value));
    const float lo = std::log (minValue), hi = std::log (maxValue);
    return std::exp (juce::jlimit (lo, hi, lv + gaussian (r) * (hi - lo) * amount));
}
}

MatchResult SoundMatcher::initialFit (const SoundFeatures& f)
{
    MatchResult r;
    auto& p = r.params;

    p.masterTuneCents = tuneCentsForReference (f.fundamentalHz);
    p.attack = juce::jlimit (0.001f, 3.0f, f.attackSeconds);
    p.decay = juce::jlimit (0.02f, 3.0f, f.decaySeconds > 0.01f ? f.decaySeconds : (f.transientScore > 0.6f ? 0.18f : 0.45f));
    p.sustain = juce::jlimit (0.05f, 1.0f, f.sustainLevel);
    p.release = juce::jlimit (0.03f, 5.0f, f.releaseSeconds > 0.01f ? f.releaseSeconds : (f.duration < 1.0f ? 0.18f : 0.55f));

    p.cutoff = juce::jlimit (120.0f, 19000.0f, f.spectralCentroidHz * 3.0f + 120.0f);
    p.resonance = juce::jlimit (0.04f, 0.72f, 0.10f + f.harmonicity * 0.22f);
    p.noiseMix = juce::jlimit (0.0f, 0.48f, f.spectralFlatness * 0.62f + f.zeroCrossingRate * 0.75f);
    p.subMix = juce::jlimit (0.0f, 0.45f, (f.lowEnergyRatio - 0.12f) * 1.2f);
    p.additiveMix = juce::jlimit (0.0f, 0.42f, (f.harmonicity - 0.55f) * 0.7f);
    p.wavetableMix = juce::jlimit (0.0f, 0.38f, f.spectralMotion * 0.8f + f.highEnergyRatio * 0.45f);
    p.wavetablePosition = juce::jlimit (0.05f, 0.95f, 0.18f + f.highEnergyRatio * 1.9f + f.spectralFlatness * 0.35f);
    p.wavetableWarp = juce::jlimit (-0.45f, 0.45f, (f.oddHarmonicRatio - 0.5f) * 0.5f);
    p.referenceWavetableMix = 0.0f;
    p.supersawMix = juce::jlimit (0.0f, 0.42f, f.stereoWidth * 0.45f + juce::jmax (0.0f, f.harmonicity - 0.65f) * 0.35f);
    p.unisonDetune = juce::jlimit (4.0f, 42.0f, 8.0f + f.stereoWidth * 30.0f);
    p.unisonSpread = juce::jlimit (0.15f, 1.0f, 0.35f + f.stereoWidth * 0.65f);
    p.wavefold = juce::jlimit (0.0f, 0.32f, juce::jmax (0.0f, f.highEnergyRatio - 0.10f) * (1.0f - f.spectralFlatness) * 1.3f);
    p.harmonicTilt = juce::jlimit (0.55f, 3.2f, 2.4f - f.highEnergyRatio * 5.0f);
    p.oddEvenBalance = juce::jlimit (0.0f, 1.0f, f.oddHarmonicRatio);

    if (f.oddHarmonicRatio > 0.72f) p.osc1Wave = 2;
    else if (f.spectralFlatness < 0.12f && f.harmonicity > 0.70f) p.osc1Wave = 1;
    else if (f.harmonicity > 0.78f) p.osc1Wave = 3;
    else p.osc1Wave = 1;

    p.osc2Wave = f.spectralCentroidHz > 3400.0f ? 2 : 0;
    p.osc1Mix = 0.72f;
    p.osc2Mix = juce::jlimit (0.05f, 0.55f, 0.12f + 0.36f * f.harmonicity);
    p.fmAmount = f.highEnergyRatio > 0.16f && f.harmonicity > 0.45f ? 0.06f : 0.0f;
    p.fmRatio = f.oddHarmonicRatio < 0.42f ? 2.0f : 1.0f;
    p.fmMix = juce::jlimit (0.0f, 0.42f, (f.highEnergyRatio - 0.06f) * 1.5f * (0.45f + f.harmonicity));
    p.fmFeedback = juce::jlimit (0.0f, 0.28f, (f.spectralFlatness < 0.20f ? f.highEnergyRatio : 0.0f) * 1.2f);
    p.fmAlgorithm = f.oddHarmonicRatio < 0.42f ? 2 : (f.spectralFlatness < 0.10f ? 5 : 1);
    p.fmOpRatio = {{ 1.0f, 2.0f, 3.0f, 1.0f, 4.0f, 2.0f }};
    p.fmOpLevel = {{ 1.0f, 0.52f, 0.30f, 0.22f, 0.16f, 0.12f }};
    if (f.inharmonicity > 0.18f && f.spectralFlatness < 0.32f)
    {
        p.fmOpFixedMode[5] = 1;
        p.fmOpFixedHz[5] = juce::jlimit (80.0f, 6000.0f, juce::jmax (220.0f, f.spectralCentroidHz * 0.55f));
        p.fmMix = juce::jmax (p.fmMix, juce::jlimit (0.08f, 0.38f, f.inharmonicity * 0.75f));
    }
    for (int i = 0; i < VoiceParameters::fmOperatorCount; ++i)
    {
        const float brightnessScale = 1.0f - i * 0.08f;
        p.fmOpAttack[(size_t) i] = juce::jlimit (0.001f, 2.0f, p.attack * (i == 0 ? 1.0f : 0.45f));
        p.fmOpDecay[(size_t) i] = juce::jlimit (0.01f, 3.0f, p.decay * (0.65f + i * 0.08f));
        p.fmOpSustain[(size_t) i] = juce::jlimit (0.0f, 1.0f, p.sustain * brightnessScale);
        p.fmOpRelease[(size_t) i] = juce::jlimit (0.01f, 4.0f, p.release * (0.65f + i * 0.06f));
        p.fmOpVelocity[(size_t) i] = juce::jlimit (0.15f, 0.85f, 0.30f + f.transientScore * 0.45f + i * 0.035f);
        p.fmOpKeyScale[(size_t) i] = juce::jlimit (0.0f, 0.65f, i * 0.045f + f.highEnergyRatio * 0.15f);
    }
    p.pulseWidth = juce::jlimit (0.15f, 0.85f, 0.5f + (f.oddHarmonicRatio - 0.5f) * 0.45f);

    p.drive = juce::jlimit (0.0f, 0.45f, (f.highEnergyRatio - 0.12f) * 0.8f);
    p.chorusMix = juce::jlimit (0.0f, 0.45f, f.stereoWidth * 0.65f);
    p.reverbMix = juce::jlimit (0.0f, 0.40f, juce::jmax (0.0f, f.releaseSeconds - 0.15f) * 0.16f);
    p.stereoWidth = juce::jlimit (0.7f, 1.8f, 1.0f + f.stereoWidth * 0.75f);

    // Very short recordings are usually plucks, hits, or one-shot basses.
    // Envelope tails and room noise can make their measured release look long,
    // which previously caused the seed to bloom into a pad. Keep the seed
    // transient and dry when the source is short or strongly transient.
    const bool shortTransient = f.duration < 1.35f || f.transientScore > 0.72f;
    if (shortTransient)
    {
        p.attack = juce::jlimit (0.001f, 0.045f, p.attack);
        p.decay = juce::jlimit (0.02f, 0.42f, p.decay);
        p.sustain = juce::jlimit (0.03f, 0.55f, p.sustain * 0.72f);
        p.release = juce::jlimit (0.025f, 0.42f, juce::jmin (p.release, juce::jmax (0.06f, f.duration * 0.38f)));
        p.reverbMix = 0.0f;
        p.chorusMix = juce::jmin (p.chorusMix, 0.06f);
        p.wavetableMix *= 0.45f;
        p.supersawMix *= 0.35f;
        p.fmMix *= 0.65f;
    }

    if (! shortTransient && f.spectralMotion > 0.09f && f.sustainLevel > 0.35f)
    {
        p.lfoRate = juce::jlimit (0.08f, 4.0f, 0.25f + f.spectralMotion * 3.0f);
        p.modSlots[0].source = (int) ModSource::lfo1;
        p.modSlots[0].destination = (int) ModDestination::cutoff;
        p.modSlots[0].amount = juce::jlimit (0.05f, 0.45f, f.spectralMotion * 0.75f);
    }

    // A sine has an almost entirely odd spectrum too. Odd/even balance alone
    // must not classify it as a square or add a sub-octave to every bass sound.
    if (f.pitchConfidence > 0.8f && f.fundamentalHz > 20.0f
        && f.spectralRolloffHz < f.fundamentalHz * 1.5f
        && f.spectralCentroidHz < f.fundamentalHz * 1.6f)
    {
        isolateOscillator (p, 0);
        p.cutoff = juce::jlimit (1200.0f, 19000.0f, f.fundamentalHz * 8.0f);
    }

    r.confidence = juce::jlimit (0.12f, 0.72f,
                                 0.22f + f.pitchConfidence * 0.18f + f.harmonicity * 0.18f + f.transientScore * 0.10f);
    r.explanation = "Deterministic seed from pitch, envelope, global/time-varying log-spectrum, cepstral timbre, harmonic/noise balance, stereo width and transient character.";
    return r;
}

MatchResult SoundMatcher::evaluateFit (const SoundFeatures& reference, const VoiceParameters& params, const MatchSettings& settings)
{
    MatchResult result;
    result.params = params;
    const float renderDuration = juce::jlimit (0.5f, settings.maxRenderSeconds,
                                              juce::jmax (0.65f, reference.duration));
    auto audio = OfflineRenderer::renderPatch (params, settings.renderSampleRate, renderDuration, reference.fundamentalHz);

    result.candidateFeatures = SampleAnalyzer::analyzeBuffer (audio, settings.renderSampleRate, reference.fundamentalHz);

    result.similarity = SimilarityScorer::compare (reference, result.candidateFeatures);
    result.confidence = result.similarity.total;
    result.evaluatedCandidates = 1;
    return result;
}

VoiceParameters SoundMatcher::mutate (const VoiceParameters& source, juce::Random& random, float amount, bool allowTopology)
{
    auto p = source;

    p.osc1Mix = mutateLinear (p.osc1Mix, 0.0f, 1.0f, amount, random);
    p.osc2Mix = mutateLinear (p.osc2Mix, 0.0f, 1.0f, amount, random);
    p.subMix = mutateLinear (p.subMix, 0.0f, 0.65f, amount, random);
    p.noiseMix = mutateLinear (p.noiseMix, 0.0f, 0.65f, amount, random);
    p.ringMix = mutateLinear (p.ringMix, 0.0f, 0.55f, amount, random);
    p.additiveMix = mutateLinear (p.additiveMix, 0.0f, 0.85f, amount, random);
    p.wavetableMix = mutateLinear (p.wavetableMix, 0.0f, 1.0f, amount, random);
    p.wavetablePosition = mutateLinear (p.wavetablePosition, 0.0f, 1.0f, amount, random);
    p.wavetableWarp = mutateLinear (p.wavetableWarp, -1.0f, 1.0f, amount * 0.65f, random);
    p.referenceWavetableMix = mutateLinear (p.referenceWavetableMix, 0.0f, 1.0f, amount, random);
    p.supersawMix = mutateLinear (p.supersawMix, 0.0f, 1.0f, amount, random);
    p.unisonDetune = mutateLinear (p.unisonDetune, 0.0f, 70.0f, amount * 0.65f, random);
    p.unisonSpread = mutateLinear (p.unisonSpread, 0.0f, 1.0f, amount * 0.7f, random);
    p.wavefold = mutateLinear (p.wavefold, 0.0f, 1.0f, amount * 0.8f, random);
    p.pulseWidth = mutateLinear (p.pulseWidth, 0.08f, 0.92f, amount, random);

    p.osc2Semitones = std::round (mutateLinear (p.osc2Semitones, -24.0f, 24.0f, amount * 0.55f, random));
    p.osc2Detune = mutateLinear (p.osc2Detune, -50.0f, 50.0f, amount, random);
    p.fmAmount = mutateLinear (p.fmAmount, 0.0f, 0.65f, amount, random);
    p.fmRatio = mutateLog (p.fmRatio, 0.25f, 8.0f, amount * 0.7f, random);
    p.fmMix = mutateLinear (p.fmMix, 0.0f, 1.0f, amount, random);
    p.fmFeedback = mutateLinear (p.fmFeedback, 0.0f, 1.0f, amount * 0.75f, random);
    for (int i = 0; i < VoiceParameters::fmOperatorCount; ++i)
    {
        p.fmOpRatio[(size_t) i] = mutateLog (p.fmOpRatio[(size_t) i], 0.125f, 16.0f, amount * 0.48f, random);
        if (random.nextFloat() < 0.35f && amount > 0.08f)
            p.fmOpRatio[(size_t) i] = juce::jlimit (0.125f, 16.0f, std::round (p.fmOpRatio[(size_t) i] * 2.0f) * 0.5f);
        p.fmOpLevel[(size_t) i] = mutateLinear (p.fmOpLevel[(size_t) i], 0.0f, 1.25f, amount * 0.72f, random);
        p.fmOpFixedHz[(size_t) i] = mutateLog (p.fmOpFixedHz[(size_t) i], 10.0f, 16000.0f, amount * 0.45f, random);
        p.fmOpAttack[(size_t) i] = mutateLog (p.fmOpAttack[(size_t) i], 0.001f, 5.0f, amount * 0.55f, random);
        p.fmOpDecay[(size_t) i] = mutateLog (p.fmOpDecay[(size_t) i], 0.001f, 5.0f, amount * 0.55f, random);
        p.fmOpSustain[(size_t) i] = mutateLinear (p.fmOpSustain[(size_t) i], 0.0f, 1.0f, amount * 0.6f, random);
        p.fmOpRelease[(size_t) i] = mutateLog (p.fmOpRelease[(size_t) i], 0.001f, 8.0f, amount * 0.55f, random);
        p.fmOpKeyScale[(size_t) i] = mutateLinear (p.fmOpKeyScale[(size_t) i], 0.0f, 1.0f, amount * 0.45f, random);
        p.fmOpVelocity[(size_t) i] = mutateLinear (p.fmOpVelocity[(size_t) i], 0.0f, 1.0f, amount * 0.45f, random);
        if (allowTopology && random.nextFloat() < 0.045f) p.fmOpFixedMode[(size_t) i] = 1 - p.fmOpFixedMode[(size_t) i];
    }
    p.harmonicTilt = mutateLinear (p.harmonicTilt, 0.45f, 3.5f, amount, random);
    p.oddEvenBalance = mutateLinear (p.oddEvenBalance, 0.0f, 1.0f, amount, random);

    p.attack = mutateLog (p.attack, 0.001f, 5.0f, amount, random);
    p.decay = mutateLog (p.decay, 0.005f, 5.0f, amount, random);
    p.sustain = mutateLinear (p.sustain, 0.0f, 1.0f, amount, random);
    p.release = mutateLog (p.release, 0.005f, 8.0f, amount, random);
    p.cutoff = mutateLog (p.cutoff, 40.0f, 20000.0f, amount, random);
    p.resonance = mutateLinear (p.resonance, 0.01f, 0.95f, amount, random);

    p.lfoRate = mutateLog (p.lfoRate, 0.03f, 20.0f, amount * 0.65f, random);
    p.lfoPitch = mutateLinear (p.lfoPitch, 0.0f, 1.5f, amount * 0.5f, random);
    p.lfoCutoff = mutateLinear (p.lfoCutoff, 0.0f, 3.0f, amount * 0.5f, random);
    p.lfoAmp = mutateLinear (p.lfoAmp, 0.0f, 1.0f, amount * 0.5f, random);
    for (auto& slot : p.modSlots)
        if (slot.source != (int) ModSource::none && slot.destination != (int) ModDestination::none)
            slot.amount = mutateLinear (slot.amount, -1.0f, 1.0f, amount * 0.45f, random);

    p.drive = mutateLinear (p.drive, 0.0f, 1.0f, amount, random);
    p.chorusMix = mutateLinear (p.chorusMix, 0.0f, 0.65f, amount, random);
    p.chorusRate = mutateLog (p.chorusRate, 0.03f, 5.0f, amount * 0.5f, random);
    p.chorusDepth = mutateLinear (p.chorusDepth, 0.0f, 1.0f, amount, random);
    p.delayMix = mutateLinear (p.delayMix, 0.0f, 0.55f, amount, random);
    p.delayTime = mutateLinear (p.delayTime, 0.04f, 0.85f, amount, random);
    p.delayFeedback = mutateLinear (p.delayFeedback, 0.0f, 0.75f, amount, random);
    p.reverbMix = mutateLinear (p.reverbMix, 0.0f, 0.55f, amount, random);
    p.reverbSize = mutateLinear (p.reverbSize, 0.05f, 1.0f, amount, random);
    p.reverbDamping = mutateLinear (p.reverbDamping, 0.0f, 1.0f, amount, random);
    p.stereoWidth = mutateLinear (p.stereoWidth, 0.0f, 2.0f, amount, random);

    if (allowTopology && random.nextFloat() < 0.28f) p.osc1Wave = random.nextInt (5);
    if (allowTopology && random.nextFloat() < 0.22f) p.osc2Wave = random.nextInt (5);
    if (allowTopology && random.nextFloat() < 0.12f) p.filterType = random.nextInt (3);
    if (allowTopology && random.nextFloat() < 0.22f) p.fmAlgorithm = random.nextInt (6);

    clamp (p);
    return p;
}

void SoundMatcher::clamp (VoiceParameters& p)
{
    p.osc1Wave = juce::jlimit (0, 4, p.osc1Wave);
    p.osc2Wave = juce::jlimit (0, 4, p.osc2Wave);
    p.filterType = juce::jlimit (0, 2, p.filterType);
    p.fmAlgorithm = juce::jlimit (0, 5, p.fmAlgorithm);
    p.fmMix = juce::jlimit (0.0f, 1.0f, p.fmMix);
    p.fmFeedback = juce::jlimit (0.0f, 1.0f, p.fmFeedback);
    p.wavetableMix = juce::jlimit (0.0f, 1.0f, p.wavetableMix);
    p.wavetablePosition = juce::jlimit (0.0f, 1.0f, p.wavetablePosition);
    p.wavetableWarp = juce::jlimit (-1.0f, 1.0f, p.wavetableWarp);
    p.referenceWavetableMix = juce::jlimit (0.0f, 1.0f, p.referenceWavetableMix);
    p.supersawMix = juce::jlimit (0.0f, 1.0f, p.supersawMix);
    p.unisonDetune = juce::jlimit (0.0f, 70.0f, p.unisonDetune);
    p.unisonSpread = juce::jlimit (0.0f, 1.0f, p.unisonSpread);
    p.wavefold = juce::jlimit (0.0f, 1.0f, p.wavefold);
    for (int i = 0; i < VoiceParameters::fmOperatorCount; ++i)
    {
        p.fmOpRatio[(size_t) i] = juce::jlimit (0.125f, 16.0f, p.fmOpRatio[(size_t) i]);
        p.fmOpLevel[(size_t) i] = juce::jlimit (0.0f, 1.25f, p.fmOpLevel[(size_t) i]);
        p.fmOpFixedMode[(size_t) i] = juce::jlimit (0, 1, p.fmOpFixedMode[(size_t) i]);
        p.fmOpFixedHz[(size_t) i] = juce::jlimit (10.0f, 16000.0f, p.fmOpFixedHz[(size_t) i]);
        p.fmOpAttack[(size_t) i] = juce::jlimit (0.001f, 5.0f, p.fmOpAttack[(size_t) i]);
        p.fmOpDecay[(size_t) i] = juce::jlimit (0.001f, 5.0f, p.fmOpDecay[(size_t) i]);
        p.fmOpSustain[(size_t) i] = juce::jlimit (0.0f, 1.0f, p.fmOpSustain[(size_t) i]);
        p.fmOpRelease[(size_t) i] = juce::jlimit (0.001f, 8.0f, p.fmOpRelease[(size_t) i]);
        p.fmOpKeyScale[(size_t) i] = juce::jlimit (0.0f, 1.0f, p.fmOpKeyScale[(size_t) i]);
        p.fmOpVelocity[(size_t) i] = juce::jlimit (0.0f, 1.0f, p.fmOpVelocity[(size_t) i]);
    }
    for (auto& slot : p.modSlots)
    {
        slot.source = juce::jlimit ((int) ModSource::none, (int) ModSource::ampEnvelope, slot.source);
        slot.destination = juce::jlimit ((int) ModDestination::none, (int) ModDestination::wavefold, slot.destination);
        slot.amount = juce::jlimit (-1.0f, 1.0f, slot.amount);
    }
    p.masterTuneCents = juce::jlimit (-100.0f, 100.0f, p.masterTuneCents);
    p.outputGainDb = juce::jlimit (-18.0f, 6.0f, p.outputGainDb);
}

void SoundMatcher::applyLocks (VoiceParameters& c, const VoiceParameters& s, const MatchSettings& settings)
{
    if (settings.lockPitch)
    {
        c.masterTuneCents = s.masterTuneCents;
        c.osc2Semitones = s.osc2Semitones;
        c.osc2Detune = s.osc2Detune;
    }
    if (settings.lockOscillators)
    {
        c.osc1Mix = s.osc1Mix; c.osc2Mix = s.osc2Mix; c.subMix = s.subMix; c.noiseMix = s.noiseMix;
        c.ringMix = s.ringMix; c.additiveMix = s.additiveMix; c.osc1Wave = s.osc1Wave; c.osc2Wave = s.osc2Wave;
        c.pulseWidth = s.pulseWidth; c.harmonicTilt = s.harmonicTilt; c.oddEvenBalance = s.oddEvenBalance;
        c.wavetableMix = s.wavetableMix; c.wavetablePosition = s.wavetablePosition; c.wavetableWarp = s.wavetableWarp; c.referenceWavetableMix = s.referenceWavetableMix;
        c.supersawMix = s.supersawMix; c.unisonDetune = s.unisonDetune; c.unisonSpread = s.unisonSpread; c.wavefold = s.wavefold;
    }
    if (settings.lockFm)
    {
        c.fmAmount = s.fmAmount; c.fmRatio = s.fmRatio; c.fmMix = s.fmMix; c.fmFeedback = s.fmFeedback;
        c.fmAlgorithm = s.fmAlgorithm; c.fmOpRatio = s.fmOpRatio; c.fmOpLevel = s.fmOpLevel;
        c.fmOpFixedMode = s.fmOpFixedMode; c.fmOpFixedHz = s.fmOpFixedHz;
        c.fmOpAttack = s.fmOpAttack; c.fmOpDecay = s.fmOpDecay; c.fmOpSustain = s.fmOpSustain; c.fmOpRelease = s.fmOpRelease;
        c.fmOpKeyScale = s.fmOpKeyScale; c.fmOpVelocity = s.fmOpVelocity;
    }
    if (settings.lockEnvelope)
    {
        c.attack = s.attack; c.decay = s.decay; c.sustain = s.sustain; c.release = s.release;
    }
    if (settings.lockFilter)
    {
        c.cutoff = s.cutoff; c.resonance = s.resonance; c.filterType = s.filterType;
    }
    if (settings.lockModulation)
    {
        c.lfoRate = s.lfoRate; c.lfoPitch = s.lfoPitch; c.lfoCutoff = s.lfoCutoff; c.lfoAmp = s.lfoAmp;
        c.modSlots = s.modSlots;
    }
    if (settings.lockEffects)
    {
        c.drive = s.drive; c.chorusMix = s.chorusMix; c.chorusRate = s.chorusRate; c.chorusDepth = s.chorusDepth;
        c.delayMix = s.delayMix; c.delayTime = s.delayTime; c.delayFeedback = s.delayFeedback;
        c.reverbMix = s.reverbMix; c.reverbSize = s.reverbSize; c.reverbDamping = s.reverbDamping;
        c.stereoWidth = s.stereoWidth; c.outputGainDb = s.outputGainDb;
    }
}

MatchResult SoundMatcher::refineFit (const SoundFeatures& reference,
                                     const VoiceParameters& seed,
                                     const MatchSettings& settings,
                                     ProgressCallback progress,
                                     CancelCallback cancel)
{
    juce::Random random ((int64) 0x524d5333);
    std::vector<MatchResult> elite;
    elite.reserve ((size_t) juce::jmax (2, settings.populationSize));

    auto insertElite = [&] (MatchResult candidate)
    {
        elite.push_back (std::move (candidate));
        std::sort (elite.begin(), elite.end(), [] (const auto& a, const auto& b)
        {
            return a.similarity.total > b.similarity.total;
        });
        if ((int) elite.size() > juce::jmax (2, settings.populationSize)) elite.resize ((size_t) settings.populationSize);
    };

    insertElite (evaluateFit (reference, seed, settings));
    int evaluated = 1;
    const int total = juce::jmax (1, 1 + settings.topologyTrials + settings.iterations);
    auto report = [&] { if (progress) progress (juce::jlimit (0.0f, 1.0f, evaluated / (float) total)); };
    report();

    for (int i = 0; i < settings.topologyTrials; ++i)
    {
        if (cancel && cancel()) break;
        auto candidate = seed;
        if (i < 5)
        {
            // Explicit sparse topologies can reach exact zero mixes, which a
            // simultaneous mutation of every layer almost never discovers.
            isolateOscillator (candidate, i == 4 ? 0 : i);
            if (i == 4 && candidate.referenceWavetable && candidate.referenceWavetable->valid)
            {
                candidate.osc1Mix = 0.0f;
                candidate.referenceWavetableMix = 0.85f;
                candidate.cutoff = 19000.0f;
            }
        }
        else
        {
            candidate.osc1Wave = random.nextInt (5);
            candidate.osc2Wave = random.nextInt (5);
            candidate.filterType = random.nextInt (3);
            candidate.fmAlgorithm = random.nextInt (6);
            if (random.nextBool()) candidate.fmAmount = random.nextFloat() * 0.30f;
            if (random.nextBool()) candidate.fmMix = random.nextFloat() * 0.55f;
            if (random.nextBool()) candidate.ringMix = random.nextFloat() * 0.25f;
            if (random.nextBool()) candidate.wavetableMix = random.nextFloat() * 0.55f;
            if (random.nextBool()) candidate.supersawMix = random.nextFloat() * 0.48f;
            if (random.nextFloat() < 0.35f) candidate.wavefold = random.nextFloat() * 0.42f;
            if (random.nextFloat() < 0.25f) candidate.fmOpFixedMode[(size_t) random.nextInt (VoiceParameters::fmOperatorCount)] = 1;
            candidate = mutate (candidate, random, 0.13f, false);
        }
        applyLocks (candidate, seed, settings);
        insertElite (evaluateFit (reference, candidate, settings));
        ++evaluated;
        report();
    }

    int stagnant = 0;
    float lastBest = elite.front().similarity.total;
    for (int i = 0; i < settings.iterations; ++i)
    {
        if (cancel && cancel()) break;
        const float phase = i / (float) juce::jmax (1, settings.iterations - 1);
        float amount = juce::jmap (phase, 0.0f, 1.0f, 0.16f, 0.014f);
        if (stagnant > 18) amount = juce::jmax (amount, 0.075f);
        const bool topology = i < settings.iterations / 3 || stagnant > 24;

        const int parentIndex = juce::jmin ((int) elite.size() - 1,
                                            (int) std::floor (std::pow (random.nextFloat(), 2.2f) * elite.size()));
        auto candidate = mutate (elite[(size_t) parentIndex].params, random, amount, topology);
        if (i % 2 == 0)
        {
            // Alternate broad exploration with envelope/filter-only steps so
            // refining a clean candidate doesn't turn every unused layer on.
            const auto mutated = candidate;
            candidate = elite[(size_t) parentIndex].params;
            candidate.attack = mutated.attack; candidate.decay = mutated.decay;
            candidate.sustain = mutated.sustain; candidate.release = mutated.release;
            candidate.cutoff = mutated.cutoff; candidate.resonance = mutated.resonance;
        }
        applyLocks (candidate, seed, settings);

        // Lightweight crossover between good candidates helps escape local minima without
        // requiring gradients through the discrete synth topology.
        if (elite.size() > 1 && random.nextFloat() < 0.22f)
        {
            const auto& donor = elite[(size_t) random.nextInt ((int) elite.size())].params;
            if (random.nextBool()) { candidate.cutoff = donor.cutoff; candidate.resonance = donor.resonance; }
            if (random.nextBool()) { candidate.attack = donor.attack; candidate.decay = donor.decay; candidate.sustain = donor.sustain; candidate.release = donor.release; }
            if (random.nextBool()) { candidate.fmAlgorithm = donor.fmAlgorithm; candidate.fmMix = donor.fmMix; candidate.fmFeedback = donor.fmFeedback; }
            if (random.nextBool()) { candidate.wavetableMix = donor.wavetableMix; candidate.wavetablePosition = donor.wavetablePosition; candidate.supersawMix = donor.supersawMix; candidate.wavefold = donor.wavefold; }
            if (random.nextFloat() < 0.35f) { candidate.fmOpAttack = donor.fmOpAttack; candidate.fmOpDecay = donor.fmOpDecay; candidate.fmOpSustain = donor.fmOpSustain; candidate.fmOpRelease = donor.fmOpRelease; }
            if (random.nextBool()) { candidate.chorusMix = donor.chorusMix; candidate.reverbMix = donor.reverbMix; candidate.stereoWidth = donor.stereoWidth; }
        }

        applyLocks (candidate, seed, settings);
        insertElite (evaluateFit (reference, candidate, settings));
        ++evaluated;
        const float nowBest = elite.front().similarity.total;
        if (nowBest > lastBest + 0.00025f) { lastBest = nowBest; stagnant = 0; }
        else ++stagnant;
        report();
    }

    auto best = elite.front();
    best.evaluatedCandidates = evaluated;
    best.confidence = best.similarity.total;
    best.explanation = "Population closed-loop match: candidate patches are rendered, re-analysed, ranked and evolved across VA, wavetable, supersaw/unison, wavefolding, additive and six-operator FM (including operator envelopes/fixed modes) against global spectrum, time-varying spectrum, cepstral timbre, envelope, harmonic, pitch and stereo descriptors.";
    if (progress) progress (1.0f);
    return best;
}
