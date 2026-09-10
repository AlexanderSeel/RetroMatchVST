#pragma once

#include "../Engine/SynthEngine.h"

namespace GeneratedRackGainPolicy
{
constexpr float defaultCoherentBudget = 0.92f;

inline int totalInstancesForComplexity (int complexity, int classicInstances = 3) noexcept
{
    complexity = juce::jlimit (0, 3, complexity);
    if (complexity == 0) return juce::jlimit (1, 3, classicInstances);
    static constexpr int expandedDepths[] { 4, 6, 8 };
    return expandedDepths[complexity - 1];
}

inline bool isSupportedTotalInstances (int totalInstances) noexcept
{
    return totalInstances == 3 || totalInstances == 4 || totalInstances == 6 || totalInstances == 8;
}

inline bool hasActiveAdditiveLayer (const VoiceParameters& rack) noexcept
{
    for (size_t i = 0; i < rack.layers.size(); ++i)
        if (rack.layers[i] != nullptr
            && rack.layerOperation[i] == 0
            && juce::jlimit (0.0f, 1.0f, rack.layerGain[i]) > 1.0e-6f
            && juce::jlimit (0.0f, 1.0f, rack.layerAmount[i]) > 1.0e-6f)
            return true;
    return false;
}

inline float coherentContribution (const VoiceParameters& rack) noexcept
{
    float contribution = juce::jlimit (0.0f, 1.0f, rack.mainLayerGain);
    for (size_t i = 0; i < rack.layers.size(); ++i)
    {
        if (rack.layers[i] == nullptr || rack.layerOperation[i] != 0)
            continue;

        contribution += juce::jlimit (0.0f, 1.0f, rack.layerGain[i])
                      * juce::jlimit (0.0f, 1.0f, rack.layerAmount[i]);
    }
    return contribution;
}

// Generated additive resynthesis racks can contain up to eight coherent voices.
// Scale the summing controls as one group so the designed main/companion balance
// remains intact while the worst-case coherent sum retains explicit headroom.
// Single-voice patches remain untouched; callers opt generated racks into this policy.
inline float apply (VoiceParameters& rack, float budget = defaultCoherentBudget) noexcept
{
    if (! hasActiveAdditiveLayer (rack))
        return 1.0f;

    budget = juce::jlimit (0.05f, 1.0f, budget);
    const float contribution = coherentContribution (rack);
    if (contribution <= budget || contribution <= 1.0e-6f)
        return 1.0f;

    const float scale = budget / contribution;
    rack.mainLayerGain = juce::jlimit (0.0f, 1.0f, rack.mainLayerGain * scale);
    for (size_t i = 0; i < rack.layers.size(); ++i)
    {
        if (rack.layers[i] != nullptr && rack.layerOperation[i] == 0)
            rack.layerGain[i] = juce::jlimit (0.0f, 1.0f, rack.layerGain[i] * scale);
    }
    return scale;
}
}
