#pragma once
#include <JuceHeader.h>
#include <array>

struct SoundFeatures
{
    static constexpr int spectralBandCount = 32;
    static constexpr int waveformPointCount = 256;
    static constexpr int temporalFrameCount = 8;
    static constexpr int temporalBandCount = 16;
    static constexpr int cepstralCount = 12;

    double sampleRate = 44100.0;
    float duration = 0.0f, rms = 0.0f, peak = 0.0f;
    float fundamentalHz = 0.0f, pitchConfidence = 0.0f;
    float spectralCentroidHz = 0.0f, spectralRolloffHz = 0.0f, spectralBandwidthHz = 0.0f;
    float spectralFlatness = 0.0f, oddHarmonicRatio = 0.5f;
    float lowEnergyRatio = 0.0f, highEnergyRatio = 0.0f;
    float zeroCrossingRate = 0.0f, harmonicity = 0.0f, inharmonicity = 0.0f, transientScore = 0.0f;
    float attackSeconds = 0.01f, decaySeconds = 0.2f, sustainLevel = 0.7f, releaseSeconds = 0.3f;
    float stereoWidth = 0.0f;
    float spectralMotion = 0.0f;

    std::array<float, spectralBandCount> spectralBands {};
    std::array<float, waveformPointCount> waveformPreview {};
    std::array<std::array<float, temporalBandCount>, temporalFrameCount> temporalSpectralBands {};
    std::array<float, temporalFrameCount> temporalRms {};
    std::array<float, cepstralCount> timbreCepstrum {};
};

class SampleAnalyzer
{
public:
    static std::optional<SoundFeatures> analyzeFile (const juce::File& file, float expectedFundamentalHz = 0.0f,
                                                     double startSeconds = 0.0, double endSeconds = -1.0);
    static SoundFeatures analyzeBuffer (const juce::AudioBuffer<float>& audio, double sampleRate, float expectedFundamentalHz = 0.0f);
};
