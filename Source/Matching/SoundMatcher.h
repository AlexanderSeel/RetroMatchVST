#pragma once
#include <JuceHeader.h>
#include "../Analysis/SampleAnalyzer.h"
#include "../Engine/SynthEngine.h"
#include "SimilarityScorer.h"
#include "RenderTelemetry.h"
#include <functional>
#include <cmath>

struct MatchSettings
{
    int iterations = 132;
    int topologyTrials = 24;
    int populationSize = 6;
    double renderSampleRate = 44100.0;
    float maxRenderSeconds = 12.0f;
    // 0 balanced hybrid, 1 reference wavetable, 2 subtractive/spectral,
    // 3 FM/harmonic, 4 layered studio, 5 texture/chopped wavetable,
    // 6 FX/guitar chain with diagnostic excitation scoring.
    int algorithm = 0;

    bool lockPitch = false;
    bool lockOscillators = false;
    bool lockFm = false;
    bool lockEnvelope = false;
    bool lockFilter = false;
    bool lockModulation = false;
    bool lockEffects = false;

    MatchSettings bounded() const noexcept
    {
        auto result = *this;
        result.iterations = juce::jlimit (0, 2000, iterations);
        result.topologyTrials = juce::jlimit (0, 256, topologyTrials);
        result.populationSize = juce::jlimit (2, 32, populationSize);
        result.renderSampleRate = juce::jlimit (8000.0, 192000.0, renderSampleRate);
        result.maxRenderSeconds = juce::jlimit (0.5f, 60.0f, maxRenderSeconds);
        result.algorithm = juce::jlimit (0, 6, algorithm);
        return result;
    }
};

struct MatchResult
{
    VoiceParameters params;
    SoundFeatures candidateFeatures;
    SimilarityBreakdown similarity;
    float confidence = 0.0f;
    int evaluatedCandidates = 0;
    float effectProbeSimilarity = -1.0f;
    float tailSilenceSimilarity = -1.0f; // one-shot held-note tail score; -1 when not applicable
    RenderTelemetry renderTelemetry;
    float technicalSafetyScore = -1.0f; // -1 until a candidate has actually been rendered
    bool technicallySafe = false;
    int algorithm = -1;      // selected resynthesis strategy when the result owns one
    int complexity = -1;     // selected 1-3 / 4 / 6 / 8 rack depth when the result owns one
    bool fullRackScore = false; // similarity was measured after all embedded layers rendered
    juce::String explanation;
};

enum class VariationDirection : int
{
    cinematic,
    atmospheric,
    organic,
    orchestral,
    staccato,
    percussive,
    techno,
    warmAnalog,
    dark,
    bright,
    wide,
    intimate,
    rhythmic,
    fragile,
    aggressive,
    glitch
};

class SoundMatcher
{
public:
    using ProgressCallback = std::function<void(float)>;
    using CancelCallback = std::function<bool()>;

