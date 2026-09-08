#pragma once
#include "SynthEngine.h"

struct FactoryPresetInfo { juce::String name, category, description; };
inline const std::vector<FactoryPresetInfo> factoryPresetCatalog = [] {
    std::vector<FactoryPresetInfo> catalog {
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
    };
    const juce::StringArray families { "Bass", "Keys", "Pluck", "Pad", "Sequence", "Texture", "Lead", "Bell", "Drone", "Layered pad" };
    const juce::StringArray names { "Obsidian", "Aurora", "Velvet", "Prism", "Ember", "Satellite", "Tidal", "Neon", "Polar", "Solstice" };
    for (int family = 0; family < 10; ++family)
        for (int variation = 0; variation < 10; ++variation)
            catalog.push_back ({ names[variation] + " " + families[family], families[family],
                families[family] + " / " + juce::String (variation < 3 ? 1 : juce::jmin (6, variation - 1)) + " instances. "
                + "Voiced with tuned oscillators, envelope movement and complementary spatial effects."
                + (variation >= 2 && (family == 2 || family == 3 || family == 4 || family == 5 || family == 6 || family == 8 || family == 9) ? " MSEG motion is part of the authored sound." : "")
                + (variation >= 5 ? " Layer controls shape the ordered combination; edit each instance independently." : "") });
    return catalog;
}();

