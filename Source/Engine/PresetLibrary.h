#pragma once
#include "SynthEngine.h"

struct FactoryPresetInfo { const char* name; const char* category; const char* description; };
inline constexpr std::array<FactoryPresetInfo, 10> factoryPresetCatalog {{
    { "Pure Sub", "Bass", "A clean sine foundation with a short, controlled release." },
    { "Copper Bass", "Bass", "Resonant square bass with envelope movement and pre-filter saturation." },
    { "Glass Keys", "Keys", "Six-operator FM with a bright attack and a small echo." },
    { "Plucked Wire", "Pluck", "Short harmonic pluck, stereo chorus and a restrained tail." },
    { "Slow Orbit", "Pad", "A moving wavetable scanned by an independent triangle LFO." },
    { "Wide Horizon", "Layered pad", "Two complementary synth instances: supersaw body and a quiet high shimmer." },
    { "Clockwork", "Sequence", "Square-wave gating from an independent LFO and pinging delay." },
    { "Dust Circuit", "Texture", "Noise, ring modulation and a bit-crushed post chain." },
    { "Liquid Lead", "Lead", "Expressive filter modulation with flanging and soft saturation." },
    { "Room Piano", "Keys", "Soft FM keys with compression and a short room." }
}};

inline VoiceParameters makeFactoryPreset (int index)
{
    VoiceParameters p; p.osc2Mix = 0; p.outputGainDb = -9; p.release = 0.2f;
    auto fx = [&p] (int slot, int type, float amount, float rate, float feedback, float mix, int stage = 0)
    { p.fxModules[(size_t) slot] = { type, stage, false, amount, rate, feedback, mix }; };
    switch (index)
    {
        case 0: p.osc1Wave = 0; p.osc1Mix = 0.95f; p.cutoff = 1200; p.attack = 0.004f; break;
        case 1:
            p.osc1Wave = 2; p.osc2Mix = 0.15f; p.cutoff = 900; p.resonance = 0.3f; p.decay = 0.16f; p.sustain = 0.45f;
            p.modSlots[0] = { (int) ModSource::ampEnvelope, (int) ModDestination::cutoff, 0.35f };
            fx (0, 3, 0.22f, 0.65f, 0.5f, 0.3f); break;
        case 2:
            p.osc1Mix = 0; p.fmMix = 0.7f; p.fmAlgorithm = 2; p.fmOpRatio[1] = 3.5f; p.fmOpLevel[1] = 0.65f;
            p.decay = 1.1f; p.sustain = 0.1f; p.release = 0.8f; fx (0, 8, 0.13f, 0.6f, 0.28f, 0.18f, 1); break;
        case 3:
            p.osc1Wave = 3; p.additiveMix = 0.35f; p.attack = 0.002f; p.decay = 0.22f; p.sustain = 0.04f;
            p.cutoff = 6500; fx (0, 6, 0.35f, 0.08f, 0.18f, 0.2f); break;
        case 4:
            p.osc1Mix = 0.2f; p.wavetableMix = 0.7f; p.attack = 0.65f; p.release = 1.8f; p.cutoff = 5800;
            p.extraLfoShape[0] = 1; p.extraLfoRate[0] = 0.18f;
            p.moduleModSlots[0] = { (int) ModSource::lfo2, (int) ModDestination::wavetablePosition, 0.6f };
            fx (0, 9, 0.65f, 0.65f, 0.9f, 0.28f, 1); break;
        case 5:
        {
            p.osc1Mix = 0.15f; p.supersawMix = 0.6f; p.attack = 0.4f; p.release = 1.4f; p.cutoff = 4500;
            auto shimmer = std::make_shared<VoiceParameters>(); shimmer->osc1Wave = 0; shimmer->osc2Mix = 0; shimmer->fmMix = 0.18f;
            shimmer->attack = 0.8f; shimmer->release = 1.6f; shimmer->outputGainDb = -12;
            p.layers[0] = shimmer; p.layerGain[0] = 0.45f; p.layerTune[0] = 12;
            fx (0, 6, 0.4f, 0.07f, 0.1f, 0.2f); break;
        }
        case 6:
            p.osc1Wave = 2; p.cutoff = 3200; p.extraLfoRate[0] = 4; p.extraLfoShape[0] = 2;
            p.moduleModSlots[0] = { (int) ModSource::lfo2, (int) ModDestination::amplitude, 0.9f };
            fx (0, 8, 0.12f, 0.6f, 0.35f, 0.25f, 1); break;
        case 7:
            p.osc1Mix = 0.35f; p.noiseMix = 0.1f; p.ringMix = 0.25f; p.osc2Semitones = 7; p.cutoff = 7000;
            fx (0, 10, 0.15f, 0.2f, 0.75f, 0.55f, 1); fx (1, 12, 0.55f, 0.38f, 0.5f, 0.2f, 1); break;
        case 8:
            p.osc1Mix = 0.55f; p.osc2Mix = 0.25f; p.osc2Detune = 7; p.cutoff = 2200; p.resonance = 0.25f;
            p.extraLfoRate[1] = 1.4f; p.moduleModSlots[0] = { (int) ModSource::lfo3, (int) ModDestination::cutoff, 0.3f };
            fx (0, 7, 0.35f, 0.09f, 0.35f, 0.25f); fx (1, 3, 0.18f, 0.7f, 0.5f, 0.25f, 1); break;
        case 9:
            p.osc1Wave = 0; p.osc1Mix = 0.35f; p.fmMix = 0.35f; p.fmOpLevel[1] = 0.25f;
            p.decay = 0.8f; p.sustain = 0.12f; p.release = 0.4f;
            fx (0, 13, 0.4f, 0.08f, 0.18f, 0.6f); fx (1, 9, 0.3f, 0.6f, 0.65f, 0.17f, 1); break;
        default: break;
    }
    return p;
}
