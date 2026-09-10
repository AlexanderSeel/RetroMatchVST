#pragma once
#include <JuceHeader.h>
#include "../Analysis/SampleAnalyzer.h"
#include "../Engine/SynthEngine.h"
#include "SimilarityScorer.h"
#include "RenderTelemetry.h"
#include <functional>

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
