#pragma once
#include "../Engine/SynthEngine.h"
#include <cmath>

// Musical residual corrections applied on top of a measured candidate. These mappings
// are deliberately bounded and deterministic so Compare can always return to baseline.
namespace CompareFineTune
{
struct Values
{
    float brightness = 0.0f;
    float lowEnd = 0.0f;
    float punch = 0.0f;
    float tail = 0.0f;
    float width = 0.0f;
    float motion = 0.0f;
    float finePitch = 0.0f;

    void clamp() noexcept
    {
        brightness = juce::jlimit (-1.0f, 1.0f, brightness);
        lowEnd = juce::jlimit (-1.0f, 1.0f, lowEnd);
        punch = juce::jlimit (-1.0f, 1.0f, punch);
        tail = juce::jlimit (-1.0f, 1.0f, tail);
        width = juce::jlimit (-1.0f, 1.0f, width);
        motion = juce::jlimit (-1.0f, 1.0f, motion);
        finePitch = juce::jlimit (-1.0f, 1.0f, finePitch);
    }

    bool isNeutral (float epsilon = 1.0e-6f) const noexcept
    {
        return std::abs (brightness) <= epsilon && std::abs (lowEnd) <= epsilon
            && std::abs (punch) <= epsilon && std::abs (tail) <= epsilon
            && std::abs (width) <= epsilon && std::abs (motion) <= epsilon
            && std::abs (finePitch) <= epsilon;
    }
};

inline float signedScale (float value, float octaves) noexcept
{
    return std::exp2 (juce::jlimit (-1.0f, 1.0f, value) * octaves);
}

inline void scaleExistingMotion (VoiceParameters& p, float scale) noexcept
{
    p.lfoPitch = juce::jlimit (-1.0f, 1.0f, p.lfoPitch * scale);
    p.lfoCutoff = juce::jlimit (-1.0f, 1.0f, p.lfoCutoff * scale);
    p.lfoAmp = juce::jlimit (-1.0f, 1.0f, p.lfoAmp * scale);
    p.msegDepth = juce::jlimit (-1.0f, 1.0f, p.msegDepth * scale);

    for (auto& slot : p.modSlots)
        slot.amount = juce::jlimit (-1.0f, 1.0f, slot.amount * scale);
    for (auto& slot : p.modGraphSlots)
        slot.amount = juce::jlimit (-1.0f, 1.0f, slot.amount * scale);
    for (auto& slot : p.moduleModSlots)
        slot.amount = juce::jlimit (-1.0f, 1.0f, slot.amount * scale);
}

inline void applyToVoice (VoiceParameters& p, Values values) noexcept
{
    values.clamp();

    // Brightness is logarithmic because filter cutoff is perceived roughly by octaves.
    p.cutoff = juce::jlimit (20.0f, 20000.0f, p.cutoff * signedScale (values.brightness, 1.75f));

    // Low-end is deliberately conservative: it moves the sub balance without changing
    // graph/layer topology or introducing another synth instance.
    p.subMix = juce::jlimit (0.0f, 0.65f, p.subMix + values.lowEnd * 0.20f);

    // Positive PUNCH means a faster onset. Keep FM envelopes coherent with the amp envelope.
    const float attackScale = signedScale (-values.punch, 2.25f);
    p.attack = juce::jlimit (0.001f, 5.0f, p.attack * attackScale);
    for (auto& attack : p.fmOpAttack)
        attack = juce::jlimit (0.001f, 5.0f, attack * attackScale);

    // TAIL changes musical length, not ambience. Space/FX remain independently editable.
    const float tailScale = signedScale (values.tail, 1.65f);
    p.decay = juce::jlimit (0.001f, 5.0f, p.decay * tailScale);
    p.release = juce::jlimit (0.001f, 8.0f, p.release * tailScale);
    p.sustain = juce::jlimit (0.0f, 1.0f, p.sustain + values.tail * 0.10f);
    for (auto& decay : p.fmOpDecay)
        decay = juce::jlimit (0.001f, 5.0f, decay * tailScale);
    for (auto& release : p.fmOpRelease)
        release = juce::jlimit (0.001f, 8.0f, release * tailScale);

    // Width stays bounded and only expands resources already present in the patch.
    p.stereoWidth = juce::jlimit (0.25f, 2.5f, p.stereoWidth * signedScale (values.width, 0.65f));
    p.unisonSpread = juce::jlimit (0.0f, 1.0f, p.unisonSpread + values.width * 0.22f);

    // Motion scales existing modulation only. A neutral/static patch is not silently given a new LFO.
    scaleExistingMotion (p, signedScale (values.motion, 1.25f));

    // Fine pitch is intentionally limited to one semitone total range.
    p.masterTuneCents = juce::jlimit (-100.0f, 100.0f, p.masterTuneCents + values.finePitch * 50.0f);
}

inline VoiceParameters apply (const VoiceParameters& baseline, Values values)
{
    values.clamp();
    auto adjusted = baseline;
    applyToVoice (adjusted, values);

    // Gold/full-rack candidates must move as one instrument. Clone immutable layers so the
    // candidate bank remains a true analysis baseline and RESET is bit-for-bit reproducible.
    for (size_t i = 0; i < adjusted.layers.size(); ++i)
    {
        if (! baseline.layers[i]) continue;
        auto layer = std::make_shared<VoiceParameters> (*baseline.layers[i]);
        layer->layers.fill (nullptr);
        applyToVoice (*layer, values);
        adjusted.layers[i] = std::move (layer);
    }

    if (std::abs (values.width) > 1.0e-6f)
    {
        const float panScale = juce::jlimit (0.35f, 1.65f, 1.0f + values.width * 0.45f);
        for (auto& pan : adjusted.layerPan)
            pan = juce::jlimit (-1.0f, 1.0f, pan * panScale);
    }
    return adjusted;
}
}
