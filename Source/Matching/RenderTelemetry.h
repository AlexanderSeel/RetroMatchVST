#pragma once
#include <JuceHeader.h>
#include <cmath>
#include <cstdint>

// Lightweight technical measurements for offline candidate/debug renders.
// This deliberately keeps measurement separate from musical similarity while
// providing a deterministic safety factor that candidate selection can apply.
struct RenderTelemetry
{
    float peak = 0.0f;
    float rms = 0.0f;
    float dc = 0.0f;
    float crestFactor = 0.0f;
    float clipThreshold = 1.0f;
    std::int64_t finiteSamples = 0;
    std::int64_t nonFiniteSamples = 0;
    std::int64_t clippedSamples = 0;

    [[nodiscard]] std::int64_t totalSamples() const noexcept
    {
        return finiteSamples + nonFiniteSamples;
    }

    [[nodiscard]] bool isFinite() const noexcept
    {
        return nonFiniteSamples == 0
            && std::isfinite (peak)
            && std::isfinite (rms)
            && std::isfinite (dc)
            && std::isfinite (crestFactor)
            && std::isfinite (clipThreshold);
    }

    [[nodiscard]] bool hasHardClipping() const noexcept
    {
        return clippedSamples > 0;
    }

    [[nodiscard]] bool isTechnicallySafe() const noexcept
    {
        return finiteSamples > 0 && isFinite() && ! hasHardClipping();
    }

    // Technical safety is deliberately independent from perceptual similarity.
    // Non-finite/empty renders are invalid. Above-threshold finite renders are
    // penalized by both peak overshoot and the fraction of affected samples.
    [[nodiscard]] float technicalSafetyScore() const noexcept
    {
        if (finiteSamples <= 0 || ! isFinite())
            return 0.0f;

        if (! hasHardClipping())
            return 1.0f;

        if (clipThreshold <= 0.0f)
            return 0.0f;

        const float peakSafety = juce::jlimit (0.0f, 1.0f, clipThreshold / juce::jmax (clipThreshold, peak));
        const float clippedRatio = juce::jlimit (0.0f, 1.0f,
            static_cast<float> (clippedSamples) / static_cast<float> (finiteSamples));
        return juce::jlimit (0.0f, 1.0f, peakSafety * (1.0f - clippedRatio));
    }

    [[nodiscard]] static RenderTelemetry analyze (const juce::AudioBuffer<float>& audio,
                                                   float clipThreshold = 1.0f) noexcept
    {
        RenderTelemetry result;
        double sum = 0.0;
        double sumSquares = 0.0;
        clipThreshold = juce::jmax (0.0f, clipThreshold);
        result.clipThreshold = clipThreshold;

        for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        {
            const auto* samples = audio.getReadPointer (channel);
            for (int sampleIndex = 0; sampleIndex < audio.getNumSamples(); ++sampleIndex)
            {
                const float sample = samples[sampleIndex];
                if (! std::isfinite (sample))
                {
                    ++result.nonFiniteSamples;
                    continue;
                }

                ++result.finiteSamples;
                const float magnitude = std::abs (sample);
                result.peak = juce::jmax (result.peak, magnitude);
                if (magnitude > clipThreshold)
                    ++result.clippedSamples;

                sum += static_cast<double> (sample);
                sumSquares += static_cast<double> (sample) * static_cast<double> (sample);
            }
        }

        if (result.finiteSamples > 0)
        {
            const double count = static_cast<double> (result.finiteSamples);
            result.dc = static_cast<float> (sum / count);
            result.rms = static_cast<float> (std::sqrt (sumSquares / count));
            if (result.rms > 1.0e-12f)
                result.crestFactor = result.peak / result.rms;
        }

        return result;
    }
};
