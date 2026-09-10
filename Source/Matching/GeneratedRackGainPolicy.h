#pragma once

#include "../Engine/SynthEngine.h"

namespace GeneratedRackGainPolicy
{
constexpr float defaultCoherentBudget = 0.92f;

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
// This helper is intentionally opt-in: user-authored layer racks are not changed.
inline float apply (VoiceParameters& rack, float budget = defaultCoherentBudget) noexcept
{
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
