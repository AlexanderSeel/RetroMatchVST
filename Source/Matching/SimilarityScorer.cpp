#include "SimilarityScorer.h"
#include <cmath>

namespace
{
float linearSimilarity (float a, float b, float scale)
{
    return juce::jlimit (0.0f, 1.0f, 1.0f - std::abs (a - b) / juce::jmax (scale, 1.0e-6f));
}

float logSimilarity (float a, float b, float octavesForZero)
{
    if (a <= 1.0e-4f || b <= 1.0e-4f) return (a <= 1.0e-4f && b <= 1.0e-4f) ? 1.0f : 0.0f;
    const float octaves = std::abs (std::log2 (a / b));
    return juce::jlimit (0.0f, 1.0f, 1.0f - octaves / octavesForZero);
}

float timeSimilarity (float a, float b)
{
    return logSimilarity (a + 0.008f, b + 0.008f, 3.0f);
}
}

SimilarityBreakdown SimilarityScorer::compare (const SoundFeatures& r, const SoundFeatures& c)
{
    SimilarityBreakdown b;

    float bandError = 0.0f;
    for (int i = 0; i < SoundFeatures::spectralBandCount; ++i)
        bandError += std::abs (r.spectralBands[(size_t) i] - c.spectralBands[(size_t) i]);
    bandError /= SoundFeatures::spectralBandCount;
    b.spectrum = juce::jlimit (0.0f, 1.0f, 1.0f - bandError * 1.35f);

    float temporalBandError = 0.0f;
    float temporalEnvError = 0.0f;
    for (int frame = 0; frame < SoundFeatures::temporalFrameCount; ++frame)
    {
        temporalEnvError += std::abs (r.temporalRms[(size_t) frame] - c.temporalRms[(size_t) frame]);
        for (int band = 0; band < SoundFeatures::temporalBandCount; ++band)
            temporalBandError += std::abs (r.temporalSpectralBands[(size_t) frame][(size_t) band]
                                        - c.temporalSpectralBands[(size_t) frame][(size_t) band]);
    }
    temporalBandError /= (SoundFeatures::temporalFrameCount * SoundFeatures::temporalBandCount);
    temporalEnvError /= SoundFeatures::temporalFrameCount;
    const float motion = linearSimilarity (r.spectralMotion, c.spectralMotion, 0.55f);
    b.temporal = juce::jlimit (0.0f, 1.0f,
                              (1.0f - temporalBandError * 1.25f) * 0.68f
                            + (1.0f - temporalEnvError) * 0.22f
                            + motion * 0.10f);

    float cepstralError = 0.0f;
    for (int i = 0; i < SoundFeatures::cepstralCount; ++i)
    {
        const float weight = i == 0 ? 0.35f : 1.0f;
        cepstralError += std::abs (r.timbreCepstrum[(size_t) i] - c.timbreCepstrum[(size_t) i]) * weight;
    }
    cepstralError /= (SoundFeatures::cepstralCount - 0.65f);
    b.timbre = juce::jlimit (0.0f, 1.0f, 1.0f - cepstralError * 1.55f);

    const float centroid = logSimilarity (r.spectralCentroidHz + 20.0f, c.spectralCentroidHz + 20.0f, 2.5f);
    const float rolloff = logSimilarity (r.spectralRolloffHz + 20.0f, c.spectralRolloffHz + 20.0f, 2.5f);
    const float bandwidth = logSimilarity (r.spectralBandwidthHz + 20.0f, c.spectralBandwidthHz + 20.0f, 2.5f);
    const float flatness = linearSimilarity (r.spectralFlatness, c.spectralFlatness, 0.65f);
    const float highRatio = linearSimilarity (r.highEnergyRatio, c.highEnergyRatio, 0.45f);
    b.brightness = centroid * 0.30f + rolloff * 0.20f + bandwidth * 0.20f + flatness * 0.15f + highRatio * 0.15f;

    const float attack = timeSimilarity (r.attackSeconds, c.attackSeconds);
    const float decay = timeSimilarity (r.decaySeconds, c.decaySeconds);
    const float sustain = linearSimilarity (r.sustainLevel, c.sustainLevel, 0.75f);
    const float release = timeSimilarity (r.releaseSeconds, c.releaseSeconds);
    const float transient = linearSimilarity (r.transientScore, c.transientScore, 0.75f);
    b.envelope = attack * 0.30f + decay * 0.15f + sustain * 0.15f + release * 0.15f + transient * 0.25f;

    const float harmonicity = linearSimilarity (r.harmonicity, c.harmonicity, 0.75f);
    const float oddEven = linearSimilarity (r.oddHarmonicRatio, c.oddHarmonicRatio, 0.75f);
    const float zcr = linearSimilarity (r.zeroCrossingRate, c.zeroCrossingRate, 0.20f);
    const float inharmonicity = linearSimilarity (r.inharmonicity, c.inharmonicity, 0.55f);
    b.harmonic = harmonicity * 0.35f + oddEven * 0.25f + zcr * 0.15f + inharmonicity * 0.25f;

    b.pitch = (r.pitchConfidence < 0.25f || c.pitchConfidence < 0.25f)
                  ? 0.75f
                  : logSimilarity (r.fundamentalHz, c.fundamentalHz, 0.5f);
    b.stereo = linearSimilarity (r.stereoWidth, c.stereoWidth, 0.8f);

    b.total = juce::jlimit (0.0f, 1.0f,
                            b.spectrum * 0.25f
                          + b.temporal * 0.18f
                          + b.timbre * 0.12f
                          + b.brightness * 0.14f
                          + b.envelope * 0.14f
                          + b.harmonic * 0.10f
                          + b.pitch * 0.04f
                          + b.stereo * 0.03f);
    return b;
}