inline VoiceParameters makeFactoryPreset (int index)
{
    if (index >= 10)
    {
        const int family = juce::jlimit (0, 9, (index - 10) / 10), variation = (index - 10) % 10;
        const int seeds[] { 1, 9, 3, 4, 6, 7, 8, 2, 4, 5 };
        auto p = makeFactoryPreset (seeds[family]);
        p.layers.fill (nullptr);
        const float t = variation / 9.0f;
        p.osc1Wave = variation % 4; p.osc2Wave = (variation + 1) % 4;
        p.osc2Mix = 0.08f + t * 0.22f; p.osc2Detune = 2 + variation;
        p.cutoff = (family == 0 ? 450.0f : 1800.0f) * (1.0f + t * 2.5f);
        p.resonance = 0.08f + t * 0.26f;
        p.outputGainDb = variation >= 5 ? -15.0f : -11.0f;
        p.extraLfoRate[0] = 0.08f + t * (family == 4 ? 7.0f : 1.2f);
        p.moduleModSlots[1] = { (int) ModSource::lfo2, (int) ModDestination::cutoff, 0.08f + t * 0.2f };
        if (variation >= 2 && (family == 2 || family == 3 || family == 4 || family == 5 || family == 6 || family == 8 || family == 9))
        {
            p.mseg.enabled = true;
            p.mseg.loopEnabled = family == 3 || family == 4 || family == 5 || family == 8 || family == 9;
            p.msegTarget = family == 2 || family == 4 ? (int) ModDestination::amplitude
                          : (family == 3 || family == 8 || family == 9 ? (int) ModDestination::wavetablePosition : (int) ModDestination::cutoff);
            p.msegDepth = family == 2 ? 0.92f : 0.28f + 0.36f * t;
            p.mseg.levels = family == 2
                ? std::array<float, MsegParameters::pointCount> {{ 0.0f, 1.0f, 0.52f, 0.24f, 0.10f, 0.0f }}
                : std::array<float, MsegParameters::pointCount> {{ 0.16f, 0.86f, 0.42f, 0.92f, 0.34f, 0.62f }};
            p.mseg.times = {{ 0.015f + t * 0.08f, 0.08f + t * 0.24f, 0.16f + t * 0.48f, 0.28f + t * 0.72f, 0.22f + t * 0.64f }};
            p.mseg.curves = {{ -0.24f, 0.16f, -0.18f, 0.24f, -0.12f }};
            if (p.msegTarget != (int) ModDestination::cutoff)
                p.modGraphSlots[0] = { (int) ModSource::mseg1, (int) ModDestination::cutoff, 0.12f + 0.18f * t };
        }
        if (family == 7) { p.fmOpRatio[1] = 2.1f + t * 3.3f; p.decay = 1.4f + t; p.release = 0.9f; }
        if (family == 8) { p.attack = 1.0f + t * 2; p.release = 2.5f; p.noiseMix = 0.02f + t * 0.04f; }
        p.fxModules[2] = { 9, 1, false, 0.25f + t * 0.35f, 0.5f, 0.45f, family == 0 ? 0.04f : 0.12f + t * 0.16f };
        const int companionCount = variation < 3 ? 0 : juce::jlimit (1, 5, variation - 2);
        p.mainLayerGain = companionCount > 0 ? 0.80f : 0.95f;
        for (int layer = 0; layer < companionCount; ++layer)
        {
            auto companion = std::make_shared<VoiceParameters> (makeFactoryPreset ((seeds[family] + layer + 2) % 10));
            companion->layers.fill (nullptr); companion->mainLayerGain = 1.0f; companion->outputGainDb = -7.5f;
            const int role = layer % 5;
            if (role == 0)
            {
                companion->attack = juce::jmax (0.002f, p.attack * 0.75f); companion->cutoff = p.cutoff * 0.78f;
                companion->supersawMix = juce::jmax (companion->supersawMix, family == 3 || family == 9 ? 0.28f : 0.08f);
            }
            else if (role == 1)
            {
                companion->filterType = 1; companion->cutoff = juce::jlimit (2200.0f, 9800.0f, p.cutoff * 0.75f + 1800.0f);
                companion->fmMix = juce::jmax (companion->fmMix, 0.18f); companion->stereoWidth = 1.45f; companion->reverbMix = juce::jmax (0.12f, companion->reverbMix);
            }
            else if (role == 2)
            {
                companion->osc1Wave = 0; companion->osc1Mix = 0.55f; companion->osc2Mix = 0.0f; companion->subMix = 0.28f;
                companion->fmMix = companion->wavetableMix = companion->supersawMix = 0.0f; companion->cutoff = juce::jlimit (180.0f, 1600.0f, p.cutoff * 0.24f);
                companion->chorusMix = companion->delayMix = companion->reverbMix = 0.0f; companion->stereoWidth = 0.85f;
            }
            else if (role == 3)
            {
                companion->wavetableMix = juce::jmax (0.32f, companion->wavetableMix); companion->supersawMix = juce::jmax (0.20f, companion->supersawMix);
                companion->extraLfoRate[0] = 0.09f + t * 0.18f; companion->moduleModSlots[0] = { (int) ModSource::lfo2, (int) ModDestination::wavetablePosition, 0.34f };
                companion->chorusMix = juce::jmax (0.10f, companion->chorusMix);
            }
            else
            {
                companion->fmMix = juce::jmax (0.36f, companion->fmMix); companion->fmAlgorithm = (family + variation) % 6;
                companion->fmOpRatio[1] = 2.0f + t * 2.0f; companion->fmOpRatio[2] = 3.0f + t; companion->reverbMix = juce::jmax (0.10f, companion->reverbMix);
            }
            p.layers[(size_t) layer] = companion;
            const float gains[] { 0.27f, 0.17f, 0.22f, 0.18f, 0.14f };
            const float pans[] { -0.16f, 0.34f, 0.0f, -0.32f, 0.28f };
            const float tunes[] { 0.0f, 12.0f, -12.0f, 0.0f, 7.0f };
            p.layerGain[(size_t) layer] = gains[role];
            p.layerPan[(size_t) layer] = pans[role];
            p.layerTune[(size_t) layer] = tunes[role];
            p.layerOperation[(size_t) layer] = 0;
            p.layerAmount[(size_t) layer] = 0.80f;
        }
        const float sourceDensity = p.osc1Mix + p.osc2Mix + p.subMix + p.fmMix + p.wavetableMix + p.supersawMix;
        p.outputGainDb = juce::jlimit (-10.5f, -5.0f, -6.0f - companionCount * 0.55f - juce::jmax (0.0f, sourceDensity - 1.2f) * 1.1f);
        return p;
    }
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
