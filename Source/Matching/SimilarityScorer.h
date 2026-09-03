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
};

class SimilarityScorer
{
public:
    static SimilarityBreakdown compare (const SoundFeatures& reference, const SoundFeatures& candidate);
};
