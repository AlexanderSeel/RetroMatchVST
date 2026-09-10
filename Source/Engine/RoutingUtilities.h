#pragma once

#include <JuceHeader.h>
#include <cmath>

// Small, allocation-free helpers shared by supported routing paths. Keeping
// bounds here prevents graph-authored gain/pan/width values from becoming an
// accidental DSP gain stage.
namespace RoutingUtilities
{
inline float boundedGain (float gain) noexcept
{
    return juce::jlimit (0.0f, 1.0f, std::isfinite (gain) ? gain : 0.0f);
}

inline float boundedPan (float pan) noexcept
{
    return juce::jlimit (-1.0f, 1.0f, std::isfinite (pan) ? pan : 0.0f);
}

inline float leftBalance (float pan) noexcept
{
    return juce::jmin (1.0f, 1.0f - boundedPan (pan));
}

inline float rightBalance (float pan) noexcept
{
    return juce::jmin (1.0f, 1.0f + boundedPan (pan));
}

inline float boundedWidth (float width) noexcept
{
    return juce::jlimit (0.0f, 2.0f, std::isfinite (width) ? width : 1.0f);
}

inline void mixParallel (juce::AudioBuffer<float>& destination,
                         const juce::AudioBuffer<float>& branch,
                         float destinationGain,
                         float branchGain) noexcept
{
    float a = boundedGain (destinationGain);
    float b = boundedGain (branchGain);
    const float sum = a + b;
    if (sum > 1.0e-6f)
    {
        a /= sum;
        b /= sum;
    }
    else
    {
        a = 1.0f;
        b = 0.0f;
    }

    const int channels = juce::jmin (destination.getNumChannels(), branch.getNumChannels());
    const int samples = juce::jmin (destination.getNumSamples(), branch.getNumSamples());
    for (int channel = 0; channel < channels; ++channel)
    {
        auto* output = destination.getWritePointer (channel);
        const auto* input = branch.getReadPointer (channel);
        for (int sample = 0; sample < samples; ++sample)
            output[sample] = a * output[sample] + b * input[sample];
    }
}

inline void applyStereoWidth (juce::AudioBuffer<float>& audio, float width) noexcept
{
    if (audio.getNumChannels() < 2) return;
    const auto safeWidth = boundedWidth (width);
    auto* left = audio.getWritePointer (0);
    auto* right = audio.getWritePointer (1);
    for (int sample = 0; sample < audio.getNumSamples(); ++sample)
    {
        const float mid = 0.5f * (left[sample] + right[sample]);
        const float side = 0.5f * (left[sample] - right[sample]) * safeWidth;
        left[sample] = mid + side;
        right[sample] = mid - side;
    }
}
}
