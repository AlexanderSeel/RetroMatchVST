#pragma once
#include "../Engine/SynthEngine.h"
#include "../Analysis/SampleAnalyzer.h"
#include <cmath>

// Musical residual corrections applied on top of a measured candidate. Compare is a
// corrective surface, not a second synthesizer: each control is deliberately narrow,
// bounded and deterministic so its label predicts the audible change and RESET always
// returns to the measured baseline.
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

    bool nearlyEquals (const Values& other, float epsilon = 1.0e-5f) const noexcept
    {
        return std::abs (brightness - other.brightness) <= epsilon
            && std::abs (lowEnd - other.lowEnd) <= epsilon
            && std::abs (punch - other.punch) <= epsilon
            && std::abs (tail - other.tail) <= epsilon
            && std::abs (width - other.width) <= epsilon
            && std::abs (motion - other.motion) <= epsilon
            && std::abs (finePitch - other.finePitch) <= epsilon;
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

    // BRIGHTNESS is only a spectral-envelope tilt through the existing filter. Do not
    // add wavefold, FM or drive here: those would make "brighter" mean "more distorted".
    p.cutoff = juce::jlimit (20.0f, 20000.0f,
                             p.cutoff * signedScale (values.brightness, 1.25f));

    // LOW END moves the main voice's existing sub balance. Companion layers are handled
    // separately below so a full GOLD rack does not gain seven additional sub sources.
    p.subMix = juce::jlimit (0.0f, 0.65f, p.subMix + values.lowEnd * 0.12f);

    // PUNCH means onset speed, not loudness, clipping or drive. Keep FM attacks coherent.
    const float attackScale = signedScale (-values.punch, 1.50f);
    p.attack = juce::jlimit (0.001f, 5.0f, p.attack * attackScale);
    for (auto& attack : p.fmOpAttack)
        attack = juce::jlimit (0.001f, 5.0f, attack * attackScale);

    // TAIL means time only. In particular, never raise sustain: doing so can turn a
    // naturally decaying/one-shot match back into a held tone while the note is down.
    const float tailScale = signedScale (values.tail, 1.15f);
    p.decay = juce::jlimit (0.001f, 5.0f, p.decay * tailScale);
    p.release = juce::jlimit (0.001f, 8.0f, p.release * tailScale);
    for (auto& decay : p.fmOpDecay)
        decay = juce::jlimit (0.001f, 5.0f, decay * tailScale);
    for (auto& release : p.fmOpRelease)
        release = juce::jlimit (0.001f, 8.0f, release * tailScale);

    // WIDTH changes spatial width only. Unison spread was intentionally removed from
    // this macro because changing detune changes pitch/timbre as well as image width.
    p.stereoWidth = juce::jlimit (0.25f, 2.5f,
                                  p.stereoWidth * signedScale (values.width, 0.45f));

    // MOTION scales modulation topology that is already present; it never invents one.
    scaleExistingMotion (p, signedScale (values.motion, 0.85f));

    // FINE PITCH remains a true whole-instrument residual tuning correction: +/-50 cents.
    p.masterTuneCents = juce::jlimit (-100.0f, 100.0f,
                                      p.masterTuneCents + values.finePitch * 50.0f);
}

inline Values companionValues (Values values) noexcept
{
    values.clamp();
    // A full-rack correction should sound like one instrument moving, not the same macro
    // being multiplied by every layer. Tuning remains full-strength so layers stay in tune.
    values.brightness *= 0.55f;
    values.lowEnd = 0.0f;
    values.punch *= 0.65f;
    values.tail *= 0.65f;
    values.width *= 0.55f;
    values.motion *= 0.65f;
    return values;
}

inline Values suggestFromResidual (const SoundFeatures& reference, const SoundFeatures& candidate,
                                   const VoiceParameters& baseline, float maxStep = 0.24f) noexcept
{
    Values v;
    maxStep = juce::jlimit (0.02f, 0.40f, maxStep);
    auto limit = [maxStep] (float x) { return juce::jlimit (-maxStep, maxStep, x); };
    auto logRatio = [] (float target, float current) noexcept
    {
        if (target <= 1.0e-5f || current <= 1.0e-5f) return 0.0f;
        return std::log2 (target / current);
    };

    // Residual directions mirror the actual macro ranges above.
    v.brightness = limit (logRatio (reference.spectralCentroidHz, candidate.spectralCentroidHz) / 1.25f);
    v.lowEnd = limit ((reference.lowEnergyRatio - candidate.lowEnergyRatio) * 2.6f);
    v.punch = limit (logRatio (candidate.attackSeconds, reference.attackSeconds) / 1.50f);

    // Tail no longer changes sustain, therefore Auto Nudge must not use sustain mismatch
    // to request a correction the knob cannot legitimately perform.
    const float tailResidual = 0.58f * logRatio (reference.releaseSeconds, candidate.releaseSeconds)
                             + 0.42f * logRatio (reference.decaySeconds, candidate.decaySeconds);
    v.tail = limit (tailResidual / 1.15f);
    v.width = limit ((reference.stereoWidth - candidate.stereoWidth) * 0.85f);

    bool hasExistingMotion = std::abs (baseline.lfoPitch) > 1.0e-5f || std::abs (baseline.lfoCutoff) > 1.0e-5f
                          || std::abs (baseline.lfoAmp) > 1.0e-5f || std::abs (baseline.msegDepth) > 1.0e-5f;
    for (const auto& slot : baseline.modSlots) hasExistingMotion |= std::abs (slot.amount) > 1.0e-5f;
    for (const auto& slot : baseline.modGraphSlots) hasExistingMotion |= std::abs (slot.amount) > 1.0e-5f;
    for (const auto& slot : baseline.moduleModSlots) hasExistingMotion |= std::abs (slot.amount) > 1.0e-5f;
    if (hasExistingMotion)
        v.motion = limit ((reference.spectralMotion - candidate.spectralMotion) * 2.0f);

    if (reference.fundamentalHz > 20.0f && candidate.fundamentalHz > 20.0f
        && reference.pitchConfidence > 0.20f && candidate.pitchConfidence > 0.20f)
    {
        const float cents = 1200.0f * logRatio (reference.fundamentalHz, candidate.fundamentalHz);
        v.finePitch = limit (cents / 50.0f);
    }
    v.clamp();
    return v;
}

inline VoiceParameters apply (const VoiceParameters& baseline, Values values)
{
    values.clamp();
    auto adjusted = baseline;
    applyToVoice (adjusted, values);

    // GOLD/full-rack candidates remain immutable in the bank. Clone companions, but use
    // reduced correction strength so one knob remains one perceptual whole-instrument move.
    const auto layerValues = companionValues (values);
    for (size_t i = 0; i < adjusted.layers.size(); ++i)
    {
        if (! baseline.layers[i]) continue;
        auto layer = std::make_shared<VoiceParameters> (*baseline.layers[i]);
        layer->layers.fill (nullptr);
        applyToVoice (*layer, layerValues);
        adjusted.layers[i] = std::move (layer);
    }

    if (std::abs (values.width) > 1.0e-6f)
    {
        const float panScale = juce::jlimit (0.65f, 1.35f, 1.0f + values.width * 0.30f);
        for (auto& pan : adjusted.layerPan)
            pan = juce::jlimit (-1.0f, 1.0f, pan * panScale);
    }
    return adjusted;
}
}
