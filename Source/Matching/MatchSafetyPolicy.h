#pragma once

#include "../Engine/SynthEngine.h"
#include <cmath>

// Compatibility checks for generated state before it reaches APVTS/live DSP.
// This is intentionally conservative: malformed optional content is rejected,
// while ordinary parameter clamping remains the responsibility of the engine.
namespace MatchSafetyPolicy
{
inline bool finite (float value) noexcept { return std::isfinite (value); }

inline bool between (float value, float low, float high) noexcept
{
    return finite (value) && value >= low && value <= high;
}

inline bool compatibleTable (const std::shared_ptr<const ReferenceWavetableData>& table) noexcept
{
    if (table == nullptr) return true;
    if (! table->valid || ! between (table->fundamentalHz, 0.0f, 20000.0f)) return false;
    for (const auto& frame : table->frames)
        for (const auto sample : frame)
            if (! between (sample, -8.0f, 8.0f)) return false;
    return true;
}

inline bool compatible (const VoiceParameters& p, int depth = 0) noexcept
{
    if (depth > VoiceParameters::extraLayerCount) return false;
    if (! compatibleTable (p.referenceWavetable) || ! compatibleTable (p.userWavetable)) return false;

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

    if (! between (p.osc1Mix, 0.0f, 1.0f) || ! between (p.osc2Mix, 0.0f, 1.0f)
        || ! between (p.subMix, 0.0f, 1.0f) || ! between (p.noiseMix, 0.0f, 1.0f)
        || ! between (p.ringMix, 0.0f, 1.0f) || ! between (p.additiveMix, 0.0f, 1.0f)
        || ! between (p.wavetableMix, 0.0f, 1.0f) || ! between (p.referenceWavetableMix, 0.0f, 1.0f)
        || ! between (p.userWavetableMix, 0.0f, 1.0f) || ! between (p.supersawMix, 0.0f, 1.0f)
        || ! between (p.wavefold, 0.0f, 1.0f) || ! between (p.fmMix, 0.0f, 1.0f)
        || ! between (p.sustain, 0.0f, 1.0f) || ! between (p.cutoff, 20.0f, 20000.0f)
        || ! between (p.resonance, 0.0f, 1.0f) || ! between (p.stereoWidth, 0.0f, 2.0f)
        || ! between (p.mainLayerGain, 0.0f, 1.0f) || ! between (p.outputGainDb, -48.0f, 12.0f)
        || ! between (p.tempoBpm, 40.0f, 300.0f) || ! between (p.masterTuneCents, -1200.0f, 1200.0f)
        || ! between (p.attack, 0.0f, 8.0f) || ! between (p.decay, 0.0f, 8.0f)
        || ! between (p.release, 0.0f, 8.0f) || ! between (p.lfoRate, 0.0f, 30.0f)
        || ! between (p.lfoPitch, 0.0f, 2.0f) || ! between (p.lfoCutoff, 0.0f, 3.0f)
        || ! between (p.lfoAmp, 0.0f, 1.0f) || ! between (p.delayMix, 0.0f, 1.0f)
        || ! between (p.delayTime, 0.0f, 8.0f) || ! between (p.delayFeedback, 0.0f, 1.0f)
        || ! between (p.reverbMix, 0.0f, 1.0f) || ! between (p.reverbSize, 0.0f, 1.0f)
        || ! between (p.reverbDamping, 0.0f, 1.0f) || ! between (p.chorusMix, 0.0f, 1.0f)
        || ! between (p.chorusRate, 0.0f, 100.0f) || ! between (p.chorusDepth, 0.0f, 1.0f))
        return false;

    for (const auto value : p.layerGain) if (! between (value, 0.0f, 1.0f)) return false;
    for (const auto value : p.layerAmount) if (! between (value, 0.0f, 1.0f)) return false;
    for (const auto value : p.layerPan) if (! between (value, -1.0f, 1.0f)) return false;
    for (const auto value : p.layerTune) if (! between (value, -48.0f, 48.0f)) return false;
    for (const auto value : p.mseg.levels) if (! between (value, 0.0f, 1.0f)) return false;
    for (const auto value : p.mseg.times) if (! between (value, 0.0f, 8.0f)) return false;
    if (p.mseg.loopStartPoint < 0 || p.mseg.loopStartPoint >= MsegParameters::pointCount - 1
        || p.mseg.loopEndPoint <= p.mseg.loopStartPoint || p.mseg.loopEndPoint >= MsegParameters::pointCount)
        return false;

    if (p.osc1Wave < 0 || p.osc1Wave > 4 || p.osc2Wave < 0 || p.osc2Wave > 4
        || p.filterType < 0 || p.filterType > 2 || p.fmAlgorithm < 0 || p.fmAlgorithm > 5
        || p.msegTarget < 0 || p.msegTarget > (int) ModDestination::wavefold)
        return false;
    for (const auto& slot : p.modSlots)
        if (slot.source < (int) ModSource::none || slot.source > (int) ModSource::lfo4
            || slot.destination < (int) ModDestination::none || slot.destination > (int) ModDestination::wavefold
            || ! between (slot.amount, -1.0f, 1.0f)) return false;
    for (const auto& slot : p.modGraphSlots)
        if (slot.source < (int) ModSource::none || slot.source > (int) ModSource::lfo4
            || slot.destination < (int) ModDestination::none || slot.destination > (int) ModDestination::wavefold
            || ! between (slot.amount, -1.0f, 1.0f)) return false;
    for (const auto& slot : p.moduleModSlots)
        if (slot.source < (int) ModSource::none || slot.source > (int) ModSource::lfo4
            || slot.destination < (int) ModDestination::none || slot.destination > (int) ModDestination::wavefold
            || ! between (slot.amount, -1.0f, 1.0f)) return false;
    for (const auto mode : p.fmOpFixedMode) if (mode < 0 || mode > 1) return false;
    for (const auto shape : p.extraLfoShape) if (shape < 0 || shape > 3) return false;
    for (const auto operation : p.layerOperation) if (operation < 0 || operation > 4) return false;
    for (const auto& module : p.fxModules)
        if (module.type < 0 || module.type >= (int) fxModuleCatalog.size()
            || module.stage < 0 || module.stage > 1
            || ! finite (module.amount) || ! finite (module.rate)
            || ! finite (module.feedback) || ! finite (module.mix))
            return false;
        else if (! between (module.amount, 0.0f, 1.0f) || ! between (module.rate, 0.0f, 1.0f)
                 || ! between (module.feedback, 0.0f, 1.0f) || ! between (module.mix, 0.0f, 1.0f))
            return false;
        else if (module.tempoDivision < 0 || module.tempoDivision >= (int) TempoSync::divisionNames.size())
            return false;
    for (const auto& module : p.globalFxModules)
        if (module.type < 0 || module.type >= (int) fxModuleCatalog.size()
            || module.stage < 0 || module.stage > 1
            || ! finite (module.amount) || ! finite (module.rate)
            || ! finite (module.feedback) || ! finite (module.mix))
            return false;
        else if (! between (module.amount, 0.0f, 1.0f) || ! between (module.rate, 0.0f, 1.0f)
                 || ! between (module.feedback, 0.0f, 1.0f) || ! between (module.mix, 0.0f, 1.0f))
            return false;
        else if (module.tempoDivision < 0 || module.tempoDivision >= (int) TempoSync::divisionNames.size())
            return false;
    for (size_t i = 0; i < p.layers.size(); ++i)
    {
        if (p.layers[i] == nullptr)
        {
            if (p.layerOperation[i] != 0)
                return false;
            continue;
        }
        if (! compatible (*p.layers[i], depth + 1)) return false;
    }
    return true;
}
}