    static MatchResult initialFit (const SoundFeatures& f);
    static VoiceParameters makeVariation (const VoiceParameters& source, int64 seed, float amount = 0.15f)
    {
        juce::Random random (seed); return mutate (source, random, juce::jlimit (0.0f, 1.0f, amount), true);
    }
    static VoiceParameters makeDirectedVariation (const VoiceParameters& source, VariationDirection direction,
                                                  int64 seed, float intensity = 0.35f,
                                                  const MatchSettings& lockSettings = {})
    {
        auto result = makeVariation (source, seed, juce::jlimit (0.0f, 1.0f, intensity) * 0.45f);
        const float amount = juce::jlimit (0.0f, 1.0f, intensity);
        auto scale = [amount] (float value, float centre, float low, float high, float weight)
        {
            return juce::jlimit (low, high, centre + (value - centre) * (1.0f + amount * weight));
        };
        switch (direction)
        {
            case VariationDirection::cinematic: result.reverbMix = juce::jmax (result.reverbMix, amount * 0.55f); result.delayMix = juce::jmax (result.delayMix, amount * 0.22f); break;
            case VariationDirection::atmospheric: result.reverbMix = juce::jmax (result.reverbMix, amount * 0.42f); result.lfoAmp = juce::jmax (result.lfoAmp, amount * 0.45f); break;
            case VariationDirection::organic: result.lfoRate = scale (result.lfoRate, 1.5f, 0.02f, 8.0f, 0.45f); result.fmMix *= 1.0f - amount * 0.35f; break;
            case VariationDirection::orchestral: result.attack = scale (result.attack, 0.12f, 0.001f, 2.0f, 0.6f); result.reverbMix = juce::jmax (result.reverbMix, amount * 0.30f); break;
            case VariationDirection::staccato: result.attack = juce::jlimit (0.001f, 0.25f, result.attack * (1.0f - amount * 0.65f)); result.release = juce::jlimit (0.01f, 2.0f, result.release * (1.0f - amount * 0.45f)); break;
            case VariationDirection::percussive: result.sustain *= 1.0f - amount * 0.75f; result.fmAmount = juce::jmax (result.fmAmount, amount * 0.18f); break;
            case VariationDirection::techno: result.resonance = juce::jmax (result.resonance, amount * 0.42f); result.lfoCutoff = juce::jmax (result.lfoCutoff, amount * 1.2f); break;
            case VariationDirection::warmAnalog: result.wavefold *= 1.0f - amount * 0.65f; result.cutoff = scale (result.cutoff, 9000.0f, 120.0f, 19000.0f, -0.35f); break;
            case VariationDirection::dark: result.cutoff = juce::jlimit (120.0f, 19000.0f, result.cutoff * (1.0f - amount * 0.55f)); break;
            case VariationDirection::bright: result.cutoff = juce::jlimit (120.0f, 19000.0f, result.cutoff * (1.0f + amount * 0.55f)); break;
            case VariationDirection::wide: result.stereoWidth = juce::jlimit (0.0f, 2.0f, result.stereoWidth + amount * 0.65f); break;
            case VariationDirection::intimate: result.stereoWidth = juce::jlimit (0.0f, 2.0f, result.stereoWidth * (1.0f - amount * 0.45f)); break;
            case VariationDirection::rhythmic: result.mseg.enabled = true; result.mseg.loopEnabled = true; result.msegTarget = (int) ModDestination::amplitude; result.msegDepth = juce::jmax (result.msegDepth, amount * 0.35f); break;
            case VariationDirection::fragile: result.noiseMix = juce::jmax (result.noiseMix, amount * 0.08f); result.outputGainDb = juce::jmin (result.outputGainDb, -6.0f); break;
            case VariationDirection::aggressive: result.fmAmount = juce::jmax (result.fmAmount, amount * 0.25f); result.drive = juce::jmax (result.drive, amount * 0.22f); break;
            case VariationDirection::glitch: result.wavetableWarp = juce::jlimit (-1.0f, 1.0f, result.wavetableWarp + amount * 0.35f); result.ringMix = juce::jmax (result.ringMix, amount * 0.16f); break;
        }
        applyLocks (result, source, lockSettings);
        clamp (result);
        return result;
    }
    static juce::StringArray changedVariationDimensions (const VoiceParameters& source,
                                                         const VoiceParameters& variant)
    {
        juce::StringArray changed;
        const auto differs = [] (float a, float b) { return std::abs (a - b) > 0.0005f; };
        if (source.masterTuneCents != variant.masterTuneCents || source.osc2Semitones != variant.osc2Semitones
            || differs (source.osc2Detune, variant.osc2Detune)) changed.add ("PITCH");
        if (source.osc1Wave != variant.osc1Wave || source.osc2Wave != variant.osc2Wave
            || differs (source.osc1Mix, variant.osc1Mix) || differs (source.osc2Mix, variant.osc2Mix)
            || differs (source.wavetableMix, variant.wavetableMix) || differs (source.supersawMix, variant.supersawMix)) changed.add ("OSCILLATORS");
        if (differs (source.fmAmount, variant.fmAmount) || differs (source.fmMix, variant.fmMix)
            || source.fmAlgorithm != variant.fmAlgorithm) changed.add ("FM");
        if (differs (source.attack, variant.attack) || differs (source.decay, variant.decay)
            || differs (source.sustain, variant.sustain) || differs (source.release, variant.release)) changed.add ("ENVELOPE");
        if (differs (source.cutoff, variant.cutoff) || differs (source.resonance, variant.resonance)) changed.add ("FILTER");
        if (differs (source.lfoRate, variant.lfoRate) || differs (source.lfoAmp, variant.lfoAmp)
            || source.mseg.enabled != variant.mseg.enabled || differs (source.msegDepth, variant.msegDepth)) changed.add ("MODULATION");
        if (differs (source.chorusMix, variant.chorusMix) || differs (source.delayMix, variant.delayMix)
            || differs (source.reverbMix, variant.reverbMix) || differs (source.drive, variant.drive)) changed.add ("FX");
        if (differs (source.stereoWidth, variant.stereoWidth) || differs (source.unisonSpread, variant.unisonSpread)) changed.add ("STEREO");
        if (changed.isEmpty()) changed.add ("NONE");
        return changed;
    }
    // A bounded, parameter-space branch distance for Magic UI/telemetry. This is
    // intentionally independent from rendered similarity: it reports how much the
    // controls moved from the immutable origin, not whether the result matches a sample.
    static float normalizedVariationDistance (const VoiceParameters& source,
                                              const VoiceParameters& variant) noexcept
    {
        double sum = 0.0;
        int count = 0;
        const auto add = [&] (float a, float b, float range)
        {
            if (! std::isfinite (a) || ! std::isfinite (b)) return;
            sum += juce::jlimit (0.0, 1.0, std::abs ((double) a - b) / juce::jmax (0.0001, (double) range));
            ++count;
        };
        add (source.masterTuneCents, variant.masterTuneCents, 2400.0f);
        add (source.osc2Semitones, variant.osc2Semitones, 48.0f);
        add (source.osc1Mix, variant.osc1Mix, 1.0f); add (source.osc2Mix, variant.osc2Mix, 1.0f);
        add (source.subMix, variant.subMix, 1.0f); add (source.noiseMix, variant.noiseMix, 1.0f);
        add (source.wavetableMix, variant.wavetableMix, 1.0f); add (source.supersawMix, variant.supersawMix, 1.0f);
        add (source.fmAmount, variant.fmAmount, 1.0f); add (source.fmMix, variant.fmMix, 1.0f);
        add (source.attack, variant.attack, 2.0f); add (source.decay, variant.decay, 2.0f);
        add (source.sustain, variant.sustain, 1.0f); add (source.release, variant.release, 2.0f);
        add (source.cutoff, variant.cutoff, 19000.0f); add (source.resonance, variant.resonance, 1.0f);
        add (source.lfoRate, variant.lfoRate, 20.0f); add (source.lfoAmp, variant.lfoAmp, 1.0f);
        add (source.chorusMix, variant.chorusMix, 1.0f); add (source.delayMix, variant.delayMix, 1.0f);
        add (source.reverbMix, variant.reverbMix, 1.0f); add (source.drive, variant.drive, 1.0f);
        add (source.stereoWidth, variant.stereoWidth, 2.0f); add (source.outputGainDb, variant.outputGainDb, 24.0f);
        for (int i = 0; i < VoiceParameters::extraLayerCount; ++i)
        {
            add (source.layerGain[(size_t) i], variant.layerGain[(size_t) i], 1.0f);
            add (source.layerPan[(size_t) i], variant.layerPan[(size_t) i], 1.0f);
            add (source.layerTune[(size_t) i], variant.layerTune[(size_t) i], 48.0f);
        }
        return count > 0 ? (float) juce::jlimit (0.0, 1.0, sum / (double) count) : 0.0f;
    }
    static MatchResult evaluateFit (const SoundFeatures& reference, const VoiceParameters& params,
                                    const MatchSettings& settings = {});

