#pragma once
#include <JuceHeader.h>
#include "../Analysis/SampleAnalyzer.h"
#include <array>

struct ResynthesisAdvice
{
    int method = 0;       // MatchSettings::algorithm
    int complexity = 0;   // resynthComplexity choice
    float matchability = 0.0f;
    juce::String sourceFamily;
    juce::String reason;

    juce::String methodName() const
    {
        static const juce::StringArray names { "Balanced Hybrid", "Reference Wavetable", "Spectral Subtractive",
                                                "FM / Harmonic", "Layered Studio", "Texture / Chop", "FX / Guitar Chain" };
        return names[juce::jlimit (0, names.size() - 1, method)];
    }
};

class ResynthesisAdvisor
{
public:
    static ResynthesisAdvice advise (const SoundFeatures& f)
    {
        ResynthesisAdvice a;
        const float pitch = juce::jlimit (0.0f, 1.0f, f.pitchConfidence);
        const float harmonic = juce::jlimit (0.0f, 1.0f, f.harmonicity);
        const float flat = juce::jlimit (0.0f, 1.0f, f.spectralFlatness);
        const float transient = juce::jlimit (0.0f, 1.0f, f.transientScore);
        const float motion = juce::jlimit (0.0f, 1.0f, f.spectralMotion * 2.5f);
        const float stereo = juce::jlimit (0.0f, 1.0f, f.stereoWidth / 1.35f);
        const float inharmonic = juce::jlimit (0.0f, 1.0f, f.inharmonicity * 2.0f);
        const float tail = juce::jlimit (0.0f, 1.0f, f.releaseSeconds / juce::jmax (0.18f, juce::jmin (2.5f, f.duration + 0.1f)) * 1.8f);

        const bool guitarLike = pitch > 0.30f && transient > 0.34f && harmonic > 0.26f && flat < 0.50f
                             && f.spectralCentroidHz > 350.0f && f.spectralCentroidHz < 6500.0f
                             && f.spectralRolloffHz > 1200.0f && f.spectralRolloffHz < 16000.0f;

        std::array<float, 7> score {};
        score[0] = 0.46f + pitch * 0.10f + harmonic * 0.08f + transient * 0.05f;
        score[1] = 0.16f + pitch * 0.36f + harmonic * 0.25f + (1.0f - flat) * 0.11f + motion * 0.08f;
        score[2] = 0.18f + pitch * 0.15f + harmonic * 0.18f + (1.0f - motion) * 0.18f + (1.0f - flat) * 0.12f;
        score[3] = 0.14f + pitch * 0.12f + harmonic * 0.28f + inharmonic * 0.24f + f.highEnergyRatio * 0.16f;
        score[4] = 0.20f + pitch * 0.10f + motion * 0.24f + stereo * 0.28f + tail * 0.14f;
        score[5] = 0.14f + flat * 0.22f + motion * 0.30f + (1.0f - pitch) * 0.18f + inharmonic * 0.10f;
        score[6] = 0.15f + pitch * 0.15f + transient * 0.25f + (1.0f - flat) * 0.11f
                         + inharmonic * 0.12f + tail * 0.11f + stereo * 0.08f;
        if (guitarLike) score[6] += 0.24f;

        for (int i = 1; i < (int) score.size(); ++i)
            if (score[(size_t) i] > score[(size_t) a.method]) a.method = i;

        float complexity = motion * 0.95f + stereo * 0.65f + inharmonic * 0.42f + tail * 0.72f
                         + juce::jlimit (0.0f, 0.45f, f.duration / 8.0f);
        a.complexity = complexity > 1.58f ? 3 : (complexity > 1.05f ? 2 : (complexity > 0.55f ? 1 : 0));
        if (a.method == 6) a.complexity = juce::jmax (1, a.complexity);

        const float structure = juce::jmax (pitch, juce::jmax (harmonic, transient));
        a.matchability = juce::jlimit (0.30f, 0.97f,
                                       0.38f + structure * 0.28f + (1.0f - flat) * 0.10f
                                     + juce::jmin (0.12f, motion * 0.12f) + juce::jmin (0.09f, stereo * 0.09f));

        if (guitarLike)
            a.sourceFamily = (inharmonic > 0.28f || tail > 0.22f || stereo > 0.18f)
                               ? "processed pluck / guitar-like" : "clean pluck / guitar-like";
        else if (pitch < 0.22f && flat > 0.42f)
            a.sourceFamily = "noise / texture";
        else if (transient > 0.58f && harmonic < 0.42f)
            a.sourceFamily = "percussive / transient";
        else if (motion > 0.35f)
            a.sourceFamily = "evolving pitched texture";
        else
            a.sourceFamily = "pitched harmonic source";

        switch (a.method)
        {
            case 1: a.reason = "Stable pitch and harmonic structure make cycles extracted from the reference especially useful."; break;
            case 2: a.reason = "The spectrum is comparatively stable, so oscillator plus filter-envelope matching should stay efficient."; break;
            case 3: a.reason = "Harmonic/inharmonic structure favours explicit six-operator reconstruction."; break;
            case 4: a.reason = "Stereo width and spectral motion benefit from complementary layered roles."; break;
            case 5: a.reason = "Texture and time-varying spectrum are stronger than one stable harmonic body."; break;
            case 6: a.reason = "A pitched transient body plus band-limited colour/tail is guitar-like; rebuild the body and search compressor, drive, cabinet filtering, modulation, delay and reverb as an ordered chain."; break;
            default: a.reason = "No single topology dominates; use the hybrid search as the safest first pass."; break;
        }
        return a;
    }
};
