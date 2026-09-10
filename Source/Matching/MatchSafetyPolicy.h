#pragma once

#include "../Engine/SynthEngine.h"
#include <cmath>

// Compatibility checks for generated state before it reaches APVTS/live DSP.
// This is intentionally conservative: malformed optional content is rejected,
// while ordinary parameter clamping remains the responsibility of the engine.
namespace MatchSafetyPolicy
{
inline bool finite (float value) noexcept { return std::isfinite (value); }

inline bool compatible (const VoiceParameters& p, int depth = 0) noexcept
{
    if (depth > VoiceParameters::extraLayerCount) return false;

    const float values[] { p.osc1Mix, p.osc2Mix, p.subMix, p.noiseMix, p.ringMix, p.additiveMix,
        p.masterTuneCents, p.osc2Semitones, p.osc2Detune, p.pulseWidth, p.wavetableMix,
        p.wavetablePosition, p.wavetableWarp, p.referenceWavetableMix, p.userWavetableMix,
        p.supersawMix, p.unisonDetune, p.unisonSpread, p.wavefold, p.fmAmount, p.fmRatio,
        p.fmMix, p.fmFeedback, p.harmonicTilt, p.oddEvenBalance, p.attack, p.decay,
        p.sustain, p.release, p.cutoff, p.resonance, p.lfoRate, p.lfoPitch, p.lfoCutoff,
        p.lfoAmp, p.msegDepth, p.tempoBpm, p.chorusMix, p.chorusRate, p.chorusDepth,
        p.delayMix, p.delayTime, p.delayFeedback, p.reverbMix, p.reverbSize, p.reverbDamping,
        p.stereoWidth, p.outputGainDb, p.mainLayerGain };
    for (const auto value : values)
        if (! finite (value)) return false;

    for (const auto value : p.fmOpRatio) if (! finite (value)) return false;
    for (const auto value : p.fmOpLevel) if (! finite (value)) return false;
    for (const auto value : p.fmOpFixedHz) if (! finite (value)) return false;
    for (const auto value : p.fmOpAttack) if (! finite (value)) return false;
    for (const auto value : p.fmOpDecay) if (! finite (value)) return false;
    for (const auto value : p.fmOpSustain) if (! finite (value)) return false;
    for (const auto value : p.fmOpRelease) if (! finite (value)) return false;
    for (const auto value : p.fmOpKeyScale) if (! finite (value)) return false;
    for (const auto value : p.fmOpVelocity) if (! finite (value)) return false;
    for (const auto value : p.extraLfoRate) if (! finite (value)) return false;
    for (const auto value : p.layerGain) if (! finite (value)) return false;
    for (const auto value : p.layerPan) if (! finite (value)) return false;
    for (const auto value : p.layerTune) if (! finite (value)) return false;
    for (const auto value : p.layerAmount) if (! finite (value)) return false;
    for (const auto& time : p.mseg.times) if (! finite (time)) return false;
    for (const auto& level : p.mseg.levels) if (! finite (level)) return false;

    if (p.filterType < 0 || p.filterType > 2 || p.fmAlgorithm < 0 || p.fmAlgorithm > 5
        || p.msegTarget < 0 || p.msegTarget > (int) ModDestination::wavefold)
        return false;
    for (const auto& module : p.fxModules)
        if (module.type < 0 || module.type >= (int) fxModuleCatalog.size()
            || module.stage < 0 || module.stage > 1
            || ! finite (module.amount) || ! finite (module.rate)
            || ! finite (module.feedback) || ! finite (module.mix))
            return false;
    for (const auto& module : p.globalFxModules)
        if (module.type < 0 || module.type >= (int) fxModuleCatalog.size()
            || module.stage < 0 || module.stage > 1
            || ! finite (module.amount) || ! finite (module.rate)
            || ! finite (module.feedback) || ! finite (module.mix))
            return false;
    for (const auto& layer : p.layers)
        if (layer != nullptr && ! compatible (*layer, depth + 1)) return false;
    return true;
}
}