    // Reference lifecycle is a hard generation invariant, separate from similarity scoring.
    // This is intentionally public so AI variants and layered/GOLD racks cannot reintroduce
    // keyboard sustain, looping amplitude motion or time-based tails after matching.
    static bool referenceSelfTerminates (const SoundFeatures& reference);
    static void enforceReferenceLifecycle (const SoundFeatures& reference, VoiceParameters& params);

    static MatchResult refineFit (const SoundFeatures& reference,
                                  const VoiceParameters& seed,
                                  const MatchSettings& settings = {},
                                  ProgressCallback progress = {},
                                  CancelCallback cancel = {});

private:
    // Core entry points are the pre-policy implementation compiled through
    // SoundMatcherCore.inc. Public entry points above add reference lifecycle policy.
    static MatchResult initialFitCore (const SoundFeatures& f);
    static MatchResult evaluateFitCore (const SoundFeatures& reference, const VoiceParameters& params,
                                        const MatchSettings& settings = {});
    static MatchResult refineFitCore (const SoundFeatures& reference,
                                      const VoiceParameters& seed,
                                      const MatchSettings& settings = {},
                                      ProgressCallback progress = {},
                                      CancelCallback cancel = {});

    static VoiceParameters mutate (const VoiceParameters& source, juce::Random& random, float amount, bool allowTopology);
    static void clamp (VoiceParameters& p);
    static void applyLocks (VoiceParameters& candidate, const VoiceParameters& seed, const MatchSettings& settings);
};
