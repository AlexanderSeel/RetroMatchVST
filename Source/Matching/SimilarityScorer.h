#pragma once
#include "../Analysis/SampleAnalyzer.h"

struct SimilarityBreakdown
{
    float total = 0.0f;
    float spectrum = 0.0f;
    float temporal = 0.0f;
    float timbre = 0.0f;
    float brightness = 0.0f;
    float envelope = 0.0f;
    float harmonic = 0.0f;
    float pitch = 0.0f;
    float stereo = 0.0f;
    // Reference-relative guard against unexplained upper-band expansion. This is
    // kept separate from the ordinary brightness similarity so callers/debug UI
    // can distinguish timbral mismatch from suspicious harshness/alias-like energy.
    float spectralSafety = 1.0f;
};

class SimilarityScorer
{
public:
    static SimilarityBreakdown compare (const SoundFeatures& reference, const SoundFeatures& candidate);
};
