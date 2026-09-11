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
        {
            const float destinationSample = std::isfinite (output[sample]) ? output[sample] : 0.0f;
            const float branchSample = std::isfinite (input[sample]) ? input[sample] : 0.0f;
            const float result = a * destinationSample + b * branchSample;
            output[sample] = std::isfinite (result) ? result : 0.0f;
        }
    }
}

// Allocation-free layer combine used by both ordinary and generated racks.
// Operation values match PatchGraph/SynthEngine's bounded combine contract:
// 0 additive, 1 replacement/crossfade, 2 subtract, 3 multiply, 4 safe divide.
inline void mixLayer (juce::AudioBuffer<float>& destination,
                      const juce::AudioBuffer<float>& layer,
                      float gain, float pan, float amount, int operation) noexcept
{
    const float safeGain = boundedGain (gain);
    const float safeAmount = juce::jlimit (0.0f, 1.0f, std::isfinite (amount) ? amount : 0.0f);
    const float safePan = boundedPan (pan);
    const int channels = juce::jmin (destination.getNumChannels(), layer.getNumChannels());
    const int samples = juce::jmin (destination.getNumSamples(), layer.getNumSamples());
    for (int channel = 0; channel < channels; ++channel)
    {
        const float balance = channel == 0 ? leftBalance (safePan) : rightBalance (safePan);
        const float scaledGain = safeGain * balance;
        auto* output = destination.getWritePointer (channel);
        const auto* input = layer.getReadPointer (channel);
        for (int sample = 0; sample < samples; ++sample)
        {
            const float a = std::isfinite (output[sample]) ? output[sample] : 0.0f;
            const float source = std::isfinite (input[sample]) ? input[sample] : 0.0f;
            const float b = source * scaledGain;
            float combined = a + b;
            switch (operation)
            {
                case 1: combined = b; break;
                case 2: combined = a - b; break;
                case 3: combined = a * b; break;
                case 4: combined = std::tanh (a * b / (b * b + 0.01f)); break;
                default: break;
            }
            const float result = a + safeAmount * (combined - a);
            output[sample] = std::isfinite (result) ? result : 0.0f;
        }
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
        const float l = std::isfinite (left[sample]) ? left[sample] : 0.0f;
        const float r = std::isfinite (right[sample]) ? right[sample] : 0.0f;
        const float mid = 0.5f * (l + r);
        const float side = 0.5f * (l - r) * safeWidth;
        const float newLeft = mid + side;
        const float newRight = mid - side;
        left[sample] = std::isfinite (newLeft) ? newLeft : 0.0f;
        right[sample] = std::isfinite (newRight) ? newRight : 0.0f;
    }
}
}
