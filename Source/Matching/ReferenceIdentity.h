#pragma once

#include "../Analysis/SampleAnalyzer.h"

namespace ReferenceIdentity
{
enum class Lifecycle
{
    oneShot,
    plucked,
    gated,
    sustained,
    evolving
};

enum class PitchModel
{
    unpitched,
    harmonic,
    inharmonicPitched
};

struct Profile
{
    Lifecycle lifecycle = Lifecycle::sustained;
    PitchModel pitchModel = PitchModel::unpitched;
    float pitchReliability = 0.0f;
    float tonalConfidence = 0.0f;
    float noiseConfidence = 0.0f;

    [[nodiscard]] bool selfTerminates() const noexcept
    {
        return lifecycle == Lifecycle::oneShot
            || lifecycle == Lifecycle::plucked
            || lifecycle == Lifecycle::gated;
    }
};

inline Profile classify (const SoundFeatures& f) noexcept
{
    Profile result;

    const float pitch = juce::jlimit (0.0f, 1.0f, f.pitchConfidence);
    const float harmonic = juce::jlimit (0.0f, 1.0f, f.harmonicity);
    const float inharmonic = juce::jlimit (0.0f, 1.0f, f.inharmonicity);
    const float flat = juce::jlimit (0.0f, 1.0f, f.spectralFlatness);
    const float transient = juce::jlimit (0.0f, 1.0f, f.transientScore);
    const float sustain = juce::jlimit (0.0f, 1.0f, f.sustainLevel);
    const float motion = juce::jlimit (0.0f, 1.0f, f.spectralMotion * 2.5f);

    result.tonalConfidence = juce::jlimit (0.0f, 1.0f,
        pitch * 0.48f + harmonic * 0.37f + (1.0f - flat) * 0.15f);
    result.noiseConfidence = juce::jlimit (0.0f, 1.0f,
        flat * 0.52f + (1.0f - harmonic) * 0.30f + (1.0f - pitch) * 0.18f);

    // Pitch confidence alone is not enough evidence for a harmonic oscillator
    // model: mallets and metallic percussion can expose a stable perceived pitch
    // while their partials remain substantially inharmonic.
    result.pitchReliability = juce::jlimit (0.0f, 1.0f,
        pitch * (0.62f + harmonic * 0.38f) * (1.0f - inharmonic * 0.28f));

    if (pitch < 0.24f || result.noiseConfidence > result.tonalConfidence + 0.18f)
        result.pitchModel = PitchModel::unpitched;
    else if (inharmonic > 0.24f && pitch > 0.34f)
        result.pitchModel = PitchModel::inharmonicPitched;
    else
        result.pitchModel = PitchModel::harmonic;

    float earlyEnergy = 0.0f;
    for (int i = 0; i < SoundFeatures::temporalFrameCount / 2; ++i)
        earlyEnergy = juce::jmax (earlyEnergy, f.temporalRms[(size_t) i]);
    const float lateEnergy = 0.5f * (f.temporalRms[(size_t) SoundFeatures::temporalFrameCount - 2]
                                   + f.temporalRms[(size_t) SoundFeatures::temporalFrameCount - 1]);
    const bool clearDecay = earlyEnergy > 1.0e-4f
                         && lateEnergy < juce::jmax (0.08f, earlyEnergy * 0.30f);

    if (motion > 0.34f && sustain > 0.30f && f.duration > 0.55f)
        result.lifecycle = Lifecycle::evolving;
    else if ((f.duration < 0.55f && sustain < 0.38f)
          || (transient > 0.72f && sustain < 0.34f && clearDecay))
        result.lifecycle = Lifecycle::oneShot;
    else if (result.pitchModel != PitchModel::unpitched
          && transient > 0.34f && sustain < 0.60f && clearDecay)
        result.lifecycle = Lifecycle::plucked;
    else if (clearDecay && sustain < 0.55f && f.duration < 3.0f)
        result.lifecycle = Lifecycle::gated;
    else
        result.lifecycle = Lifecycle::sustained;

    return result;
}

inline const char* lifecycleName (Lifecycle lifecycle) noexcept
{
    switch (lifecycle)
    {
        case Lifecycle::oneShot: return "one-shot";
        case Lifecycle::plucked: return "plucked/decaying";
        case Lifecycle::gated: return "gated";
        case Lifecycle::evolving: return "evolving";
        default: return "sustained";
    }
}

inline const char* pitchModelName (PitchModel pitchModel) noexcept
{
    switch (pitchModel)
    {
        case PitchModel::harmonic: return "harmonic pitched";
        case PitchModel::inharmonicPitched: return "inharmonic pitched";
        default: return "unpitched/noise";
    }
}
}
