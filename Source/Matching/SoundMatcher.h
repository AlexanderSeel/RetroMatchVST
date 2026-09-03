#pragma once
#include <JuceHeader.h>
#include "../Analysis/SampleAnalyzer.h"
#include "../Engine/SynthEngine.h"
#include "SimilarityScorer.h"
#include <functional>

struct MatchSettings
{
    int iterations = 132;
    int topologyTrials = 24;
    int populationSize = 6;
    double renderSampleRate = 44100.0;
    float maxRenderSeconds = 2.5f;

    bool lockPitch = false;
    bool lockOscillators = false;
    bool lockFm = false;
    bool lockEnvelope = false;
    bool lockFilter = false;
    bool lockModulation = false;
    bool lockEffects = false;
};

struct MatchResult
{
    VoiceParameters params;
    SoundFeatures candidateFeatures;
    SimilarityBreakdown similarity;
    float confidence = 0.0f;
    int evaluatedCandidates = 0;
    juce::String explanation;
};

class SoundMatcher
{
public:
    using ProgressCallback = std::function<void(float)>;
    using CancelCallback = std::function<bool()>;

    static MatchResult initialFit (const SoundFeatures& f);
    static MatchResult evaluateFit (const SoundFeatures& reference, const VoiceParameters& params,
                                    const MatchSettings& settings = {});
    static MatchResult refineFit (const SoundFeatures& reference,
                                  const VoiceParameters& seed,
                                  const MatchSettings& settings = {},
                                  ProgressCallback progress = {},
                                  CancelCallback cancel = {});

private:
    static VoiceParameters mutate (const VoiceParameters& source, juce::Random& random, float amount, bool allowTopology);
    static void clamp (VoiceParameters& p);
    static void applyLocks (VoiceParameters& candidate, const VoiceParameters& seed, const MatchSettings& settings);
};
