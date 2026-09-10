#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "UI/MsegPage.h"
#include "UI/UserWavetablePage.h"
#include "UI/MidiMappingPage.h"
#include "UI/LayersPage.h"
#include "UI/FxRackPage.h"
#include "UI/ModulatorsPage.h"
#include "Engine/PresetLibrary.h"
#include "UI/PresetsPage.h"
#include "Matching/GeneratedRackGainPolicy.h"
#include "Matching/ResynthesisAdvisor.h"
#include "Matching/MatchSafetyPolicy.h"
#include "Engine/PresetPackSafety.h"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <vector>

namespace
{
bool isGlobalRackOrClockParameter (const juce::String& id)
{
    if (id.startsWith ("layer") || id.startsWith ("globalFxModule") || id == "mainLayerGain" || id == "oversamplingQuality" || id == "resynthInstances"
        || id == "masterOutputGain" || id == "resynthStrategy" || id == "resynthComplexity")
        return true;
    if (id == "tempoSource" || id == "manualBpm" || id == "chorusSync" || id == "chorusDivision"
        || id == "delaySync" || id == "delayDivision" || id == "msegSync" || id == "msegDivision")
        return true;
    for (int i = 1; i <= 4; ++i)
        if (id == "lfo" + juce::String (i) + "Sync" || id == "lfo" + juce::String (i) + "Division")
            return true;
    return false;
}


float referenceTableWeight (const SoundFeatures& f, int strategy)
{
    const float motion = juce::jlimit (0.0f, 1.0f, f.spectralMotion * 2.5f);
    float weight = juce::jlimit (0.14f, 0.62f,
        0.12f + f.pitchConfidence * 0.22f + f.harmonicity * 0.16f + motion * 0.24f);
    if (strategy == 1) weight = juce::jmax (0.72f, weight);
    if (strategy == 2) weight *= 0.12f;
    if (strategy == 3) weight *= 0.28f;
    if (strategy == 4) weight = juce::jmax (0.36f, weight);
    if (strategy == 5) weight = juce::jmax (0.62f, weight);
    if (strategy == 6) weight = juce::jmax (0.34f, weight * 0.78f);
    return juce::jlimit (0.0f, 0.92f, weight);
}

VoiceParameters makeResynthCompanion (const VoiceParameters& source, int role, int strategy,
                                      const std::shared_ptr<const ReferenceWavetableData>& table)
{
    auto p = source;
    p.layers.fill (nullptr); p.mainLayerGain = 1.0f; p.globalFxModules = {};
    p.referenceWavetable = table;
    p.outputGainDb = juce::jlimit (-12.0f, -4.0f, source.outputGainDb - 1.5f);
    p.delayMix *= 0.65f; p.reverbMix *= 0.75f;
    if (strategy == 6)
    {
        // The main guitar-like voice owns the audible pedal/amp chain. Companion
        // layers contribute body/air/foundation without multiplying every tail.
        for (auto& module : p.fxModules)
            if (module.type == 6 || module.type == 7 || module.type == 8 || module.type == 9) module.mix *= 0.32f;
            else module.mix *= 0.68f;
        p.chorusMix = p.delayMix = p.reverbMix = 0.0f;
    }

    switch (role % 7)
    {
        case 0: // Body: sample fingerprint + stable fundamental.
            if (table) p.referenceWavetableMix = juce::jmax (0.48f, p.referenceWavetableMix);
            p.osc1Mix *= 0.58f; p.osc2Mix *= 0.55f; p.fmMix *= 0.72f;
            p.cutoff = juce::jlimit (180.0f, 15000.0f, p.cutoff * 0.88f);
            break;
        case 1: // Air / shimmer.
            if (table) p.referenceWavetableMix = juce::jmax (0.32f, p.referenceWavetableMix);
            p.filterType = 1; p.cutoff = juce::jlimit (2200.0f, 10500.0f, p.cutoff * 0.72f + 2200.0f);
            p.fmMix = juce::jmax (0.16f, p.fmMix); p.fmAlgorithm = 5;
            p.stereoWidth = juce::jmax (1.25f, p.stereoWidth); p.reverbMix = juce::jmax (0.12f, p.reverbMix);
            break;
        case 2: // Foundation / sub.
            p.osc1Wave = 0; p.osc1Mix = 0.62f; p.osc2Mix = 0.0f; p.subMix = juce::jmax (0.24f, p.subMix);
            p.referenceWavetableMix *= 0.18f; p.wavetableMix *= 0.15f; p.fmMix *= 0.25f; p.supersawMix = 0.0f;
            p.filterType = 0; p.cutoff = juce::jlimit (160.0f, 1800.0f, p.cutoff * 0.28f);
            p.chorusMix = p.delayMix = p.reverbMix = 0.0f; p.stereoWidth = 0.82f;
            break;
        case 3: // Motion table: let MSEG carry the long-form motion, with LFO as a secondary shimmer.
            if (table) p.referenceWavetableMix = juce::jmax (0.52f, p.referenceWavetableMix);
            p.wavetableMix = juce::jmax (0.16f, p.wavetableMix);
            p.mseg.enabled = true; p.mseg.loopEnabled = true; p.msegTarget = (int) ModDestination::wavetablePosition; p.msegDepth = 0.48f;
            p.mseg.levels = {{ 0.18f, 0.82f, 0.46f, 0.94f, 0.35f, 0.62f }};
            p.mseg.times = {{ 0.18f, 0.46f, 0.72f, 0.54f, 0.90f }};
            p.mseg.curves = {{ -0.15f, 0.24f, -0.22f, 0.18f, -0.08f }};
            p.extraLfoRate[0] = 0.11f;
            p.modGraphSlots[0] = { (int) ModSource::mseg1, (int) ModDestination::cutoff, 0.24f };
            p.modGraphSlots[1] = { (int) ModSource::lfo2, (int) ModDestination::wavetablePosition, 0.12f };
            p.chorusMix = juce::jmax (0.10f, p.chorusMix); p.stereoWidth = juce::jmax (1.2f, p.stereoWidth);
            break;
        case 4: // Harmonic / FM colour.
            p.osc1Mix *= 0.48f; p.osc2Mix *= 0.42f;
            p.fmMix = juce::jmax (0.42f, p.fmMix); p.fmAmount = juce::jmax (0.09f, p.fmAmount);
            p.fmAlgorithm = strategy == 3 ? 2 : 4;
            p.fmOpRatio = {{ 1.0f, 2.0f, 3.0f, 1.5f, 4.0f, 6.0f }};
            p.referenceWavetableMix *= 0.42f;
            break;
        case 5: // Width / ensemble body.
            p.supersawMix = juce::jmax (0.38f, p.supersawMix); p.unisonDetune = juce::jmax (16.0f, p.unisonDetune);
            p.unisonSpread = juce::jmax (0.78f, p.unisonSpread); p.chorusMix = juce::jmax (0.12f, p.chorusMix);
            p.stereoWidth = juce::jmax (1.45f, p.stereoWidth); p.referenceWavetableMix *= 0.55f;
            break;
        default: // Texture / transient dust.
            if (table) p.referenceWavetableMix = juce::jmax (0.30f, p.referenceWavetableMix);
            p.noiseMix = juce::jmax (0.035f, juce::jmin (0.16f, p.noiseMix + 0.04f));
            p.ringMix = juce::jmax (0.05f, juce::jmin (0.20f, p.ringMix + 0.04f));
            p.wavefold = juce::jmax (0.06f, juce::jmin (0.28f, p.wavefold + 0.05f));
            p.attack = juce::jmax (0.003f, p.attack * 0.7f); p.release *= 0.72f;
            break;
    }
    return p;
}

void seedGoldWholeInstrumentRack (VoiceParameters& rack, const SoundFeatures& reference, int strategy);

VoiceParameters makeEmbeddedResynthRack (const MatchResult& mainResult, const SoundFeatures& reference,
                                         int complexity, int strategy,
                                         const std::shared_ptr<const ReferenceWavetableData>& table)
{
    auto rack = mainResult.params;
    rack.layers.fill (nullptr);
    strategy = juce::jlimit (0, 6, strategy);

    // Gold evaluates the actual completed instrument. Classic intentionally uses
    // a strong three-instance rack (the upper end of the legacy 1-3 range), while
    // the other choices map directly to 4 / 6 / 8 total instances.
    const int totalInstances = GeneratedRackGainPolicy::totalInstancesForComplexity (complexity);
    rack.mainLayerGain = totalInstances >= 8 ? 0.64f : (totalInstances >= 6 ? 0.70f : 0.78f);

    static const float roleGain[] { 0.30f, 0.18f, 0.24f, 0.20f, 0.17f, 0.16f, 0.13f };
    static const float rolePan[]  { -0.10f, 0.34f, 0.0f, -0.30f, 0.18f, 0.42f, -0.42f };
    static const float roleTune[] { 0.0f, 12.0f, -12.0f, 0.0f, 7.0f, 0.0f, 12.0f };

    seedGoldWholeInstrumentRack (rack, reference, strategy);

    const int wantedLayers = juce::jmin (VoiceParameters::extraLayerCount, totalInstances - 1);
    for (int layer = 0; layer < wantedLayers; ++layer)
    {
        auto companion = makeResynthCompanion (mainResult.params, layer, strategy, table);
        companion.layers.fill (nullptr);
        companion.mainLayerGain = 1.0f;
        rack.layers[(size_t) layer] = std::make_shared<VoiceParameters> (std::move (companion));
        rack.layerGain[(size_t) layer] = roleGain[layer];
        rack.layerPan[(size_t) layer] = rolePan[layer];
        rack.layerTune[(size_t) layer] = roleTune[layer];
        rack.layerOperation[(size_t) layer] = 0;
        rack.layerAmount[(size_t) layer] = 0.78f;
    }

    // Layer roles are allowed to add motion/space for sustained references, but a detected
    // one-shot must remain self-ending as a complete instrument, not only as its main voice.
    SoundMatcher::enforceReferenceLifecycle (reference, rack);
    return rack;
}

float goldGaussian (juce::Random& random)
{
    const float u1 = juce::jmax (1.0e-6f, random.nextFloat());
    const float u2 = random.nextFloat();
    return std::sqrt (-2.0f * std::log (u1)) * std::cos (juce::MathConstants<float>::twoPi * u2);
}

float goldMutateLinear (float value, float lo, float hi, float amount, juce::Random& random)
{
    return juce::jlimit (lo, hi, value + goldGaussian (random) * (hi - lo) * amount);
}

float goldMutateLog (float value, float lo, float hi, float amount, juce::Random& random)
{
    const float lv = std::log (juce::jlimit (lo, hi, value));
    const float llo = std::log (lo), lhi = std::log (hi);
    return std::exp (juce::jlimit (llo, lhi, lv + goldGaussian (random) * (lhi - llo) * amount));
}

float moduleAmountForCutoff (float hz)
{
    return juce::jlimit (0.0f, 1.0f,
                         std::log (juce::jlimit (30.0f, 18000.0f, hz) / 30.0f) / std::log (600.0f));
}

void seedGoldWholeInstrumentRack (VoiceParameters& rack, const SoundFeatures& reference, int strategy)
{
    rack.globalFxModules = {};
    int slot = 0;
    auto add = [&] (int type, int stage, float amount, float rate, float feedback, float mix)
    {
        if (slot >= FxModuleParameters::slotCount) return;
        auto& m = rack.globalFxModules[(size_t) slot++];
        m.type = type; m.stage = stage; m.bypass = false;
        m.amount = juce::jlimit (0.0f, 1.0f, amount);
        m.rate = juce::jlimit (0.0f, 1.0f, rate);
        m.feedback = juce::jlimit (0.0f, 1.0f, feedback);
        m.mix = juce::jlimit (0.0f, 1.0f, mix);
    };

    // Remove rumble only when the reference itself has little deep-low energy.
    if (reference.lowEnergyRatio < 0.12f)
    {
        const float hp = juce::jlimit (35.0f, 160.0f, 45.0f + (0.12f - reference.lowEnergyRatio) * 650.0f);
        add (2, 0, moduleAmountForCutoff (hp), 0.0f, 0.05f, 0.62f);
    }

    // A post-sum LPF is especially useful as speaker/cabinet colour for guitar,
    // but it can also remove synthetic excess from other layered reconstructions.
    if (reference.spectralRolloffHz > 500.0f && reference.spectralRolloffHz < 15000.0f)
    {
        const float target = juce::jlimit (1800.0f, 15000.0f,
                                           reference.spectralRolloffHz * (strategy == 6 ? 0.88f : 1.04f));
        add (1, 0, moduleAmountForCutoff (target), 0.0f, 0.04f, strategy == 6 ? 0.78f : 0.48f);
    }

    // Glue, width and tail are conservative seeds; rack evolution can remove them
    // by driving their mix to zero if the musical reference score rejects them.
    if (reference.transientScore < 0.72f || reference.sustainLevel > 0.48f)
        add (13, 0, juce::jlimit (0.10f, 0.42f, 0.18f + reference.sustainLevel * 0.22f),
             juce::jlimit (0.03f, 0.22f, 0.04f + reference.transientScore * 0.14f), 0.32f, 0.28f);

    if (reference.stereoWidth > 0.18f || reference.spectralMotion > 0.07f)
        add (6, 1, juce::jlimit (0.06f, 0.34f, reference.stereoWidth * 0.22f + reference.spectralMotion * 0.55f),
             juce::jlimit (0.04f, 0.32f, 0.08f + reference.spectralMotion * 1.2f), 0.06f,
             juce::jlimit (0.04f, 0.24f, reference.stereoWidth * 0.16f));

    const float tailNeed = juce::jlimit (0.0f, 1.0f,
        reference.releaseSeconds / juce::jmax (0.12f, juce::jmin (2.5f, reference.duration + 0.15f)) * 1.8f);
    if (tailNeed > 0.15f)
    {
        if (strategy == 6 || reference.spectralMotion > 0.10f)
            add (8, 1, juce::jlimit (0.05f, 0.48f, 0.10f + tailNeed * 0.26f), 0.56f,
                 juce::jlimit (0.08f, 0.45f, tailNeed * 0.32f), juce::jlimit (0.04f, 0.22f, tailNeed * 0.16f));
        add (9, 1, juce::jlimit (0.12f, 0.72f, 0.20f + tailNeed * 0.46f),
             juce::jlimit (0.18f, 0.78f, 0.52f - reference.highEnergyRatio * 0.32f),
             juce::jlimit (0.25f, 0.92f, 0.46f + reference.stereoWidth * 0.28f),
             juce::jlimit (0.04f, 0.30f, 0.06f + tailNeed * 0.22f));
    }
}

VoiceParameters mutateGoldRack (const VoiceParameters& source, const SoundFeatures& reference,
                                int strategy, juce::Random& random, float amount)
{
    auto rack = source;
    rack.mainLayerGain = goldMutateLinear (rack.mainLayerGain, 0.42f, 1.0f, amount * 0.55f, random);

    for (int layer = 0; layer < VoiceParameters::extraLayerCount; ++layer)
    {
        if (! rack.layers[(size_t) layer]) continue;
        rack.layerGain[(size_t) layer] = goldMutateLinear (rack.layerGain[(size_t) layer], 0.0f, 0.72f, amount, random);
        rack.layerAmount[(size_t) layer] = goldMutateLinear (rack.layerAmount[(size_t) layer], 0.28f, 1.0f, amount * 0.72f, random);
        rack.layerPan[(size_t) layer] = goldMutateLinear (rack.layerPan[(size_t) layer], -0.72f, 0.72f, amount * 0.55f, random);
        rack.layerTune[(size_t) layer] = goldMutateLinear (rack.layerTune[(size_t) layer], -24.0f, 24.0f, amount * 0.16f, random);

        if (random.nextFloat() < 0.50f)
        {
            auto voice = std::make_shared<VoiceParameters> (*rack.layers[(size_t) layer]);
            voice->layers.fill (nullptr); voice->mainLayerGain = 1.0f; voice->globalFxModules = {};
            voice->outputGainDb = goldMutateLinear (voice->outputGainDb, -16.0f, -2.0f, amount * 0.45f, random);
            voice->referenceWavetableMix = goldMutateLinear (voice->referenceWavetableMix, 0.0f, 1.0f, amount * 0.55f, random);
            voice->wavetableMix = goldMutateLinear (voice->wavetableMix, 0.0f, 1.0f, amount * 0.45f, random);
            voice->wavetablePosition = goldMutateLinear (voice->wavetablePosition, 0.0f, 1.0f, amount * 0.42f, random);
            voice->fmMix = goldMutateLinear (voice->fmMix, 0.0f, 1.0f, amount * 0.42f, random);
            voice->fmAmount = goldMutateLinear (voice->fmAmount, 0.0f, 0.65f, amount * 0.38f, random);
            voice->noiseMix = goldMutateLinear (voice->noiseMix, 0.0f, 0.38f, amount * 0.28f, random);
            voice->cutoff = goldMutateLog (voice->cutoff, 60.0f, 19500.0f, amount * 0.38f, random);
            voice->resonance = goldMutateLinear (voice->resonance, 0.01f, 0.88f, amount * 0.28f, random);
            voice->stereoWidth = goldMutateLinear (voice->stereoWidth, 0.4f, 1.8f, amount * 0.32f, random);
            if (voice->mseg.enabled)
            {
                voice->msegDepth = goldMutateLinear (voice->msegDepth, -1.0f, 1.0f, amount * 0.40f, random);
                for (auto& level : voice->mseg.levels)
                    level = goldMutateLinear (level, 0.0f, 1.0f, amount * 0.20f, random);
                for (auto& time : voice->mseg.times)
                    time = goldMutateLog (time, 0.001f, 5.0f, amount * 0.18f, random);
                for (auto& curve : voice->mseg.curves)
                    curve = goldMutateLinear (curve, -1.0f, 1.0f, amount * 0.18f, random);
                if (random.nextFloat() < 0.08f)
                {
                    static const int safeMsegTargets[] {
                        (int) ModDestination::amplitude,
                        (int) ModDestination::cutoff,
                        (int) ModDestination::wavetablePosition
                    };
                    if (voice->wavefold > 0.02f && random.nextFloat() < 0.18f)
                        voice->msegTarget = (int) ModDestination::wavefold;
                    else
                        voice->msegTarget = safeMsegTargets[random.nextInt ((int) std::size (safeMsegTargets))];
                }
            }
            rack.layers[(size_t) layer] = std::move (voice);
        }
    }

    // The whole-instrument rack is part of the inverse problem. Mutate its
    // parameters much more often than its topology so Gold converges rather than
    // endlessly replacing useful filters/delays.
    for (auto& module : rack.globalFxModules)
    {
        if (module.type == 0) continue;
        module.amount = goldMutateLinear (module.amount, 0.0f, 1.0f, amount * 0.65f, random);
        module.rate = goldMutateLinear (module.rate, 0.0f, 1.0f, amount * 0.48f, random);
        module.feedback = goldMutateLinear (module.feedback, 0.0f, 1.0f, amount * 0.52f, random);
        module.mix = goldMutateLinear (module.mix, 0.0f, 1.0f, amount * 0.70f, random);
    }

    if (random.nextFloat() < 0.10f)
    {
        const int slot = random.nextInt (FxModuleParameters::slotCount);
        auto& module = rack.globalFxModules[(size_t) slot];
        static const int sensibleTypes[] { 0, 1, 2, 6, 7, 8, 9, 13 };
        module.type = sensibleTypes[random.nextInt ((int) std::size (sensibleTypes))];
        module.stage = random.nextBool() ? 1 : 0;
        module.bypass = false;
        module.amount = random.nextFloat(); module.rate = random.nextFloat();
        module.feedback = random.nextFloat() * 0.65f; module.mix = random.nextFloat() * 0.45f;
    }

    // Avoid uncontrolled layer-operation chaos. A rare crossfade/subtractive test
    // is useful, while multiply/divide remain manual Patch Map sound-design tools.
    if (random.nextFloat() < 0.06f)
    {
        const int layer = random.nextInt (VoiceParameters::extraLayerCount);
        if (rack.layers[(size_t) layer]) rack.layerOperation[(size_t) layer] = random.nextInt (3);
    }

    if (strategy == 6 && reference.transientScore > 0.35f)
    {
        // Keep the pick contour important while still allowing Gold to tune it.
        rack.mseg.enabled = true;
        rack.msegDepth = goldMutateLinear (rack.msegDepth, 0.35f, 1.0f, amount * 0.28f, random);
    }
    if (rack.mseg.enabled)
    {
        rack.msegDepth = goldMutateLinear (rack.msegDepth, -1.0f, 1.0f, amount * 0.30f, random);
        for (auto& level : rack.mseg.levels)
            level = goldMutateLinear (level, 0.0f, 1.0f, amount * 0.16f, random);
        for (auto& time : rack.mseg.times)
            time = goldMutateLog (time, 0.001f, 5.0f, amount * 0.14f, random);
        for (auto& curve : rack.mseg.curves)
            curve = goldMutateLinear (curve, -1.0f, 1.0f, amount * 0.16f, random);
        if (random.nextFloat() < 0.06f)
        {
            static const int safeMsegTargets[] {
                (int) ModDestination::amplitude,
                (int) ModDestination::cutoff,
                (int) ModDestination::wavetablePosition
            };
            if (rack.wavefold > 0.02f && random.nextFloat() < 0.18f)
                rack.msegTarget = (int) ModDestination::wavefold;
            else
                rack.msegTarget = safeMsegTargets[random.nextInt ((int) std::size (safeMsegTargets))];
        }
    }

    // Evolution may mutate MSEG/FX topology after the initial rack was made. Re-apply the
    // lifecycle constraint before scoring so mutation cannot resurrect a held tail.
    SoundMatcher::enforceReferenceLifecycle (reference, rack);
    return rack;
}

MatchResult evolveGoldRack (const SoundFeatures& reference, MatchResult seed, const MatchSettings& settings,
                            int iterations, int64 randomSeed,
                            SoundMatcher::ProgressCallback progress = {}, SoundMatcher::CancelCallback cancel = {})
{
    seed.fullRackScore = true;
    auto best = seed;
    juce::Random random (randomSeed);
    int evaluated = 0;
    int stagnant = 0;

    for (int i = 0; i < iterations; ++i)
    {
        if (cancel && cancel()) break;
        const float phase = (float) i / (float) juce::jmax (1, iterations - 1);
        float amount = juce::jmap (phase, 0.0f, 1.0f, 0.105f, 0.012f);
        if (stagnant > 10) amount = juce::jmax (amount, 0.050f);

        const auto mutated = mutateGoldRack (best.params, reference, best.algorithm, random, amount);
        auto candidate = SoundMatcher::evaluateFit (reference, mutated, settings);
        candidate.algorithm = best.algorithm;
        candidate.complexity = best.complexity;
        candidate.fullRackScore = true;
        candidate.evaluatedCandidates = seed.evaluatedCandidates + evaluated + 1;
        if (candidate.similarity.total > best.similarity.total)
        {
            best = std::move (candidate);
            stagnant = 0;
        }
        else ++stagnant;
        ++evaluated;
        if (progress) progress ((float) (i + 1) / (float) juce::jmax (1, iterations));
    }

    best.explanation = "GOLD rack-level evolution: after method and depth selection, the complete instrument was re-rendered while optimizing main/layer balance, pan, tune, role timbre, MSEG motion and a whole-instrument filter/FX bus. " + best.explanation;
    return best;
}

class OversamplingQualityEditor final : public RetroMatchSynthAudioProcessorEditor
{
public:
    explicit OversamplingQualityEditor (RetroMatchSynthAudioProcessor& processor)
        : RetroMatchSynthAudioProcessorEditor (processor), proc (processor)
    {
        qualityLabel.setText ("NONLINEAR OS", juce::dontSendNotification);
        qualityLabel.setColour (juce::Label::textColourId, getLookAndFeel().findColour (RetroLookAndFeel::secondaryLed));
        qualityLabel.setFont (juce::Font (juce::FontOptions (9.0f, juce::Font::bold)));
        qualityLabel.setJustificationType (juce::Justification::centredRight);

        qualityChoice.addItemList ({ "1x", "2x", "4x" }, 1);
        qualityChoice.setTooltip ("Oversampling quality for the per-voice wavefolder and global drive stage. Higher modes reduce aliasing at increased CPU cost.");
        qualityAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
            proc.apvts, "oversamplingQuality", qualityChoice);

        if (auto* tabbed = findTabbedComponent())
        {
            // Base editor contributes these pages without transferring ownership.
            // Rebuild the outer tab strip in the requested sound-design workflow order.
            auto* synthContent = tabbed->getTabContentComponent (0);
            auto* fmContent = tabbed->getTabContentComponent (1);
            auto* filterContent = tabbed->getTabContentComponent (2);
            auto* builtInMod = tabbed->getTabContentComponent (3);
            auto* builtInFx = tabbed->getTabContentComponent (4);
            auto* baseSettings = tabbed->getTabContentComponent (5);
            auto* baseAiLog = tabbed->getTabContentComponent (6);
            auto* signalContent = tabbed->getTabContentComponent (7);
            auto* melodyContent = tabbed->getTabContentComponent (8);

            settingsPage = baseSettings;
            if (settingsPage != nullptr)
            {
                settingsPage->addAndMakeVisible (qualityLabel);
                settingsPage->addAndMakeVisible (qualityChoice);
            }

            for (int i = tabbed->getNumTabs() - 1; i >= 0; --i)
                tabbed->removeTab (i);

            auto* settingsHub = new juce::TabbedComponent (juce::TabbedButtonBar::TabsAtTop);
            settingsHub->setTabBarDepth (32);
            settingsHub->addTab ("GENERAL + AI", juce::Colour (0xff171d1d), baseSettings, false);
            settingsHub->addTab ("AI LOG", juce::Colour (0xff10191b), baseAiLog, false);

            tabbed->addTab ("PRESETS", juce::Colour (0xff101719), new PresetsPage (proc), true);
            tabbed->addTab ("LAYERS", juce::Colour (0xff101719), new LayersPage (proc), true);
            tabbed->addTab ("SYNTH", juce::Colour (0xff14201e), synthContent, false);
            tabbed->addTab ("FM", juce::Colour (0xff211b14), fmContent, false);
            tabbed->addTab ("MSEG", juce::Colour (0xff10201d), new MsegPage (proc), true);
            tabbed->addTab ("FILTER", juce::Colour (0xff151e20), filterContent, false);
            tabbed->addTab ("MOD", juce::Colour (0xff101719), new ModulatorsPage (proc, builtInMod), true);
            tabbed->addTab ("FX", juce::Colour (0xff101719), new FxRackPage (proc, builtInFx), true);
            tabbed->addTab ("WAVETABLE", juce::Colour (0xff101b20), new UserWavetablePage (proc), true);
            tabbed->addTab ("SIGNAL", juce::Colour (0xff102024), signalContent, false);
            tabbed->addTab ("MIDI MAP", juce::Colour (0xff171b20), new MidiMappingPage (proc), true);
            tabbed->addTab ("MELODY", juce::Colour (0xff102024), melodyContent, false);
            tabbed->addTab ("SETTINGS", juce::Colour (0xff171d1d), settingsHub, true);
            tabbed->setCurrentTabIndex (0);
        }
        resized();
    }

    void resized() override
    {
        RetroMatchSynthAudioProcessorEditor::resized();
        if (settingsPage == nullptr) return;

        auto header = settingsPage->getLocalBounds().reduced (12).removeFromTop (24);
        auto qualityArea = header.removeFromRight (220);
        qualityLabel.setBounds (qualityArea.removeFromLeft (92));
        qualityChoice.setBounds (qualityArea.reduced (3, 1));
    }

private:
    juce::TabbedComponent* findTabbedComponent() const
    {
        for (int i = 0; i < getNumChildComponents(); ++i)
            if (auto* tabbed = dynamic_cast<juce::TabbedComponent*> (getChildComponent (i)))
                return tabbed;
        return nullptr;
    }

    RetroMatchSynthAudioProcessor& proc;
    juce::Component* settingsPage = nullptr;
    juce::Label qualityLabel;
    juce::ComboBox qualityChoice;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> qualityAttachment;
};

juce::ValueTree stateWithPost10Defaults (const juce::XmlElement& xml)
{
    auto state = juce::ValueTree::fromXml (xml);
    if (! state.isValid()) return state;

    auto setDefault = [&state] (const juce::String& id, const juce::var& value)
    {
        if (! state.getChildWithProperty ("id", id).isValid())
        {
            juce::ValueTree parameter ("PARAM");
            parameter.setProperty ("id", id, nullptr);
            parameter.setProperty ("value", state.getProperty (id, value), nullptr);
            state.appendChild (parameter, nullptr);
        }
    };

    setDefault ("oversamplingQuality", 0);
    setDefault ("msegEnabled", false);
    setDefault ("msegLoopEnabled", false);
    setDefault ("msegLoopStart", 1);
    setDefault ("msegLoopEnd", 2);
    setDefault ("msegTarget", (int) ModDestination::amplitude);
    setDefault ("msegDepth", 1.0f);
    setDefault ("userWavetableMix", 0.0f);
    setDefault ("distortionMode", 0); setDefault ("distortionMix", 1.0f); setDefault ("mainLayerGain", 1.0f);
    for (int i = 1; i <= VoiceParameters::extraLayerCount; ++i)
    {
        const auto prefix = "layer" + juce::String (i);
        setDefault (prefix + "Enabled", false); setDefault (prefix + "Gain", 0.5f);
        setDefault (prefix + "Pan", 0.0f); setDefault (prefix + "Tune", 0.0f);
        setDefault (prefix + "Operation", 0); setDefault (prefix + "Amount", 1.0f);
    }

    const std::array<float, MsegParameters::pointCount> levels {{ 0.0f, 1.0f, 0.78f, 0.58f, 0.28f, 0.0f }};
    const std::array<float, MsegParameters::segmentCount> times {{ 0.025f, 0.090f, 0.180f, 0.320f, 0.420f }};
    const std::array<float, MsegParameters::segmentCount> curves {{ 0.15f, -0.10f, 0.0f, 0.10f, -0.15f }};
    for (int i = 0; i < MsegParameters::pointCount; ++i)
        setDefault ("msegLevel" + juce::String (i + 1), levels[(size_t) i]);
    for (int i = 0; i < MsegParameters::segmentCount; ++i)
    {
        setDefault ("msegTime" + juce::String (i + 1), times[(size_t) i]);
        setDefault ("msegCurve" + juce::String (i + 1), curves[(size_t) i]);
    }
    for (int i = 0; i < VoiceParameters::modGraphSlotCount; ++i)
    {
        const auto index = juce::String (i + 1);
        setDefault ("modGraph" + index + "Source", 0);
        setDefault ("modGraph" + index + "Dest", 0);
        setDefault ("modGraph" + index + "Amount", 0.0f);
    }
    for (int i = 1; i <= FxModuleParameters::slotCount; ++i)
    {
        const auto prefix = "fxModule" + juce::String (i);
        setDefault (prefix + "Type", 0); setDefault (prefix + "Stage", 0); setDefault (prefix + "Bypass", false);
        setDefault (prefix + "Amount", 0.5f); setDefault (prefix + "Rate", 0.25f); setDefault (prefix + "Feedback", 0.25f); setDefault (prefix + "Mix", 0.5f);
        setDefault (prefix + "TempoSync", false); setDefault (prefix + "Division", 3);
    }
    for (int i = 1; i <= FxModuleParameters::slotCount; ++i)
    {
        const auto prefix = "globalFxModule" + juce::String (i);
        setDefault (prefix + "Type", 0); setDefault (prefix + "Stage", 0); setDefault (prefix + "Bypass", false);
        setDefault (prefix + "Amount", 0.5f); setDefault (prefix + "Rate", 0.25f); setDefault (prefix + "Feedback", 0.25f); setDefault (prefix + "Mix", 0.5f);
        setDefault (prefix + "TempoSync", false); setDefault (prefix + "Division", 3);
    }
    for (int i = 2; i <= 4; ++i)
    {
        const auto prefix = "lfoModule" + juce::String (i); setDefault (prefix + "Rate", i == 2 ? 0.5f : i == 3 ? 2.0f : 5.0f); setDefault (prefix + "Shape", 0);
    }
    for (int i = 1; i <= 4; ++i)
    {
        const auto prefix = "moduleMod" + juce::String (i); setDefault (prefix + "Source", 0); setDefault (prefix + "Dest", 0); setDefault (prefix + "Amount", 0.0f);
    }

    setDefault ("resynthInstances", 1);
    setDefault ("resynthStrategy", 0);
    setDefault ("resynthComplexity", 0);
    setDefault ("masterOutputGain", 0.0f);
    setDefault ("tempoSource", 1);
    setDefault ("manualBpm", 120.0f);
    for (int i = 1; i <= 4; ++i)
    {
        setDefault ("lfo" + juce::String (i) + "Sync", false);
        setDefault ("lfo" + juce::String (i) + "Division", 3);
    }
    setDefault ("chorusSync", false); setDefault ("chorusDivision", 3);
    setDefault ("delaySync", false); setDefault ("delayDivision", 3);
    setDefault ("msegSync", false); setDefault ("msegDivision", 3);
    return state;
}
}

RetroMatchSynthAudioProcessor::RetroMatchSynthAudioProcessor()
 : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
   apvts (*this, nullptr, "STATE", createLayout())
{
}

void RetroMatchSynthAudioProcessor::prepareToPlay (double sr, int bs)
{
    const int channels = getTotalNumOutputChannels();
    engine.prepare (sr, bs, channels);
    globalModuleRack.prepare (sr, bs, channels);
    renderMidi.ensureSize (131072);
    { const juce::ScopedLock lock (editorMidiLock); editorMidi.clear(); editorMidi.ensureSize (16384); }
    melodyTransport.stop();
    setLatencySamples (engine.getLatencySamples());

    referencePlayer.prepare (sr);
    referenceScratch.setSize (juce::jmax (1, channels), juce::jmax (1, bs), false, false, true);
    const juce::dsp::ProcessSpec spec { sr, (juce::uint32) juce::jmax (1, bs), (juce::uint32) juce::jmax (1, channels) };
    referenceLatencyDelay.setMaximumDelayInSamples (juce::jmax (1, engine.getLatencySamples() + 8));
    referenceLatencyDelay.prepare (spec);
    referenceLatencyDelay.setDelay ((float) engine.getLatencySamples());
    referenceLatencyDelay.reset();
}

bool RetroMatchSynthAudioProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void RetroMatchSynthAudioProcessor::rebuildRoutingPlanFromState()
{
    const auto graph = getPatchGraphDocument();
    const auto compiled = DspRouting::compile (graph);
    routingPlanPublisher.publish (compiled.validation.ok ? compiled.plan : DspRouting::Plan {});
}

VoiceParameters RetroMatchSynthAudioProcessor::readParams (const juce::ValueTree& snapshot, bool routed) const
{
    VoiceParameters p;
    auto v = [this, &snapshot] (const juce::String& id)
    {
        if (! snapshot.isValid()) return apvts.getRawParameterValue (id)->load();
        auto* parameter = apvts.getParameter (id);
        return (float) snapshot.getProperty (id, parameter->convertFrom0to1 (parameter->getDefaultValue()));
    };

    p.osc1Wave = (int) v ("osc1Wave");
    p.osc2Wave = (int) v ("osc2Wave");
    p.osc1Mix = v ("osc1Mix");
    p.osc2Mix = v ("osc2Mix");
    p.subMix = v ("subMix");
    p.noiseMix = v ("noise");
    p.ringMix = v ("ringMix");
    p.additiveMix = v ("additiveMix");
    p.masterTuneCents = v ("masterTune");
    p.osc2Semitones = v ("osc2Semi");
    p.osc2Detune = v ("osc2Detune");
    p.pulseWidth = v ("pulseWidth");
    p.wavetableMix = v ("wavetableMix");
    p.wavetablePosition = v ("wavetablePosition");
    p.wavetableWarp = v ("wavetableWarp");
    p.referenceWavetableMix = v ("referenceWavetableMix");
    p.referenceWavetable = referenceWavetable;
    p.userWavetableMix = v ("userWavetableMix");
    p.userWavetable = userWavetable;
    p.supersawMix = v ("supersawMix");
    p.unisonDetune = v ("unisonDetune");
    p.unisonSpread = v ("unisonSpread");
    p.wavefold = v ("wavefold");
    p.fmAmount = v ("fmAmount");
    p.fmRatio = v ("fmRatio");
    p.fmMix = v ("fmMix");
    p.fmFeedback = v ("fmFeedback");
    p.fmAlgorithm = (int) v ("fmAlgorithm");
    for (int i = 0; i < VoiceParameters::fmOperatorCount; ++i)
    {
        const auto index = juce::String (i + 1);
        p.fmOpRatio[(size_t) i] = v ("fmOp" + index + "Ratio");
        p.fmOpLevel[(size_t) i] = v ("fmOp" + index + "Level");
        p.fmOpFixedMode[(size_t) i] = (int) v ("fmOp" + index + "Mode");
        p.fmOpFixedHz[(size_t) i] = v ("fmOp" + index + "FixedHz");
        p.fmOpAttack[(size_t) i] = v ("fmOp" + index + "Attack");
        p.fmOpDecay[(size_t) i] = v ("fmOp" + index + "Decay");
        p.fmOpSustain[(size_t) i] = v ("fmOp" + index + "Sustain");
        p.fmOpRelease[(size_t) i] = v ("fmOp" + index + "Release");
        p.fmOpKeyScale[(size_t) i] = v ("fmOp" + index + "KeyScale");
        p.fmOpVelocity[(size_t) i] = v ("fmOp" + index + "Velocity");
    }
    p.harmonicTilt = v ("harmonicTilt");
    p.oddEvenBalance = v ("oddEven");

    p.attack = v ("attack");
    p.decay = v ("decay");
    p.sustain = v ("sustain");
    p.release = v ("release");
    p.cutoff = v ("cutoff");
    p.resonance = v ("resonance");
    p.filterType = (int) v ("filterType");

    p.lfoRate = v ("lfoRate");
    p.lfoPitch = v ("lfoPitch");
    p.lfoCutoff = v ("lfoCutoff");
    p.lfoAmp = v ("lfoAmp");
    for (int i = 0; i < VoiceParameters::modSlotCount; ++i)
    {
        const auto index = juce::String (i + 1);
        p.modSlots[(size_t) i].source = (int) v ("mod" + index + "Source");
        p.modSlots[(size_t) i].destination = (int) v ("mod" + index + "Dest");
        p.modSlots[(size_t) i].amount = v ("mod" + index + "Amount");
    }

    p.mseg.enabled = v ("msegEnabled") >= 0.5f;
    p.msegTarget = juce::jlimit ((int) ModDestination::none, (int) ModDestination::wavefold, (int) v ("msegTarget"));
    p.msegDepth = juce::jlimit (-1.0f, 1.0f, v ("msegDepth"));
    p.mseg.loopEnabled = v ("msegLoopEnabled") >= 0.5f;
    p.mseg.loopStartPoint = juce::jlimit (0, MsegParameters::pointCount - 2, (int) v ("msegLoopStart"));
    p.mseg.loopEndPoint = juce::jlimit (1, MsegParameters::pointCount - 1, (int) v ("msegLoopEnd") + 1);
    for (int i = 0; i < MsegParameters::pointCount; ++i)
        p.mseg.levels[(size_t) i] = v ("msegLevel" + juce::String (i + 1));
    for (int i = 0; i < MsegParameters::segmentCount; ++i)
    {
        p.mseg.times[(size_t) i] = v ("msegTime" + juce::String (i + 1));
        p.mseg.curves[(size_t) i] = v ("msegCurve" + juce::String (i + 1));
    }
    for (int i = 0; i < VoiceParameters::modGraphSlotCount; ++i)
    {
        const auto index = juce::String (i + 1);
        p.modGraphSlots[(size_t) i].source = (int) v ("modGraph" + index + "Source");
        p.modGraphSlots[(size_t) i].destination = (int) v ("modGraph" + index + "Dest");
        p.modGraphSlots[(size_t) i].amount = v ("modGraph" + index + "Amount");
    }

    for (int i = 0; i < FxModuleParameters::slotCount; ++i)
    {
        const auto prefix = "fxModule" + juce::String (i + 1); auto& module = p.fxModules[(size_t) i];
        module.type = (int) v (prefix + "Type"); module.stage = (int) v (prefix + "Stage"); module.bypass = v (prefix + "Bypass") >= 0.5f;
        module.amount = v (prefix + "Amount"); module.rate = v (prefix + "Rate"); module.feedback = v (prefix + "Feedback"); module.mix = v (prefix + "Mix");
        module.tempoSync = v (prefix + "TempoSync") >= 0.5f; module.tempoDivision = (int) v (prefix + "Division");
    }
    for (int i = 0; i < 3; ++i)
    {
        const auto prefix = "lfoModule" + juce::String (i + 2);
        p.extraLfoRate[(size_t) i] = v (prefix + "Rate"); p.extraLfoShape[(size_t) i] = (int) v (prefix + "Shape");
    }
    for (int i = 0; i < 4; ++i)
    {
        const auto prefix = "moduleMod" + juce::String (i + 1); auto& slot = p.moduleModSlots[(size_t) i];
        slot.source = (int) v (prefix + "Source"); slot.destination = (int) v (prefix + "Dest"); slot.amount = v (prefix + "Amount");
    }
    p.drive = v ("drive");
    p.distortionMode = (int) v ("distortionMode");
    p.distortionMix = v ("distortionMix");
    p.chorusMix = v ("chorusMix");
    p.chorusRate = v ("chorusRate");
    p.chorusDepth = v ("chorusDepth");
    p.delayMix = v ("delayMix");
    p.delayTime = v ("delayTime");
    p.delayFeedback = v ("delayFeedback");
    p.reverbMix = v ("reverbMix");
    p.reverbSize = v ("reverbSize");
    p.reverbDamping = v ("reverbDamping");
    p.stereoWidth = v ("stereoWidth");
    p.outputGainDb = v ("outputGain");

    p.tempoBpm = effectiveBpm.load (std::memory_order_relaxed);
    for (int i = 0; i < 4; ++i)
    {
        const auto prefix = "lfo" + juce::String (i + 1);
        p.lfoTempoSync[(size_t) i] = v (prefix + "Sync") >= 0.5f;
        p.lfoTempoDivision[(size_t) i] = (int) v (prefix + "Division");
    }
    p.chorusTempoSync = v ("chorusSync") >= 0.5f; p.chorusTempoDivision = (int) v ("chorusDivision");
    p.delayTempoSync = v ("delaySync") >= 0.5f; p.delayTempoDivision = (int) v ("delayDivision");
    p.msegTempoSync = v ("msegSync") >= 0.5f; p.msegTempoDivision = (int) v ("msegDivision");

    p.oversamplingQuality = juce::jlimit (0, 2, (int) v ("oversamplingQuality"));
    if (snapshot.isValid())
    {
        p.referenceWavetable = ReferenceWavetableData::fromBase64 (snapshot["referenceTable"].toString());
        p.userWavetable = ReferenceWavetableData::fromBase64 (snapshot["userTable"].toString());
    }
    else
    {
        p.mainLayerGain = v ("mainLayerGain");
        for (int i = 0; i < VoiceParameters::extraLayerCount; ++i)
        {
            const auto prefix = "layer" + juce::String (i + 1);
            if (v (prefix + "Enabled") >= 0.5f) p.layers[(size_t) i] = savedLayers[(size_t) i].load();
            p.layerGain[(size_t) i] = v (prefix + "Gain");
            p.layerPan[(size_t) i] = v (prefix + "Pan");
            p.layerTune[(size_t) i] = v (prefix + "Tune");
            p.layerOperation[(size_t) i] = (int) v (prefix + "Operation");
            p.layerAmount[(size_t) i] = v (prefix + "Amount");
        }
    }
    if (routed && ! snapshot.isValid())
        if (auto main = editingMain.load())
        {
            auto mixed = *main;
            mixed.layers = p.layers; mixed.mainLayerGain = p.mainLayerGain; mixed.oversamplingQuality = p.oversamplingQuality;
            mixed.layerGain = p.layerGain; mixed.layerPan = p.layerPan; mixed.layerTune = p.layerTune;
            mixed.layerOperation = p.layerOperation; mixed.layerAmount = p.layerAmount;
            mixed.inheritTempoFrom (p);
            return mixed;
        }
    return p;
}

void RetroMatchSynthAudioProcessor::delayReferenceForLatency (juce::AudioBuffer<float>& buffer)
{
    const int latency = engine.getLatencySamples();
    if (latency <= 0) return;
    referenceLatencyDelay.setDelay ((float) latency);

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* samples = buffer.getWritePointer (ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            referenceLatencyDelay.pushSample (ch, samples[i]);
            samples[i] = referenceLatencyDelay.popSample (ch);
        }
    }
}

void RetroMatchSynthAudioProcessor::processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer& m)
{
    juce::ScopedNoDenormals noDenormals;
    b.clear();
    for (const auto metadata : m)
    {
        const auto message = metadata.getMessage();
        if (! message.isController()) continue;
        const int cc = message.getControllerNumber();
        const float value = message.getControllerValue() / 127.0f;
        const juce::ScopedLock lock (midiMappingLock);
        if (midiLearning.load())
        {
            midiMappings.erase (std::remove_if (midiMappings.begin(), midiMappings.end(), [this] (const auto& x) { return x.parameterId == midiLearnParameter; }), midiMappings.end());
            midiMappings.push_back ({ midiLearnParameter, cc }); midiLearning.store (false);
        }
        else
            for (const auto& mapping : midiMappings)
                if (mapping.cc == cc)
                    if (auto* parameter = apvts.getParameter (mapping.parameterId)) parameter->setValue (parameter->convertTo0to1 (value));
    }
    renderMidi.clear();
    renderMidi.addEvents (m, 0, b.getNumSamples(), 0);
    {
        const juce::ScopedTryLock lock (editorMidiLock);
        if (lock.isLocked()) { renderMidi.addEvents (editorMidi, 0, -1, 0); editorMidi.clear(); }
    }
    melodyTransport.process (renderMidi, b.getNumSamples(), getSampleRate());

    float bpm = apvts.getRawParameterValue ("manualBpm")->load();
    if (apvts.getRawParameterValue ("tempoSource")->load() >= 0.5f)
        if (auto* playHead = getPlayHead())
            if (auto position = playHead->getPosition())
                if (auto hostBpm = position->getBpm())
                    bpm = (float) *hostBpm;
    effectiveBpm.store (TempoSync::clampBpm (bpm), std::memory_order_relaxed);

    const auto mode = getReferenceAuditionMode();
    engine.setRoutingPlan (routingPlanPublisher.snapshot());
    engine.setParameters (readParams());
    engine.render (b, renderMidi);

    // Whole-synth global bus. This happens after SynthEngine has rendered and combined
    // every active instance, so filters/effects here colour the complete patch instead
    // of being repeated separately inside each layer. Reference-only audition remains dry.
    if (mode != ReferenceAuditionMode::referenceOnly)
    {
        std::array<FxModuleParameters, FxModuleParameters::slotCount> globalModules {};
        for (int i = 0; i < FxModuleParameters::slotCount; ++i)
        {
            const auto prefix = "globalFxModule" + juce::String (i + 1);
            auto value = [this, &prefix] (const char* suffix, float fallback)
            {
                if (auto* v = apvts.getRawParameterValue (prefix + suffix)) return v->load();
                return fallback;
            };
            auto& module = globalModules[(size_t) i];
            module.type = (int) value ("Type", 0.0f);
            module.stage = (int) value ("Stage", 0.0f);
            module.bypass = value ("Bypass", 0.0f) >= 0.5f;
            module.amount = value ("Amount", 0.5f);
            module.rate = value ("Rate", 0.25f);
            module.feedback = value ("Feedback", 0.25f);
            module.mix = value ("Mix", 0.5f);
            module.tempoSync = value ("TempoSync", 0.0f) >= 0.5f;
            module.tempoDivision = (int) value ("Division", 3.0f);
        }
        globalModuleRack.process (b, globalModules, 0, effectiveBpm.load (std::memory_order_relaxed));
        globalModuleRack.process (b, globalModules, 1, effectiveBpm.load (std::memory_order_relaxed));
    }

    if (mode == ReferenceAuditionMode::referenceOnly) b.clear();

    if (mode != ReferenceAuditionMode::synthOnly && referencePlayer.hasSample())
    {
        if (referenceScratch.getNumChannels() != b.getNumChannels() || referenceScratch.getNumSamples() < b.getNumSamples())
            referenceScratch.setSize (b.getNumChannels(), b.getNumSamples(), false, false, true);

        referenceScratch.clear();
        referencePlayer.render (referenceScratch, renderMidi, 0, b.getNumSamples());
        delayReferenceForLatency (referenceScratch);
        const float gain = referenceAuditionLevel.load();

        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            if (mode == ReferenceAuditionMode::referenceOnly)
            {
                b.copyFrom (ch, 0, referenceScratch, ch, 0, b.getNumSamples());
                b.applyGain (ch, 0, b.getNumSamples(), gain);
            }
            else
            {
                b.addFrom (ch, 0, referenceScratch, ch, 0, b.getNumSamples(), gain);
            }
        }
    }
    else if (mode == ReferenceAuditionMode::synthOnly)
    {
        referenceLatencyDelay.reset();
    }

    // Global hardware-style master trim. Unlike patch OUTPUT this survives preset changes
    // and controls the complete instrument, including reference A/B audition.
    const float masterDb = apvts.getRawParameterValue ("masterOutputGain")->load();
    b.applyGain (juce::Decibels::decibelsToGain (masterDb));

    if (b.getNumSamples() > 0 && b.getNumChannels() > 0)
    {
        const float left = b.getMagnitude (0, 0, b.getNumSamples());
        const int rightChannel = juce::jmin (1, b.getNumChannels() - 1);
        const float right = b.getMagnitude (rightChannel, 0, b.getNumSamples());
        const float previousLeft = outputPeakLeft.load (std::memory_order_relaxed);
        const float previousRight = outputPeakRight.load (std::memory_order_relaxed);
        outputPeakLeft.store (juce::jmax (left, previousLeft * 0.88f), std::memory_order_relaxed);
        outputPeakRight.store (juce::jmax (right, previousRight * 0.88f), std::memory_order_relaxed);
    }
    visualAudio.push (b);
}

float RetroMatchSynthAudioProcessor::midiNoteToHz (int midiNote)
{
    const int note = juce::jlimit (0, 127, midiNote);
    return 440.0f * std::pow (2.0f, (note - 69) / 12.0f);
}

int RetroMatchSynthAudioProcessor::hzToNearestMidiNote (float hz)
{
    if (! std::isfinite (hz) || hz <= 0.0f) return 60;
    return juce::jlimit (0, 127, (int) std::lround (69.0 + 12.0 * std::log2 ((double) hz / 440.0)));
}

void RetroMatchSynthAudioProcessor::invalidateMatchesAfterReferencePitchChange()
{
    allEditorNotesOff();
    currentCandidateFeatures.reset();
    lastMatch = {};
    candidateBank = {};
    clearCompareFineTuneState();
    selectedCandidate = 0;
}

void RetroMatchSynthAudioProcessor::beginMidiLearn (const juce::String& parameterId)
{
    const juce::ScopedLock lock (midiMappingLock);
    midiLearnParameter = parameterId;
    midiLearning.store (parameterId.isNotEmpty());
}

void RetroMatchSynthAudioProcessor::removeMidiMapping (const juce::String& parameterId)
{
    const juce::ScopedLock lock (midiMappingLock);
    midiMappings.erase (std::remove_if (midiMappings.begin(), midiMappings.end(), [&parameterId] (const auto& x) { return x.parameterId == parameterId; }), midiMappings.end());
}

std::vector<RetroMatchSynthAudioProcessor::MidiMapping> RetroMatchSynthAudioProcessor::getMidiMappings() const
{
    const juce::ScopedLock lock (midiMappingLock);
    return midiMappings;
}

bool RetroMatchSynthAudioProcessor::loadReferenceSample (const juce::File& f)
{
    auto analysed = SampleAnalyzer::analyzeFile (f);
    if (! analysed) return false;
    setMelodyClip ({});

    detectedReferenceHz = analysed->fundamentalHz;
    detectedReferencePitchConfidence = analysed->pitchConfidence;
    detectedReferenceMidiNote = hzToNearestMidiNote (detectedReferenceHz);
    referenceBaseMidiNote.store (detectedReferenceMidiNote);
    referencePitchLocked.store (false);
    loadedReferenceFile = f;

    double sourceDuration = analysed->duration;
    juce::AudioFormatManager durationFormats;
    durationFormats.registerBasicFormats();
    if (std::unique_ptr<juce::AudioFormatReader> durationReader (durationFormats.createReaderFor (f)); durationReader != nullptr && durationReader->sampleRate > 0.0)
        sourceDuration = (double) durationReader->lengthInSamples / durationReader->sampleRate;
    const float sourceSeconds = (float) juce::jmax (0.0, sourceDuration);
    analysisSourceDuration.store (sourceSeconds);
    analysisStartSeconds.store (0.0f);
    const float initialEnd = sourceSeconds > 0.0f ? juce::jmin (sourceSeconds, juce::jmax (0.05f, analysed->duration)) : analysed->duration;
    analysisEndSeconds.store (initialEnd);

    currentFeatures = std::move (analysed);
    currentCandidateFeatures.reset();
    const float wavetableHz = currentFeatures->fundamentalHz > 20.0f ? currentFeatures->fundamentalHz : midiNoteToHz (referenceBaseMidiNote.load());
    referenceWavetable = ReferenceWavetableExtractor::extract (f, wavetableHz, 0.0f, initialEnd);
    referencePlayer.load (f, detectedReferenceMidiNote);
    loadedSampleName = f.getFileName();
    setMidiAnalysisRegion (0.0f, sourceSeconds);
    lastMatch = {};
    candidateBank = {};
    clearCompareFineTuneState();
    selectedCandidate = 0;
    return true;
}

bool RetroMatchSynthAudioProcessor::setReferenceAnalysisRegion (float startSeconds, float endSeconds)
{
    if (! loadedReferenceFile.existsAsFile()) return false;
    const float duration = analysisSourceDuration.load();
    const float start = juce::jlimit (0.0f, juce::jmax (0.0f, duration - 0.002f), startSeconds);
    const float end = juce::jlimit (start + 0.002f, juce::jmax (start + 0.002f, duration), endSeconds);
    const float expected = referencePitchLocked.load() ? midiNoteToHz (referenceBaseMidiNote.load()) : 0.0f;
    auto analysed = SampleAnalyzer::analyzeFile (loadedReferenceFile, expected, start, end);
    if (! analysed) return false;

    analysisStartSeconds.store (start);
    analysisEndSeconds.store (end);
    if (! referencePitchLocked.load())
    {
        detectedReferenceHz = analysed->fundamentalHz;
        detectedReferencePitchConfidence = analysed->pitchConfidence;
        if (detectedReferenceHz > 20.0f && detectedReferencePitchConfidence > 0.10f)
        {
            detectedReferenceMidiNote = hzToNearestMidiNote (detectedReferenceHz);
            referenceBaseMidiNote.store (detectedReferenceMidiNote);
            referencePlayer.setRootMidiNote (detectedReferenceMidiNote);
        }
    }

    currentFeatures = std::move (analysed);
    const float wavetableHz = currentFeatures->fundamentalHz > 20.0f ? currentFeatures->fundamentalHz : midiNoteToHz (referenceBaseMidiNote.load());
    referenceWavetable = ReferenceWavetableExtractor::extract (loadedReferenceFile, wavetableHz, start, end);
    invalidateMatchesAfterReferencePitchChange();
    return true;
}

bool RetroMatchSynthAudioProcessor::createUserWavetableFromReference (float start, float end, bool chop)
{
    if (! loadedReferenceFile.existsAsFile()) return false;
    auto table = chop ? ReferenceWavetableExtractor::chop (loadedReferenceFile, start, end)
                      : ReferenceWavetableExtractor::extract (loadedReferenceFile, midiNoteToHz (referenceBaseMidiNote.load()), start, end);
    if (! table || ! table->valid) return false;
    userWavetable = std::move (table);
    userWavetableName = loadedReferenceFile.getFileNameWithoutExtension() + " [" + juce::String (start, 3) + " - " + juce::String (end, 3) + " s]";
    userWavetableDescription = chop ? "5 sample slices / pitch-normalized frames / click a frame or scan WT POSITION" : "Sample selection / 5 frames / adjust USER WT MIX and WT POSITION";
    auto* mix = apvts.getParameter ("userWavetableMix");
    mix->setValueNotifyingHost (mix->convertTo0to1 (0.8f));
    return true;
}

bool RetroMatchSynthAudioProcessor::hasLayer (int index) const
{
    return juce::isPositiveAndBelow (index, VoiceParameters::extraLayerCount)
        && savedLayers[(size_t) index].load() != nullptr;
}

juce::String RetroMatchSynthAudioProcessor::getLayerName (int index) const
{
    return apvts.state.getChildWithName ("SYNTH_LAYERS").getChildWithProperty ("index", index)["name"].toString();
}

juce::ValueTree RetroMatchSynthAudioProcessor::snapshotCurrent() const
{
    juce::ValueTree snapshot ("LAYER");
    for (auto* parameter : getParameters())
        if (auto* identified = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
            snapshot.setProperty (identified->paramID, apvts.getRawParameterValue (identified->paramID)->load(), nullptr);
    if (referenceWavetable) snapshot.setProperty ("referenceTable", referenceWavetable->toBase64(), nullptr);
    if (userWavetable) snapshot.setProperty ("userTable", userWavetable->toBase64(), nullptr);
    snapshot.setProperty ("userTableName", userWavetableName, nullptr);
    snapshot.setProperty ("userTableDescription", userWavetableDescription, nullptr);
    return snapshot;
}

void RetroMatchSynthAudioProcessor::refreshEditingLayer()
{
    const int index = editingLayer.load();
    if (index >= 0)
    {
        auto edited = readParams ({}, false);
        edited.layers.fill (nullptr); edited.mainLayerGain = 1.0f;
        savedLayers[(size_t) index].store (std::make_shared<VoiceParameters> (edited));
    }
}

void RetroMatchSynthAudioProcessor::selectEditingLayer (int index)
{
    if (index == editingLayer.load() || (index >= 0 && ! hasLayer (index))) return;
    if (editingLayer.load() >= 0)
    {
        const int previous = editingLayer.exchange (-1);
        const auto id = "layer" + juce::String (previous + 1) + "Enabled";
        const float enabled = apvts.getRawParameterValue (id)->load();
        captureLayer (previous);
        apvts.getParameter (id)->setValueNotifyingHost (enabled);
        applyEditingSnapshot (editingMainSnapshot);
        editingMain.store (nullptr);
    }
    if (index >= 0)
    {
        editingMainSnapshot = snapshotCurrent();
        editingMain.store (std::make_shared<VoiceParameters> (getMainVoiceParameters()));
        loadLayerToMain (index);
        editingLayer.store (index);
    }
}

juce::ValueTree RetroMatchSynthAudioProcessor::canonicalState()
{
    auto state = apvts.copyState();
    const int index = editingLayer.load();
    if (index < 0) return state;
    auto bank = state.getOrCreateChildWithName ("SYNTH_LAYERS", nullptr);
    auto previous = bank.getChildWithProperty ("index", index);
    auto snapshot = snapshotCurrent();
    snapshot.setProperty ("index", index, nullptr);
    snapshot.setProperty ("name", previous["name"], nullptr);
    bank.removeChild (previous, nullptr); bank.appendChild (snapshot, nullptr);
    for (auto child : state)
    {
        const auto id = child["id"].toString();
        if (! isGlobalRackOrClockParameter (id) && editingMainSnapshot.hasProperty (id))
            child.setProperty ("value", editingMainSnapshot[id], nullptr);
    }
    return state;
}

void RetroMatchSynthAudioProcessor::captureLayer (int index)
{
    if (! juce::isPositiveAndBelow (index, VoiceParameters::extraLayerCount)) return;
    auto snapshot = snapshotCurrent();
    snapshot.setProperty ("index", index, nullptr);
    snapshot.setProperty ("name", loadedSampleName.isEmpty() ? "Current patch" : loadedSampleName + " / " + juce::String (getAnalysisStartSeconds(), 2) + " s", nullptr);
    auto bank = apvts.state.getOrCreateChildWithName ("SYNTH_LAYERS", nullptr);
    auto previous = bank.getChildWithProperty ("index", index);
    if (previous.isValid()) bank.removeChild (previous, nullptr);
    bank.appendChild (snapshot, nullptr);
    std::shared_ptr<const VoiceParameters> parameters = std::make_shared<VoiceParameters> (readParams (snapshot));
    savedLayers[(size_t) index].store (parameters);
    apvts.getParameter ("layer" + juce::String (index + 1) + "Enabled")->setValueNotifyingHost (1.0f);
}

void RetroMatchSynthAudioProcessor::clearLayer (int index)
{
    if (editingLayer.load() == index) selectEditingLayer (-1);
    if (! juce::isPositiveAndBelow (index, VoiceParameters::extraLayerCount)) return;
    auto bank = apvts.state.getChildWithName ("SYNTH_LAYERS");
    auto previous = bank.getChildWithProperty ("index", index);
    if (previous.isValid()) bank.removeChild (previous, nullptr);
    savedLayers[(size_t) index].store (std::shared_ptr<const VoiceParameters> {});
    apvts.getParameter ("layer" + juce::String (index + 1) + "Enabled")->setValueNotifyingHost (0.0f);
}

bool RetroMatchSynthAudioProcessor::loadLayerToMain (int index)
{
    if (! hasLayer (index)) return false;
    auto snapshot = apvts.state.getChildWithName ("SYNTH_LAYERS").getChildWithProperty ("index", index);
    applyEditingSnapshot (snapshot);
    return true;
}

void RetroMatchSynthAudioProcessor::applyEditingSnapshot (const juce::ValueTree& snapshot)
{
    for (auto* parameter : getParameters())
        if (auto* identified = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
            if (! isGlobalRackOrClockParameter (identified->paramID) && snapshot.hasProperty (identified->paramID))
            {
                auto* ranged = apvts.getParameter (identified->paramID);
                ranged->setValueNotifyingHost (ranged->convertTo0to1 ((float) snapshot[identified->paramID]));
            }
    referenceWavetable = ReferenceWavetableData::fromBase64 (snapshot["referenceTable"].toString());
    userWavetable = ReferenceWavetableData::fromBase64 (snapshot["userTable"].toString());
    userWavetableName = userWavetable ? snapshot.getProperty ("userTableName", snapshot["name"]).toString() : juce::String {};
    userWavetableDescription = userWavetable ? snapshot.getProperty ("userTableDescription", "Stored layer wavetable").toString() : juce::String {};
}

void RetroMatchSynthAudioProcessor::restoreLayers()
{
    editingLayer.store (-1); editingMain.store (nullptr); editingMainSnapshot = {};
    const auto bank = apvts.state.getChildWithName ("SYNTH_LAYERS");
    for (int i = 0; i < VoiceParameters::extraLayerCount; ++i)
    {
        const auto snapshot = bank.getChildWithProperty ("index", i);
        std::shared_ptr<const VoiceParameters> parameters;
        if (snapshot.isValid()) parameters = std::make_shared<VoiceParameters> (readParams (snapshot));
        savedLayers[(size_t) i].store (parameters);
    }
}

bool RetroMatchSynthAudioProcessor::loadUserWavetable (const juce::File& file, int sourceFrameSize)
{
    juce::String description;
    auto imported = ReferenceWavetableExtractor::importSet (file, sourceFrameSize, &description);
    if (imported == nullptr || ! imported->valid) return false;

    userWavetable = std::move (imported);
    userWavetableName = file.getFileName();
    userWavetableDescription = description;

    if (auto* parameter = apvts.getParameter ("userWavetableMix"))
    {
        const float current = parameter->convertFrom0to1 (parameter->getValue());
        if (current <= 0.0001f)
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (0.65f));
    }
    return true;
}

void RetroMatchSynthAudioProcessor::clearUserWavetable()
{
    userWavetable.reset();
    userWavetableName.clear();
    userWavetableDescription.clear();
    if (auto* parameter = apvts.getParameter ("userWavetableMix"))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (0.0f));
}

bool RetroMatchSynthAudioProcessor::setReferenceBaseMidiNote (int midiNote)
{
    if (! loadedReferenceFile.existsAsFile()) return false;

    const int note = juce::jlimit (0, 127, midiNote);
    const float expectedHz = midiNoteToHz (note);
    const float start = analysisStartSeconds.load();
    const float end = analysisEndSeconds.load() > start ? analysisEndSeconds.load() : analysisSourceDuration.load();
    auto analysed = SampleAnalyzer::analyzeFile (loadedReferenceFile, expectedHz, start, end);
    if (! analysed) return false;

    currentFeatures = std::move (analysed);
    referenceBaseMidiNote.store (note);
    referencePitchLocked.store (true);
    referencePlayer.setRootMidiNote (note);
    referenceWavetable = ReferenceWavetableExtractor::extract (loadedReferenceFile, expectedHz, start, end);
    invalidateMatchesAfterReferencePitchChange();
    return true;
}

bool RetroMatchSynthAudioProcessor::resetReferenceBaseMidiNote()
{
    if (! loadedReferenceFile.existsAsFile()) return false;
    referencePitchLocked.store (false);
    const float start = analysisStartSeconds.load();
    const float end = analysisEndSeconds.load() > start ? analysisEndSeconds.load() : analysisSourceDuration.load();
    auto analysed = SampleAnalyzer::analyzeFile (loadedReferenceFile, 0.0f, start, end);
    if (! analysed) return false;

    detectedReferenceHz = analysed->fundamentalHz;
    detectedReferencePitchConfidence = analysed->pitchConfidence;
    if (detectedReferenceHz > 20.0f) detectedReferenceMidiNote = hzToNearestMidiNote (detectedReferenceHz);
    referenceBaseMidiNote.store (detectedReferenceMidiNote);
    referencePlayer.setRootMidiNote (detectedReferenceMidiNote);
    currentFeatures = std::move (analysed);
    const float wavetableHz = currentFeatures->fundamentalHz > 20.0f ? currentFeatures->fundamentalHz : midiNoteToHz (detectedReferenceMidiNote);
    referenceWavetable = ReferenceWavetableExtractor::extract (loadedReferenceFile, wavetableHz, start, end);
    invalidateMatchesAfterReferencePitchChange();
    return true;
}

float RetroMatchSynthAudioProcessor::getMidiAnalysisStartSeconds() const noexcept
{
    const float duration = analysisSourceDuration.load();
    return juce::jlimit (0.0f, juce::jmax (0.0f, duration), (float) apvts.state.getProperty ("midiAnalysisStart", 0.0f));
}

float RetroMatchSynthAudioProcessor::getMidiAnalysisEndSeconds() const noexcept
{
    const float duration = analysisSourceDuration.load();
    const float start = getMidiAnalysisStartSeconds();
    return juce::jlimit (start, juce::jmax (start, duration), (float) apvts.state.getProperty ("midiAnalysisEnd", duration));
}

void RetroMatchSynthAudioProcessor::setMidiAnalysisRegion (float startSeconds, float endSeconds)
{
    const float duration = analysisSourceDuration.load();
    if (duration <= 0.0f) return;
    const float start = juce::jlimit (0.0f, juce::jmax (0.0f, duration - 0.002f), startSeconds);
    const float end = juce::jlimit (start + 0.002f, duration, endSeconds);
    apvts.state.setProperty ("midiAnalysisStart", start, nullptr);
    apvts.state.setProperty ("midiAnalysisEnd", end, nullptr);
}

bool RetroMatchSynthAudioProcessor::previewReferenceRegion (float startSeconds, float endSeconds, bool normalize,
                                                            float fadeInSeconds, float fadeOutSeconds)
{
    if (! loadedReferenceFile.existsAsFile()) return false;
    setReferenceAuditionMode (ReferenceAuditionMode::referenceOnly);
    return referencePlayer.previewRegion (loadedReferenceFile, referenceBaseMidiNote.load(), startSeconds, endSeconds,
                                          normalize, fadeInSeconds, fadeOutSeconds);
}

void RetroMatchSynthAudioProcessor::stopReferencePreview()
{
    referencePlayer.stopPreview();
}

bool RetroMatchSynthAudioProcessor::exportReferenceSelection (const juce::File& destination,
                                                              float startSeconds, float endSeconds, bool normalize,
                                                              float fadeInSeconds, float fadeOutSeconds)
{
    if (! loadedReferenceFile.existsAsFile()) return false;
    return ReferenceSamplePlayer::writeProcessedRegion (loadedReferenceFile, destination, startSeconds, endSeconds,
                                                        normalize, fadeInSeconds, fadeOutSeconds);
}

void RetroMatchSynthAudioProcessor::setReferenceAuditionMode (ReferenceAuditionMode mode)
{
    melodyTransport.stop();
    const int value = juce::jlimit ((int) ReferenceAuditionMode::synthOnly,
                                    (int) ReferenceAuditionMode::mixed,
                                    (int) mode);
    referencePlayer.stopPreview();
    allEditorNotesOff();
    referenceAuditionMode.store (value);
}

void RetroMatchSynthAudioProcessor::noteOnFromEditor (int midiNote, float velocity)
{
    const juce::ScopedLock lock (editorMidiLock);
    editorMidi.addEvent (juce::MidiMessage::noteOn (1, juce::jlimit (0, 127, midiNote), juce::jlimit (0.0f, 1.0f, velocity)), 0);
}

void RetroMatchSynthAudioProcessor::noteOffFromEditor (int midiNote, float velocity)
{
    const juce::ScopedLock lock (editorMidiLock);
    editorMidi.addEvent (juce::MidiMessage::noteOff (1, juce::jlimit (0, 127, midiNote), velocity), 0);
}

void RetroMatchSynthAudioProcessor::allEditorNotesOff()
{
    const juce::ScopedLock lock (editorMidiLock);
    editorMidi.clear();
    editorMidi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
}

void RetroMatchSynthAudioProcessor::applyMatchResult (const MatchResult& result)
{
    if (! MatchSafetyPolicy::compatible (result.params))
        return;
    // Results with a populated safety score were rendered candidates. Never
    // apply one that failed telemetry; hand-authored/live snapshots use the
    // sentinel score and remain governed by parameter compatibility alone.
    if (result.technicalSafetyScore >= 0.0f && ! result.technicallySafe)
        return;

    auto set = [this] (const juce::String& id, float x)
    {
        if (auto* param = apvts.getParameter (id))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->convertTo0to1 (x));
            param->endChangeGesture();
        }
    };

    const auto& q = result.params;
    set ("osc1Wave", (float) q.osc1Wave); set ("osc2Wave", (float) q.osc2Wave);
    set ("osc1Mix", q.osc1Mix); set ("osc2Mix", q.osc2Mix); set ("subMix", q.subMix);
    set ("noise", q.noiseMix); set ("ringMix", q.ringMix); set ("additiveMix", q.additiveMix); set ("masterTune", q.masterTuneCents);
    set ("osc2Semi", q.osc2Semitones); set ("osc2Detune", q.osc2Detune); set ("pulseWidth", q.pulseWidth);
    set ("wavetableMix", q.wavetableMix); set ("wavetablePosition", q.wavetablePosition); set ("wavetableWarp", q.wavetableWarp); set ("referenceWavetableMix", q.referenceWavetableMix);
    set ("supersawMix", q.supersawMix); set ("unisonDetune", q.unisonDetune); set ("unisonSpread", q.unisonSpread); set ("wavefold", q.wavefold);
    set ("fmAmount", q.fmAmount); set ("fmRatio", q.fmRatio); set ("fmMix", q.fmMix); set ("fmFeedback", q.fmFeedback); set ("fmAlgorithm", (float) q.fmAlgorithm);
    for (int i = 0; i < VoiceParameters::fmOperatorCount; ++i)
    {
        const auto index = juce::String (i + 1);
        set ("fmOp" + index + "Ratio", q.fmOpRatio[(size_t) i]);
        set ("fmOp" + index + "Level", q.fmOpLevel[(size_t) i]);
        set ("fmOp" + index + "Mode", (float) q.fmOpFixedMode[(size_t) i]);
        set ("fmOp" + index + "FixedHz", q.fmOpFixedHz[(size_t) i]);
        set ("fmOp" + index + "Attack", q.fmOpAttack[(size_t) i]);
        set ("fmOp" + index + "Decay", q.fmOpDecay[(size_t) i]);
        set ("fmOp" + index + "Sustain", q.fmOpSustain[(size_t) i]);
        set ("fmOp" + index + "Release", q.fmOpRelease[(size_t) i]);
        set ("fmOp" + index + "KeyScale", q.fmOpKeyScale[(size_t) i]);
        set ("fmOp" + index + "Velocity", q.fmOpVelocity[(size_t) i]);
    }
    set ("harmonicTilt", q.harmonicTilt); set ("oddEven", q.oddEvenBalance);

    set ("attack", q.attack); set ("decay", q.decay); set ("sustain", q.sustain); set ("release", q.release);
    set ("cutoff", q.cutoff); set ("resonance", q.resonance); set ("filterType", (float) q.filterType);
    set ("lfoRate", q.lfoRate); set ("lfoPitch", q.lfoPitch); set ("lfoCutoff", q.lfoCutoff); set ("lfoAmp", q.lfoAmp);
    for (int i = 0; i < VoiceParameters::modSlotCount; ++i)
    {
        const auto index = juce::String (i + 1);
        set ("mod" + index + "Source", (float) q.modSlots[(size_t) i].source);
        set ("mod" + index + "Dest", (float) q.modSlots[(size_t) i].destination);
        set ("mod" + index + "Amount", q.modSlots[(size_t) i].amount);
    }

    for (int i = 0; i < FxModuleParameters::slotCount; ++i)
    {
        const auto prefix = "fxModule" + juce::String (i + 1); const auto& module = q.fxModules[(size_t) i];
        set (prefix + "Type", (float) module.type); set (prefix + "Stage", (float) module.stage); set (prefix + "Bypass", module.bypass ? 1.0f : 0.0f);
        set (prefix + "Amount", module.amount); set (prefix + "Rate", module.rate); set (prefix + "Feedback", module.feedback); set (prefix + "Mix", module.mix);
        set (prefix + "TempoSync", module.tempoSync ? 1.0f : 0.0f); set (prefix + "Division", (float) module.tempoDivision);
    }
    for (int i = 0; i < 3; ++i)
    {
        const auto prefix = "lfoModule" + juce::String (i + 2); set (prefix + "Rate", q.extraLfoRate[(size_t) i]); set (prefix + "Shape", (float) q.extraLfoShape[(size_t) i]);
    }
    for (int i = 0; i < 4; ++i)
    {
        const auto prefix = "moduleMod" + juce::String (i + 1); const auto& slot = q.moduleModSlots[(size_t) i];
        set (prefix + "Source", (float) slot.source); set (prefix + "Dest", (float) slot.destination); set (prefix + "Amount", slot.amount);
    }
    set ("userWavetableMix", q.userWavetableMix);
    set ("msegEnabled", q.mseg.enabled ? 1.0f : 0.0f);
    set ("msegTarget", (float) q.msegTarget);
    set ("msegDepth", q.msegDepth);
    set ("msegLoopEnabled", q.mseg.loopEnabled ? 1.0f : 0.0f);
    set ("msegLoopStart", (float) q.mseg.loopStartPoint); set ("msegLoopEnd", (float) q.mseg.loopEndPoint - 1);
    for (int i = 0; i < MsegParameters::pointCount; ++i) set ("msegLevel" + juce::String (i + 1), q.mseg.levels[(size_t) i]);
    for (int i = 0; i < MsegParameters::segmentCount; ++i)
    { set ("msegTime" + juce::String (i + 1), q.mseg.times[(size_t) i]); set ("msegCurve" + juce::String (i + 1), q.mseg.curves[(size_t) i]); }
    for (int i = 0; i < VoiceParameters::modGraphSlotCount; ++i)
    {
        const auto prefix = "modGraph" + juce::String (i + 1); const auto& slot = q.modGraphSlots[(size_t) i];
        set (prefix + "Source", (float) slot.source); set (prefix + "Dest", (float) slot.destination); set (prefix + "Amount", slot.amount);
    }
    set ("drive", q.drive); set ("distortionMode", (float) q.distortionMode); set ("distortionMix", q.distortionMix);
    set ("chorusMix", q.chorusMix); set ("chorusRate", q.chorusRate); set ("chorusDepth", q.chorusDepth);
    set ("delayMix", q.delayMix); set ("delayTime", q.delayTime); set ("delayFeedback", q.delayFeedback);
    set ("reverbMix", q.reverbMix); set ("reverbSize", q.reverbSize); set ("reverbDamping", q.reverbDamping);
    set ("stereoWidth", q.stereoWidth); set ("outputGain", q.outputGainDb);

    // The winning voice owns voice/module settings. The generated rack lifecycle
    // is handled separately so selecting/refining a candidate cannot accidentally
    // inherit unrelated previously loaded synth instances.
    lastMatch = result;
    updateCandidatePreview (result);
}

void RetroMatchSynthAudioProcessor::applyGeneratedRack (const MatchResult& mainResult, int selectedBankIndex)
{
    // Validate the complete rack before touching APVTS or the stored layer bank.
    // Gold must never leave a half-applied main/layer combination behind.
    if (! MatchSafetyPolicy::compatible (mainResult.params))
        return;
    if (mainResult.technicalSafetyScore >= 0.0f && ! mainResult.technicallySafe)
        return;

    selectEditingLayer (-1);
    allEditorNotesOff();
    for (int i = 0; i < VoiceParameters::extraLayerCount; ++i) clearLayer (i);

    const int strategy = mainResult.algorithm >= 0
                       ? juce::jlimit (0, 6, mainResult.algorithm)
                       : juce::jlimit (0, 6, (int) apvts.getRawParameterValue ("resynthStrategy")->load());
    const int currentComplexity = juce::jlimit (0, 3, (int) apvts.getRawParameterValue ("resynthComplexity")->load());
    const int complexity = mainResult.complexity >= 0 ? juce::jlimit (0, 3, mainResult.complexity) : currentComplexity;

    bool hasEmbeddedRack = false;
    for (const auto& layer : mainResult.params.layers) hasEmbeddedRack |= layer != nullptr;

    auto setGlobal = [this] (const juce::String& id, float value)
    {
        if (auto* parameter = apvts.getParameter (id))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    };
    for (int i = 0; i < FxModuleParameters::slotCount; ++i)
    {
        const auto prefix = "globalFxModule" + juce::String (i + 1);
        const FxModuleParameters empty;
        const auto& module = hasEmbeddedRack && mainResult.fullRackScore
                           ? mainResult.params.globalFxModules[(size_t) i] : empty;
        setGlobal (prefix + "Type", (float) module.type);
        setGlobal (prefix + "Stage", (float) module.stage);
        setGlobal (prefix + "Bypass", module.bypass ? 1.0f : 0.0f);
        setGlobal (prefix + "Amount", module.amount);
        setGlobal (prefix + "Rate", module.rate);
        setGlobal (prefix + "Feedback", module.feedback);
        setGlobal (prefix + "Mix", module.mix);
        setGlobal (prefix + "TempoSync", module.tempoSync ? 1.0f : 0.0f);
        setGlobal (prefix + "Division", (float) module.tempoDivision);
    }

    if (hasEmbeddedRack)
    {
        if (auto* mainGain = apvts.getParameter ("mainLayerGain"))
            mainGain->setValueNotifyingHost (mainGain->convertTo0to1 (mainResult.params.mainLayerGain));

        MatchResult mainOnly = mainResult;
        mainOnly.params.layers.fill (nullptr);
        applyMatchResult (mainOnly);

        static const char* roleNames[] { "BODY", "AIR", "FOUNDATION", "MOTION", "HARMONIC", "WIDTH", "TEXTURE" };
        for (int layer = 0; layer < VoiceParameters::extraLayerCount; ++layer)
        {
            const auto& storedLayer = mainResult.params.layers[(size_t) layer];
            if (! storedLayer) continue;
            MatchResult layerResult; layerResult.params = *storedLayer;
            applyMatchResult (layerResult);
            captureLayer (layer);

            auto bank = apvts.state.getChildWithName ("SYNTH_LAYERS");
            auto stored = bank.getChildWithProperty ("index", layer);
            if (stored.isValid()) stored.setProperty ("name", "GOLD / " + juce::String (roleNames[layer]), nullptr);

            const auto prefix = "layer" + juce::String (layer + 1);
            auto setLayer = [this, &prefix] (const char* suffix, float value)
            {
                if (auto* parameter = apvts.getParameter (prefix + suffix))
                    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
            };
            setLayer ("Gain", mainResult.params.layerGain[(size_t) layer]);
            setLayer ("Pan", mainResult.params.layerPan[(size_t) layer]);
            setLayer ("Tune", mainResult.params.layerTune[(size_t) layer]);
            setLayer ("Operation", (float) mainResult.params.layerOperation[(size_t) layer]);
            setLayer ("Amount", mainResult.params.layerAmount[(size_t) layer]);
        }

        // Restore the exact measured main voice and its full-rack similarity metadata.
        applyMatchResult (mainResult);
        return;
    }

    if (auto* mainGain = apvts.getParameter ("mainLayerGain"))
        mainGain->setValueNotifyingHost (mainGain->convertTo0to1 (1.0f));
    applyMatchResult (mainResult);
    const int legacyInstances = juce::jlimit (1, 3, 1 + (int) apvts.getRawParameterValue ("resynthInstances")->load());
    const int totalInstances = GeneratedRackGainPolicy::totalInstancesForComplexity (complexity, legacyInstances);

    std::array<int, 2> complement {{ -1, -1 }};
    int complementCount = 0;
    for (int candidate = 0; candidate < 3 && complementCount < 2; ++candidate)
        if (candidate != selectedBankIndex && candidateBank[(size_t) candidate].confidence > 0.0f)
            complement[(size_t) complementCount++] = candidate;
    if (complementCount == 2 && candidateBank[(size_t) complement[1]].similarity.total > candidateBank[(size_t) complement[0]].similarity.total)
        std::swap (complement[0], complement[1]);

    static const char* roleNames[] { "BODY", "AIR", "FOUNDATION", "MOTION", "HARMONIC", "WIDTH", "TEXTURE" };
    static const float roleGain[] { 0.30f, 0.18f, 0.24f, 0.20f, 0.17f, 0.16f, 0.13f };
    static const float rolePan[]  { -0.10f, 0.34f, 0.0f, -0.30f, 0.18f, 0.42f, -0.42f };
    static const float roleTune[] { 0.0f, 12.0f, -12.0f, 0.0f, 7.0f, 0.0f, 12.0f };

    const int wantedLayers = juce::jmin (VoiceParameters::extraLayerCount, totalInstances - 1);
    for (int layer = 0; layer < wantedLayers; ++layer)
    {
        VoiceParameters source = mainResult.params;
        if (layer < complementCount) source = candidateBank[(size_t) complement[(size_t) layer]].params;
        source.layers.fill (nullptr);
        auto companion = makeResynthCompanion (source, layer, strategy, referenceWavetable);
        MatchResult layerResult; layerResult.params = companion;
        applyMatchResult (layerResult);
        captureLayer (layer);

        auto bank = apvts.state.getChildWithName ("SYNTH_LAYERS");
        auto stored = bank.getChildWithProperty ("index", layer);
        if (stored.isValid()) stored.setProperty ("name", "RESYNTH / " + juce::String (roleNames[layer]), nullptr);

        const auto prefix = "layer" + juce::String (layer + 1);
        auto setLayer = [this, &prefix] (const char* suffix, float value)
        {
            if (auto* parameter = apvts.getParameter (prefix + suffix))
                parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
        };
        setLayer ("Gain", roleGain[layer]);
        setLayer ("Pan", rolePan[layer]);
        setLayer ("Tune", roleTune[layer]);
        setLayer ("Operation", 0.0f);
        setLayer ("Amount", 0.78f);
    }

    applyMatchResult (mainResult);
}

void RetroMatchSynthAudioProcessor::updateCandidatePreview (const MatchResult& result)
{
    if (result.candidateFeatures.duration > 0.0f) currentCandidateFeatures = result.candidateFeatures;
}

MatchResult RetroMatchSynthAudioProcessor::fitReference()
{
    if (! currentFeatures) return {};
    auto seed = SoundMatcher::initialFit (*currentFeatures);
    const auto authored = getMainVoiceParameters();
    const int strategy = juce::jlimit (0, 6, (int) apvts.getRawParameterValue ("resynthStrategy")->load());
    auto strategyTable = referenceWavetable;
    if (strategy == 5 && loadedReferenceFile.existsAsFile())
        if (auto chopped = ReferenceWavetableExtractor::chop (loadedReferenceFile, analysisStartSeconds.load(), analysisEndSeconds.load()))
            strategyTable = std::move (chopped);
    seed.params.referenceWavetable = strategyTable;
    seed.params.referenceWavetableMix = strategyTable ? referenceTableWeight (*currentFeatures, strategy) : 0.0f;
    seed.params.userWavetable = userWavetable;
    seed.params.userWavetableMix = authored.userWavetableMix;
    seed.params.distortionMode = matchSettings.lockEffects ? authored.distortionMode : 0;
    seed.params.distortionMix = matchSettings.lockEffects ? authored.distortionMix : 1.0f;
    auto fitSettings = matchSettings; fitSettings.algorithm = strategy;
    auto evaluated = SoundMatcher::evaluateFit (*currentFeatures, seed.params, fitSettings);
    evaluated.explanation = seed.explanation + " Initial rendered similarity: " + juce::String (evaluated.similarity.total * 100.0f, 1) + "%";
    applyGeneratedRack (evaluated, -1);
    return evaluated;
}

MatchResult RetroMatchSynthAudioProcessor::refineReference (SoundMatcher::ProgressCallback progress, SoundMatcher::CancelCallback cancel)
{
    if (! currentFeatures) return {};
    auto settings = matchSettings;
    const int strategy = juce::jlimit (0, 6, (int) apvts.getRawParameterValue ("resynthStrategy")->load());
    settings.algorithm = strategy;
    const auto reference = *currentFeatures;
    const auto authored = getMainVoiceParameters();
    auto seed = lastMatch.confidence > 0.0f ? lastMatch.params : SoundMatcher::initialFit (reference).params;
    auto strategyTable = referenceWavetable;
    if (strategy == 5 && loadedReferenceFile.existsAsFile())
        if (auto chopped = ReferenceWavetableExtractor::chop (loadedReferenceFile, analysisStartSeconds.load(), analysisEndSeconds.load()))
            strategyTable = std::move (chopped);
    seed.referenceWavetable = strategyTable;
    if (strategyTable) seed.referenceWavetableMix = juce::jmax (seed.referenceWavetableMix, referenceTableWeight (reference, strategy));
    seed.userWavetable = userWavetable;
    seed.userWavetableMix = authored.userWavetableMix;
    seed.layers.fill (nullptr); seed.mainLayerGain = 1.0f;
    seed.distortionMode = matchSettings.lockEffects ? authored.distortionMode : 0;
    seed.distortionMix = matchSettings.lockEffects ? authored.distortionMix : 1.0f;
    return SoundMatcher::refineFit (reference, seed, settings, std::move (progress), std::move (cancel));
}

juce::AudioProcessorValueTreeState::ParameterLayout RetroMatchSynthAudioProcessor::createLayout()
{
    using P = juce::AudioParameterFloat;
    using C = juce::AudioParameterChoice;
    using B = juce::AudioParameterBool;
    juce::AudioProcessorValueTreeState::ParameterLayout l;

    l.add (std::make_unique<C> ("osc1Wave", "OSC 1 Wave", juce::StringArray { "Sine", "Saw", "Square", "Triangle", "Pulse" }, 1));
    l.add (std::make_unique<C> ("osc2Wave", "OSC 2 Wave", juce::StringArray { "Sine", "Saw", "Square", "Triangle", "Pulse" }, 0));
    l.add (std::make_unique<P> ("osc1Mix", "OSC 1 Mix", juce::NormalisableRange<float> (0, 1), 0.75f));
    l.add (std::make_unique<P> ("osc2Mix", "OSC 2 Mix", juce::NormalisableRange<float> (0, 1), 0.35f));
    l.add (std::make_unique<P> ("subMix", "Sub Mix", juce::NormalisableRange<float> (0, 0.65f), 0.0f));
    l.add (std::make_unique<P> ("noise", "Noise", juce::NormalisableRange<float> (0, 0.65f), 0.0f));
    l.add (std::make_unique<P> ("ringMix", "Ring Mod", juce::NormalisableRange<float> (0, 0.55f), 0.0f));
    l.add (std::make_unique<P> ("additiveMix", "Additive Mix", juce::NormalisableRange<float> (0, 0.85f), 0.0f));
    l.add (std::make_unique<P> ("masterTune", "Master Tune", juce::NormalisableRange<float> (-100, 100, 0.1f), 0.0f));
    l.add (std::make_unique<P> ("osc2Semi", "OSC 2 Semitones", juce::NormalisableRange<float> (-24, 24, 1), 0));
    l.add (std::make_unique<P> ("osc2Detune", "OSC 2 Detune", juce::NormalisableRange<float> (-50, 50, 0.1f), 0));
    l.add (std::make_unique<P> ("pulseWidth", "Pulse Width", juce::NormalisableRange<float> (0.08f, 0.92f), 0.5f));
    l.add (std::make_unique<P> ("wavetableMix", "Wavetable Mix", juce::NormalisableRange<float> (0, 1), 0.0f));
    l.add (std::make_unique<P> ("wavetablePosition", "Wavetable Position", juce::NormalisableRange<float> (0, 1), 0.25f));
    l.add (std::make_unique<P> ("wavetableWarp", "Wavetable Warp", juce::NormalisableRange<float> (-1, 1), 0.0f));
    l.add (std::make_unique<P> ("referenceWavetableMix", "Reference Wavetable Mix", juce::NormalisableRange<float> (0, 1), 0.0f));
    l.add (std::make_unique<P> ("supersawMix", "Supersaw Mix", juce::NormalisableRange<float> (0, 1), 0.0f));
    l.add (std::make_unique<P> ("unisonDetune", "Unison Detune", juce::NormalisableRange<float> (0, 70, 0.1f, 0.55f), 18.0f));
    l.add (std::make_unique<P> ("unisonSpread", "Unison Spread", juce::NormalisableRange<float> (0, 1), 0.72f));
    l.add (std::make_unique<P> ("wavefold", "Wavefold", juce::NormalisableRange<float> (0, 1), 0.0f));
    l.add (std::make_unique<P> ("fmAmount", "FM Amount", juce::NormalisableRange<float> (0, 0.65f), 0));
    l.add (std::make_unique<P> ("fmRatio", "FM Ratio", juce::NormalisableRange<float> (0.25f, 8, 0.01f, 0.45f), 2));
    l.add (std::make_unique<P> ("fmMix", "6-OP FM Mix", juce::NormalisableRange<float> (0, 1), 0));
    l.add (std::make_unique<P> ("fmFeedback", "FM Feedback", juce::NormalisableRange<float> (0, 1), 0));
    l.add (std::make_unique<C> ("fmAlgorithm", "FM Algorithm", juce::StringArray { "Stack", "Dual Stack", "Triple Pair", "Star", "Branch", "Six Carriers" }, 0));
    for (int i = 0; i < VoiceParameters::fmOperatorCount; ++i)
    {
        const auto index = juce::String (i + 1);
        l.add (std::make_unique<P> ("fmOp" + index + "Ratio", "FM OP" + index + " Ratio", juce::NormalisableRange<float> (0.125f, 16.0f, 0.001f, 0.42f), i == 0 ? 1.0f : (i == 1 ? 2.0f : (i == 2 ? 3.0f : 1.0f))));
        const float defaultLevel[] = { 1.0f, 0.55f, 0.35f, 0.25f, 0.18f, 0.12f };
        l.add (std::make_unique<P> ("fmOp" + index + "Level", "FM OP" + index + " Level", juce::NormalisableRange<float> (0, 1.25f), defaultLevel[i]));
        l.add (std::make_unique<C> ("fmOp" + index + "Mode", "FM OP" + index + " Frequency Mode", juce::StringArray { "Ratio", "Fixed" }, 0));
        const float defaultFixedHz[] = { 440.0f, 880.0f, 1320.0f, 440.0f, 440.0f, 440.0f };
        const float defaultDecay[] = { 0.45f, 0.32f, 0.22f, 0.35f, 0.30f, 0.25f };
        const float defaultSustain[] = { 1.0f, 0.72f, 0.52f, 0.65f, 0.55f, 0.45f };
        const float defaultRelease[] = { 0.30f, 0.20f, 0.16f, 0.25f, 0.22f, 0.18f };
        const float defaultKeyScale[] = { 0.0f, 0.12f, 0.18f, 0.08f, 0.16f, 0.22f };
        const float defaultVelocity[] = { 0.35f, 0.55f, 0.65f, 0.45f, 0.50f, 0.55f };
        l.add (std::make_unique<P> ("fmOp" + index + "FixedHz", "FM OP" + index + " Fixed Hz", juce::NormalisableRange<float> (10.0f, 16000.0f, 0.0f, 0.25f), defaultFixedHz[i]));
        l.add (std::make_unique<P> ("fmOp" + index + "Attack", "FM OP" + index + " Attack", juce::NormalisableRange<float> (0.001f, 5.0f, 0.0f, 0.3f), 0.005f));
        l.add (std::make_unique<P> ("fmOp" + index + "Decay", "FM OP" + index + " Decay", juce::NormalisableRange<float> (0.001f, 5.0f, 0.0f, 0.3f), defaultDecay[i]));
        l.add (std::make_unique<P> ("fmOp" + index + "Sustain", "FM OP" + index + " Sustain", juce::NormalisableRange<float> (0, 1), defaultSustain[i]));
        l.add (std::make_unique<P> ("fmOp" + index + "Release", "FM OP" + index + " Release", juce::NormalisableRange<float> (0.001f, 8.0f, 0.0f, 0.3f), defaultRelease[i]));
        l.add (std::make_unique<P> ("fmOp" + index + "KeyScale", "FM OP" + index + " Key Scale", juce::NormalisableRange<float> (0, 1), defaultKeyScale[i]));
        l.add (std::make_unique<P> ("fmOp" + index + "Velocity", "FM OP" + index + " Velocity", juce::NormalisableRange<float> (0, 1), defaultVelocity[i]));
    }
    l.add (std::make_unique<P> ("harmonicTilt", "Harmonic Tilt", juce::NormalisableRange<float> (0.45f, 3.5f, 0.01f, 0.6f), 1.35f));
    l.add (std::make_unique<P> ("oddEven", "Odd Even Balance", juce::NormalisableRange<float> (0, 1), 0.5f));

    l.add (std::make_unique<P> ("attack", "Attack", juce::NormalisableRange<float> (0.001f, 5, 0, 0.3f), 0.01f));
    l.add (std::make_unique<P> ("decay", "Decay", juce::NormalisableRange<float> (0.001f, 5, 0, 0.3f), 0.25f));
    l.add (std::make_unique<P> ("sustain", "Sustain", juce::NormalisableRange<float> (0, 1), 0.75f));
    l.add (std::make_unique<P> ("release", "Release", juce::NormalisableRange<float> (0.001f, 8, 0, 0.3f), 0.35f));
    l.add (std::make_unique<P> ("cutoff", "Cutoff", juce::NormalisableRange<float> (20, 20000, 0, 0.22f), 12000));
    l.add (std::make_unique<P> ("resonance", "Resonance", juce::NormalisableRange<float> (0.01f, 0.99f), 0.15f));
    l.add (std::make_unique<C> ("filterType", "Filter", juce::StringArray { "Low-pass", "High-pass", "Band-pass" }, 0));

    l.add (std::make_unique<P> ("lfoRate", "LFO Rate", juce::NormalisableRange<float> (0.02f, 30, 0, 0.3f), 1.5f));
    l.add (std::make_unique<P> ("lfoPitch", "LFO Pitch", juce::NormalisableRange<float> (0, 2), 0));
    l.add (std::make_unique<P> ("lfoCutoff", "LFO Cutoff", juce::NormalisableRange<float> (0, 3), 0));
    l.add (std::make_unique<P> ("lfoAmp", "LFO Amp", juce::NormalisableRange<float> (0, 1), 0));

    const juce::StringArray modSources { "Off", "LFO 1", "Velocity", "Key Track", "Random Note", "Amp Env" };
    const juce::StringArray actualModDestinations { "Off", "Pitch", "Cutoff", "Amplitude", "Pulse Width", "FM Amount", "6-OP FM Mix", "Wavetable Position", "Wavefold" };
    for (int i = 0; i < VoiceParameters::modSlotCount; ++i)
    {
        const auto index = juce::String (i + 1);
        l.add (std::make_unique<C> ("mod" + index + "Source", "Mod " + index + " Source", modSources, 0));
        l.add (std::make_unique<C> ("mod" + index + "Dest", "Mod " + index + " Destination", actualModDestinations, 0));
        l.add (std::make_unique<P> ("mod" + index + "Amount", "Mod " + index + " Amount", juce::NormalisableRange<float> (-1, 1), 0));
    }

    l.add (std::make_unique<P> ("drive", "Drive", juce::NormalisableRange<float> (0, 1), 0));
    l.add (std::make_unique<P> ("chorusMix", "Chorus Mix", juce::NormalisableRange<float> (0, 1), 0));
    l.add (std::make_unique<P> ("chorusRate", "Chorus Rate", juce::NormalisableRange<float> (0.02f, 10.0f, 0, 0.35f), 0.35f));
    l.add (std::make_unique<P> ("chorusDepth", "Chorus Depth", juce::NormalisableRange<float> (0, 1), 0.25f));
    l.add (std::make_unique<P> ("delayMix", "Delay Mix", juce::NormalisableRange<float> (0, 1), 0));
    l.add (std::make_unique<P> ("delayTime", "Delay Time", juce::NormalisableRange<float> (0.02f, 1.8f, 0, 0.35f), 0.28f));
    l.add (std::make_unique<P> ("delayFeedback", "Delay Feedback", juce::NormalisableRange<float> (0, 0.92f), 0.22f));
    l.add (std::make_unique<P> ("reverbMix", "Reverb Mix", juce::NormalisableRange<float> (0, 1), 0));
    l.add (std::make_unique<P> ("reverbSize", "Reverb Size", juce::NormalisableRange<float> (0, 1), 0.45f));
    l.add (std::make_unique<P> ("reverbDamping", "Reverb Damping", juce::NormalisableRange<float> (0, 1), 0.45f));
    l.add (std::make_unique<P> ("stereoWidth", "Stereo Width", juce::NormalisableRange<float> (0, 2), 1.0f));
    l.add (std::make_unique<P> ("outputGain", "Output Gain", juce::NormalisableRange<float> (-18, 6, 0.1f), -3.0f));

    l.add (std::make_unique<C> ("oversamplingQuality", "Nonlinear Oversampling", juce::StringArray { "1x", "2x", "4x" }, 0));

    l.add (std::make_unique<B> ("msegEnabled", "MSEG 1 Enabled", false));
    l.add (std::make_unique<B> ("msegLoopEnabled", "MSEG 1 Loop Enabled", false));
    l.add (std::make_unique<C> ("msegLoopStart", "MSEG 1 Loop Start", juce::StringArray { "P1", "P2", "P3", "P4", "P5" }, 1));
    l.add (std::make_unique<C> ("msegLoopEnd", "MSEG 1 Loop End", juce::StringArray { "P2", "P3", "P4", "P5", "P6" }, 2));
    l.add (std::make_unique<C> ("msegTarget", "MSEG 1 Direct Target", actualModDestinations, (int) ModDestination::amplitude));
    l.add (std::make_unique<P> ("msegDepth", "MSEG 1 Direct Depth", juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 1.0f));

    const float defaultMsegLevels[] = { 0.0f, 1.0f, 0.78f, 0.58f, 0.28f, 0.0f };
    for (int i = 0; i < MsegParameters::pointCount; ++i)
    {
        const auto index = juce::String (i + 1);
        l.add (std::make_unique<P> ("msegLevel" + index, "MSEG 1 Point " + index + " Level", juce::NormalisableRange<float> (0, 1), defaultMsegLevels[i]));
    }

    const float defaultMsegTimes[] = { 0.025f, 0.090f, 0.180f, 0.320f, 0.420f };
    const float defaultMsegCurves[] = { 0.15f, -0.10f, 0.0f, 0.10f, -0.15f };
    for (int i = 0; i < MsegParameters::segmentCount; ++i)
    {
        const auto index = juce::String (i + 1);
        l.add (std::make_unique<P> ("msegTime" + index, "MSEG 1 Segment " + index + " Time", juce::NormalisableRange<float> (0.001f, 12.0f, 0.0f, 0.30f), defaultMsegTimes[i]));
        l.add (std::make_unique<P> ("msegCurve" + index, "MSEG 1 Segment " + index + " Curve", juce::NormalisableRange<float> (-1, 1), defaultMsegCurves[i]));
    }

    const juce::StringArray graphSources { "Off", "LFO 1", "Velocity", "Key Track", "Random Note", "Amp Env", "MSEG 1" };
    const juce::StringArray graphDestinations { "Off", "Pitch", "Cutoff", "Amplitude", "Pulse Width", "FM Amount", "6-OP FM Mix", "Wavetable Position", "Wavefold" };
    for (int i = 0; i < VoiceParameters::modGraphSlotCount; ++i)
    {
        const auto index = juce::String (i + 1);
        l.add (std::make_unique<C> ("modGraph" + index + "Source", "Graph " + index + " Source", graphSources, 0));
        l.add (std::make_unique<C> ("modGraph" + index + "Dest", "Graph " + index + " Destination", graphDestinations, 0));
        l.add (std::make_unique<P> ("modGraph" + index + "Amount", "Graph " + index + " Amount", juce::NormalisableRange<float> (-1, 1), 0));
    }

    l.add (std::make_unique<P> ("userWavetableMix", "User Wavetable Mix", juce::NormalisableRange<float> (0, 1), 0.0f));
    l.add (std::make_unique<juce::AudioParameterChoice> ("distortionMode", "Distortion Mode", juce::StringArray { "Soft saturation", "Hard clip", "Sine fold" }, 0));
    l.add (std::make_unique<P> ("distortionMix", "Distortion Mix", juce::NormalisableRange<float> (0, 1), 1.0f));
    l.add (std::make_unique<P> ("mainLayerGain", "Main Layer Level", juce::NormalisableRange<float> (0, 1), 1.0f));
    for (int i = 1; i <= VoiceParameters::extraLayerCount; ++i)
    {
        const auto prefix = "layer" + juce::String (i);
        const auto name = "Layer " + juce::String (i + 1);
        l.add (std::make_unique<juce::AudioParameterBool> (prefix + "Enabled", name + " Enabled", false));
        l.add (std::make_unique<P> (prefix + "Gain", name + " Level", juce::NormalisableRange<float> (0, 1), 0.5f));
        l.add (std::make_unique<P> (prefix + "Pan", name + " Pan", juce::NormalisableRange<float> (-1, 1), 0.0f));
        l.add (std::make_unique<P> (prefix + "Tune", name + " Tune", juce::NormalisableRange<float> (-24, 24, 0.01f), 0.0f));
    }
    juce::StringArray moduleTypes;
    for (const auto& descriptor : fxModuleCatalog) moduleTypes.add (descriptor.name);
    for (int i = 1; i <= FxModuleParameters::slotCount; ++i)
    {
        const auto prefix = "fxModule" + juce::String (i);
        l.add (std::make_unique<C> (prefix + "Type", prefix + " Type", moduleTypes, 0));
        l.add (std::make_unique<C> (prefix + "Stage", prefix + " Stage", juce::StringArray { "PRE", "POST" }, 0));
        l.add (std::make_unique<juce::AudioParameterBool> (prefix + "Bypass", prefix + " Bypass", false));
        for (const auto* suffix : { "Amount", "Rate", "Feedback", "Mix" })
            l.add (std::make_unique<P> (prefix + suffix, prefix + " " + suffix, juce::NormalisableRange<float> (0, 1), juce::String (suffix) == "Rate" || juce::String (suffix) == "Feedback" ? 0.25f : 0.5f));
    }
    for (int i = 2; i <= 4; ++i)
    {
        const auto prefix = "lfoModule" + juce::String (i);
        l.add (std::make_unique<P> (prefix + "Rate", prefix + " Rate", juce::NormalisableRange<float> (0.01f, 30.0f, 0, 0.35f), i == 2 ? 0.5f : i == 3 ? 2.0f : 5.0f));
        l.add (std::make_unique<C> (prefix + "Shape", prefix + " Shape", juce::StringArray { "Sine", "Triangle", "Square", "Ramp" }, 0));
    }
    for (int i = 1; i <= 4; ++i)
    {
        const auto prefix = "moduleMod" + juce::String (i);
        l.add (std::make_unique<C> (prefix + "Source", prefix + " Source", juce::StringArray { "Off", "LFO 1", "Velocity", "Key Track", "Random Note", "Amp Env", "MSEG", "LFO 2", "LFO 3", "LFO 4" }, 0));
        l.add (std::make_unique<C> (prefix + "Dest", prefix + " Destination", actualModDestinations, 0));
        l.add (std::make_unique<P> (prefix + "Amount", prefix + " Amount", juce::NormalisableRange<float> (-1, 1), 0.0f));
    }
    for (int i = 1; i <= VoiceParameters::extraLayerCount; ++i)
    {
        const auto prefix = "layer" + juce::String (i);
        l.add (std::make_unique<C> (prefix + "Operation", prefix + " Combine", juce::StringArray { "Add", "Mix", "Subtract", "Multiply", "Divide" }, 0));
        l.add (std::make_unique<P> (prefix + "Amount", prefix + " Combine Amount", juce::NormalisableRange<float> (0, 1), 1.0f));
    }

    // Automation-safe append-only clock/resynthesis surface.
    l.add (std::make_unique<C> ("resynthInstances", "Resynthesis Instances", juce::StringArray { "1 / Single", "2 / Layered", "3 / Deep Layered" }, 1));
    l.add (std::make_unique<C> ("tempoSource", "Tempo Source", juce::StringArray { "Manual BPM", "DAW Tempo" }, 1));
    l.add (std::make_unique<P> ("manualBpm", "Manual BPM", juce::NormalisableRange<float> (40.0f, 300.0f, 0.1f), 120.0f));
    const auto divisions = TempoSync::divisionLabels();
    for (int i = 1; i <= 4; ++i)
    {
        const auto prefix = "lfo" + juce::String (i);
        l.add (std::make_unique<B> (prefix + "Sync", "LFO " + juce::String (i) + " Tempo Sync", false));
        l.add (std::make_unique<C> (prefix + "Division", "LFO " + juce::String (i) + " Division", divisions, 3));
    }
    l.add (std::make_unique<B> ("chorusSync", "Chorus Tempo Sync", false));
    l.add (std::make_unique<C> ("chorusDivision", "Chorus Division", divisions, 3));
    l.add (std::make_unique<B> ("delaySync", "Delay Tempo Sync", false));
    l.add (std::make_unique<C> ("delayDivision", "Delay Division", divisions, 3));
    l.add (std::make_unique<B> ("msegSync", "MSEG Tempo Sync", false));
    l.add (std::make_unique<C> ("msegDivision", "MSEG Division", divisions, 3));
    for (int i = 1; i <= FxModuleParameters::slotCount; ++i)
    {
        const auto prefix = "fxModule" + juce::String (i);
        l.add (std::make_unique<B> (prefix + "TempoSync", prefix + " Tempo Sync", false));
        l.add (std::make_unique<C> (prefix + "Division", prefix + " Division", divisions, 3));
    }

    // Append-only professional resynthesis / master section. Existing automation indices stay intact.
    l.add (std::make_unique<C> ("resynthStrategy", "Resynthesis Strategy",
        juce::StringArray { "Balanced Hybrid", "Reference Wavetable", "Spectral Subtractive",
                            "FM / Harmonic", "Layered Studio", "Texture / Chop", "FX / Guitar Chain" }, 0));
    l.add (std::make_unique<C> ("resynthComplexity", "Resynthesis Complexity",
        juce::StringArray { "Classic / legacy 1-3", "Studio / 4 instances", "Deep / 6 instances", "Maximum / 8 instances" }, 0));
    l.add (std::make_unique<P> ("masterOutputGain", "Master Output",
        juce::NormalisableRange<float> (-36.0f, 12.0f, 0.1f), 0.0f));

    // Append-only whole-synth bus parameters. Kept after all existing parameters so
    // established automation indices remain stable. Each slot can be a filter or FX.
    for (int i = 1; i <= FxModuleParameters::slotCount; ++i)
    {
        const auto prefix = "globalFxModule" + juce::String (i);
        l.add (std::make_unique<C> (prefix + "Type", prefix + " Type", moduleTypes, 0));
        l.add (std::make_unique<C> (prefix + "Stage", prefix + " Stage", juce::StringArray { "PRE", "POST" }, 0));
        l.add (std::make_unique<B> (prefix + "Bypass", prefix + " Bypass", false));
        l.add (std::make_unique<P> (prefix + "Amount", prefix + " Amount", juce::NormalisableRange<float> (0, 1), 0.5f));
        l.add (std::make_unique<P> (prefix + "Rate", prefix + " Rate", juce::NormalisableRange<float> (0, 1), 0.25f));
        l.add (std::make_unique<P> (prefix + "Feedback", prefix + " Feedback", juce::NormalisableRange<float> (0, 1), 0.25f));
        l.add (std::make_unique<P> (prefix + "Mix", prefix + " Mix", juce::NormalisableRange<float> (0, 1), 0.5f));
        l.add (std::make_unique<B> (prefix + "TempoSync", prefix + " Tempo Sync", false));
        l.add (std::make_unique<C> (prefix + "Division", prefix + " Division", divisions, 3));
    }
    return l;
}

void RetroMatchSynthAudioProcessor::applyPresetParameters (const VoiceParameters& parameters, const juce::String& name)
{
    selectEditingLayer (-1);
    melodyTransport.stop(); setReferenceAuditionMode (ReferenceAuditionMode::synthOnly);
    for (auto* parameter : getParameters())
        if (auto* identified = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
            if (identified->paramID != "oversamplingQuality" && identified->paramID != "masterOutputGain"
                && identified->paramID != "resynthStrategy" && identified->paramID != "resynthComplexity")
                parameter->setValueNotifyingHost (parameter->getDefaultValue());
    for (int i = 0; i < VoiceParameters::extraLayerCount; ++i) clearLayer (i);
    for (int i = 0; i < VoiceParameters::extraLayerCount; ++i)
        if (parameters.layers[(size_t) i])
        {
            MatchResult layer; layer.params = *parameters.layers[(size_t) i]; applyMatchResult (layer); captureLayer (i);
            const auto prefix = "layer" + juce::String (i + 1);
            auto set = [this, &prefix] (const char* suffix, float value) { auto* p = apvts.getParameter (prefix + suffix); p->setValueNotifyingHost (p->convertTo0to1 (value)); };
            set ("Gain", parameters.layerGain[(size_t) i]); set ("Pan", parameters.layerPan[(size_t) i]); set ("Tune", parameters.layerTune[(size_t) i]); set ("Operation", (float) parameters.layerOperation[(size_t) i]); set ("Amount", parameters.layerAmount[(size_t) i]);
        }
    MatchResult main; main.params = parameters; applyMatchResult (main);
    apvts.getParameter ("distortionMode")->setValueNotifyingHost (apvts.getParameter ("distortionMode")->convertTo0to1 ((float) parameters.distortionMode));
    apvts.getParameter ("distortionMix")->setValueNotifyingHost (parameters.distortionMix);
    apvts.state.setProperty ("patchName", name, nullptr);
    candidateBank = {}; clearCompareFineTuneState(); currentCandidateFeatures.reset();
}

void RetroMatchSynthAudioProcessor::loadFactoryPreset (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) factoryPresetCatalog.size())) return;
    applyPresetParameters (makeFactoryPreset (index), factoryPresetCatalog[(size_t) index].name);
}

void RetroMatchSynthAudioProcessor::randomizePreset()
{
    auto& random = juce::Random::getSystemRandom(); const auto seed = random.nextInt64();
    const int family = random.nextInt (10);
    const int variation = 5 + random.nextInt (5);
    const int presetIndex = 10 + family * 10 + variation;
    auto patch = SoundMatcher::makeVariation (makeFactoryPreset (presetIndex), seed, 0.06f + random.nextFloat() * 0.08f);
    patch.outputGainDb = juce::jlimit (-10.0f, -5.0f, patch.outputGainDb);
    patch.noiseMix = juce::jmin (patch.noiseMix, 0.15f);
    applyPresetParameters (patch, "Designed / " + juce::String (factoryPresetCatalog[(size_t) presetIndex].name) + " / " + juce::String::toHexString (seed).substring (0, 6));
}

bool RetroMatchSynthAudioProcessor::applyDirectedVariation (VariationDirection direction, float intensity, int64 seed)
{
    const auto source = readParams();
    if (! magicOriginSnapshot.isValid()) magicOriginSnapshot = snapshotCurrent();
    auto variant = SoundMatcher::makeDirectedVariation (source, direction, seed, intensity, matchSettings);
    if (currentFeatures)
        SoundMatcher::enforceReferenceLifecycle (*currentFeatures, variant);
    if (! MatchSafetyPolicy::compatible (variant)) return false;

    if (currentFeatures)
    {
        const auto measured = SoundMatcher::evaluateFit (*currentFeatures, variant, matchSettings);
        if (measured.technicalSafetyScore >= 0.0f && ! measured.technicallySafe)
            return false;
    }

    static constexpr const char* names[] {
        "Cinematic", "Atmospheric", "Organic", "Orchestral", "Staccato", "Percussive",
        "Techno", "Warm Analog", "Dark", "Bright", "Wide", "Intimate", "Rhythmic",
        "Fragile", "Aggressive", "Glitch"
    };
    const auto index = juce::jlimit (0, (int) std::size (names) - 1, (int) direction);
    applyPresetParameters (variant, "Magic / " + juce::String (names[index]));
    currentCandidateFeatures.reset();
    lastMatch = {};
    return true;
}

void RetroMatchSynthAudioProcessor::captureMagicOrigin()
{
    magicOriginSnapshot = snapshotCurrent();
}

bool RetroMatchSynthAudioProcessor::restoreMagicOrigin()
{
    if (! magicOriginSnapshot.isValid()) return false;
    applyEditingSnapshot (magicOriginSnapshot);
    lastMatch = {};
    currentCandidateFeatures.reset();
    return true;
}

bool RetroMatchSynthAudioProcessor::validateReleaseState (juce::String* reason) const
{
    const auto parameters = readParams ({}, true);
    if (! MatchSafetyPolicy::compatible (parameters))
    {
        if (reason != nullptr) *reason = "Patch parameters failed recursive finite/bounded safety validation";
        return false;
    }

    const auto graph = getPatchGraphDocument();
    const auto compiled = DspRouting::compile (graph);
    if (! compiled.validation.ok)
    {
        if (reason != nullptr) *reason = "Patch graph is not release-safe: " + compiled.validation.message;
        return false;
    }

    for (int i = 0; i < VoiceParameters::extraLayerCount; ++i)
    {
        if (const auto layer = savedLayers[(size_t) i].load(); layer != nullptr
            && ! MatchSafetyPolicy::compatible (*layer))
        {
            if (reason != nullptr) *reason = "Layer " + juce::String (i + 1) + " failed recursive safety validation";
            return false;
        }
    }
    return true;
}

bool RetroMatchSynthAudioProcessor::savePreset (const juce::File& file)
{
    juce::String safetyReason;
    if (! validateReleaseState (&safetyReason))
        return false;

    const auto main = editingMain.load();
    const auto storedReference = main ? main->referenceWavetable : referenceWavetable;
    const auto storedUser = main ? main->userWavetable : userWavetable;

    auto xml = canonicalState().createXml();
    if (! xml) return false;
    xml->setAttribute ("presetVersion", "1.4");
    if (storedReference && storedReference->valid) xml->setAttribute ("referenceWavetable", storedReference->toBase64());
    if (storedUser && storedUser->valid)
    {
        xml->setAttribute ("userWavetable", storedUser->toBase64());
        xml->setAttribute ("userWavetableName", main ? editingMainSnapshot["userTableName"].toString() : userWavetableName);
        xml->setAttribute ("userWavetableDescription", main ? editingMainSnapshot["userTableDescription"].toString() : userWavetableDescription);
    }
    xml->setAttribute ("product", "RetroMatchSynth");
    xml->setAttribute ("analysisStartSeconds", (double) analysisStartSeconds.load());
    xml->setAttribute ("analysisEndSeconds", (double) analysisEndSeconds.load());
    const auto mappings = getMidiMappings(); xml->setAttribute ("midiMapCount", (int) mappings.size());
    for (int i = 0; i < (int) mappings.size(); ++i) { xml->setAttribute ("midiMap" + juce::String (i) + "Id", mappings[(size_t) i].parameterId); xml->setAttribute ("midiMap" + juce::String (i) + "CC", mappings[(size_t) i].cc); }
    return xml->writeTo (file, {});
}

bool RetroMatchSynthAudioProcessor::loadPreset (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (! xml || ! xml->hasTagName (apvts.state.getType())) return false;
    const auto importedReference = xml->hasAttribute ("referenceWavetable")
        ? ReferenceWavetableData::fromBase64 (xml->getStringAttribute ("referenceWavetable")) : nullptr;
    const auto importedUser = xml->hasAttribute ("userWavetable")
        ? ReferenceWavetableData::fromBase64 (xml->getStringAttribute ("userWavetable")) : nullptr;
    if (! MatchSafetyPolicy::compatibleTable (importedReference)
        || ! MatchSafetyPolicy::compatibleTable (importedUser))
        return false;
    melodyTransport.stop();
    const float preservedMaster = apvts.getRawParameterValue ("masterOutputGain")->load();
    const float preservedStrategy = apvts.getRawParameterValue ("resynthStrategy")->load();
    const float preservedComplexity = apvts.getRawParameterValue ("resynthComplexity")->load();
    apvts.replaceState (stateWithPost10Defaults (*xml));
    auto restoreGlobal = [this] (const char* id, float value)
    {
        if (auto* parameter = apvts.getParameter (id))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    };
    restoreGlobal ("masterOutputGain", preservedMaster);
    restoreGlobal ("resynthStrategy", preservedStrategy);
    restoreGlobal ("resynthComplexity", preservedComplexity);
    restoreLayers();
    rebuildRoutingPlanFromState();
    analysisStartSeconds.store ((float) xml->getDoubleAttribute ("analysisStartSeconds", 0.0));
    analysisEndSeconds.store ((float) xml->getDoubleAttribute ("analysisEndSeconds", -1.0));
    { const juce::ScopedLock lock (midiMappingLock); midiMappings.clear(); for (int i = 0; i < xml->getIntAttribute ("midiMapCount", 0); ++i) midiMappings.push_back ({ xml->getStringAttribute ("midiMap" + juce::String (i) + "Id"), xml->getIntAttribute ("midiMap" + juce::String (i) + "CC", 0) }); }

    referenceWavetable = importedReference;
    userWavetable = importedUser;
    userWavetableName = userWavetable ? xml->getStringAttribute ("userWavetableName", "Embedded wavetable") : juce::String {};
    userWavetableDescription = userWavetable ? xml->getStringAttribute ("userWavetableDescription", "Embedded 5 x 2048 table") : juce::String {};
    return true;
}

bool RetroMatchSynthAudioProcessor::exportPreviewWav (const juce::File& file, float seconds) const
{
    auto params = readParams();
    const double sr = getSampleRate() > 1000.0 ? getSampleRate() : 44100.0;
    const float f0 = currentFeatures && currentFeatures->fundamentalHz > 20.0f ? currentFeatures->fundamentalHz : 261.6256f;
    auto audio = OfflineRenderer::renderPatch (params, sr, juce::jlimit (0.25f, 12.0f, seconds), f0, 256);
    audio.applyGain (juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("masterOutputGain")->load()));
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    if (! stream) return false;

    juce::WavAudioFormat format;
    const auto options = juce::AudioFormatWriter::Options {}
                             .withSampleRate (sr)
                             .withNumChannels (audio.getNumChannels())
                             .withBitsPerSample (24);
    auto writer = format.createWriterFor (stream, options);
    if (! writer) return false;
    return writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
}

void RetroMatchSynthAudioProcessor::getStateInformation (juce::MemoryBlock& d)
{
    const auto main = editingMain.load();
    const auto storedReference = main ? main->referenceWavetable : referenceWavetable;
    const auto storedUser = main ? main->userWavetable : userWavetable;

    if (auto xml = canonicalState().createXml())
    {
        xml->setAttribute ("lightPalette", lightPalette.load());
        if (storedReference && storedReference->valid)
            xml->setAttribute ("referenceWavetable", storedReference->toBase64());
        if (storedUser && storedUser->valid)
        {
            xml->setAttribute ("userWavetable", storedUser->toBase64());
            xml->setAttribute ("userWavetableName", main ? editingMainSnapshot["userTableName"].toString() : userWavetableName);
            xml->setAttribute ("userWavetableDescription", main ? editingMainSnapshot["userTableDescription"].toString() : userWavetableDescription);
        }
        xml->setAttribute ("referenceAuditionMode", referenceAuditionMode.load());
        xml->setAttribute ("referenceAuditionLevel", (double) referenceAuditionLevel.load());
        xml->setAttribute ("analysisStartSeconds", (double) analysisStartSeconds.load());
        xml->setAttribute ("analysisEndSeconds", (double) analysisEndSeconds.load());
        const auto mappings = getMidiMappings(); xml->setAttribute ("midiMapCount", (int) mappings.size());
        for (int i = 0; i < (int) mappings.size(); ++i) { xml->setAttribute ("midiMap" + juce::String (i) + "Id", mappings[(size_t) i].parameterId); xml->setAttribute ("midiMap" + juce::String (i) + "CC", mappings[(size_t) i].cc); }
        copyXmlToBinary (*xml, d);
    }
}

void RetroMatchSynthAudioProcessor::setStateInformation (const void* d, int n)
{
    if (auto xml = getXmlFromBinary (d, n))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            const auto importedReference = xml->hasAttribute ("referenceWavetable")
                ? ReferenceWavetableData::fromBase64 (xml->getStringAttribute ("referenceWavetable")) : nullptr;
            const auto importedUser = xml->hasAttribute ("userWavetable")
                ? ReferenceWavetableData::fromBase64 (xml->getStringAttribute ("userWavetable")) : nullptr;
            if (! MatchSafetyPolicy::compatibleTable (importedReference)
                || ! MatchSafetyPolicy::compatibleTable (importedUser))
                return;
            melodyTransport.stop();
            lightPalette.store (juce::jlimit (0, 3, xml->getIntAttribute ("lightPalette", 0)));
            apvts.replaceState (stateWithPost10Defaults (*xml));
            restoreLayers();
            rebuildRoutingPlanFromState();
            analysisStartSeconds.store ((float) xml->getDoubleAttribute ("analysisStartSeconds", 0.0));
            analysisEndSeconds.store ((float) xml->getDoubleAttribute ("analysisEndSeconds", -1.0));
            { const juce::ScopedLock lock (midiMappingLock); midiMappings.clear(); for (int i = 0; i < xml->getIntAttribute ("midiMapCount", 0); ++i) midiMappings.push_back ({ xml->getStringAttribute ("midiMap" + juce::String (i) + "Id"), xml->getIntAttribute ("midiMap" + juce::String (i) + "CC", 0) }); }
            referenceWavetable = importedReference;
            userWavetable = importedUser;
            userWavetableName = userWavetable ? xml->getStringAttribute ("userWavetableName", "Embedded wavetable") : juce::String {};
            userWavetableDescription = userWavetable ? xml->getStringAttribute ("userWavetableDescription", "Embedded 5 x 2048 table") : juce::String {};
            referenceAuditionMode.store (juce::jlimit (0, 2, xml->getIntAttribute ("referenceAuditionMode", 0)));
            referenceAuditionLevel.store (juce::jlimit (0.0f, 1.0f, (float) xml->getDoubleAttribute ("referenceAuditionLevel", 0.70)));
        }
    }
}

std::array<MatchResult, 3> RetroMatchSynthAudioProcessor::buildCandidateBank()
{
    if (! currentFeatures) return {};
    clearCompareFineTuneState();
    const auto authored = getMainVoiceParameters();
    auto base = lastMatch.confidence > 0.0f ? lastMatch.params : authored;
    base.referenceWavetable = referenceWavetable;
    base.userWavetable = userWavetable;
    base.userWavetableMix = authored.userWavetableMix;
    base.layers.fill (nullptr); base.mainLayerGain = 1.0f;
    base.distortionMode = matchSettings.lockEffects ? authored.distortionMode : 0;
    base.distortionMix = matchSettings.lockEffects ? authored.distortionMix : 1.0f;
    std::array<VoiceParameters, 3> seeds { base, base, base };
    seeds[1].fmMix = juce::jmax (0.18f, base.fmMix); seeds[1].fmAlgorithm = (base.fmAlgorithm + 2) % 6; seeds[1].referenceWavetableMix *= 0.45f;
    seeds[2].supersawMix = juce::jmax (0.16f, base.supersawMix); seeds[2].wavetableMix = juce::jmax (0.20f, base.wavetableMix); seeds[2].referenceWavetableMix *= 0.70f;
    auto settings = matchSettings; settings.iterations = juce::jmax (36, settings.iterations / 2); settings.topologyTrials = juce::jmax (8, settings.topologyTrials / 2);
    for (int i = 0; i < 3; ++i) candidateBank[(size_t) i] = SoundMatcher::refineFit (*currentFeatures, seeds[(size_t) i], settings);
    selectedCandidate = 0; applyGeneratedRack (candidateBank[0], 0);
    return candidateBank;
}

std::array<MatchResult, 3> RetroMatchSynthAudioProcessor::buildGoldCandidateBank (SoundMatcher::ProgressCallback progress,
                                                                                 SoundMatcher::CancelCallback cancel)
{
    std::array<MatchResult, 3> result {};
    if (! currentFeatures) return result;
    clearCompareFineTuneState();

    const auto reference = *currentFeatures;
    const auto advice = ResynthesisAdvisor::advise (reference);
    const auto authored = getMainVoiceParameters();

    std::vector<int> methods;
    methods.reserve (7);
    methods.push_back (juce::jlimit (0, 6, advice.method));
    for (int method = 0; method <= 6; ++method)
        if (std::find (methods.begin(), methods.end(), method) == methods.end()) methods.push_back (method);

    std::vector<MatchResult> coarse;
    coarse.reserve (methods.size());
    for (size_t index = 0; index < methods.size(); ++index)
    {
        if (cancel && cancel()) return {};
        const int method = methods[index];
        auto seed = SoundMatcher::initialFit (reference).params;

        std::shared_ptr<const ReferenceWavetableData> table = referenceWavetable;
        if (method == 5 && loadedReferenceFile.existsAsFile())
            if (auto chopped = ReferenceWavetableExtractor::chop (loadedReferenceFile, analysisStartSeconds.load(), analysisEndSeconds.load()))
                table = std::move (chopped);

        seed.referenceWavetable = table;
        seed.referenceWavetableMix = table ? referenceTableWeight (reference, method) : 0.0f;
        seed.userWavetable = userWavetable;
        seed.userWavetableMix = authored.userWavetableMix;
        seed.distortionMode = matchSettings.lockEffects ? authored.distortionMode : 0;
        seed.distortionMix = matchSettings.lockEffects ? authored.distortionMix : 1.0f;
        seed.layers.fill (nullptr);
        seed.mainLayerGain = 1.0f;

        auto settings = matchSettings;
        settings.algorithm = method;
        settings.iterations = juce::jlimit (18, 42, juce::jmax (18, matchSettings.iterations / 4));
        settings.topologyTrials = juce::jlimit (6, 12, juce::jmax (6, matchSettings.topologyTrials / 2));
        settings.populationSize = juce::jlimit (4, 6, matchSettings.populationSize);
        auto candidate = SoundMatcher::refineFit (reference, seed, settings, {}, cancel);
        candidate.algorithm = method;
        candidate.complexity = advice.complexity;
        candidate.explanation = "GOLD coarse method sweep / " + ResynthesisAdvice { method, advice.complexity, 0.0f, {}, {} }.methodName()
                              + ". " + candidate.explanation;
        coarse.push_back (std::move (candidate));
        if (progress) progress (0.25f * (float) (index + 1) / (float) methods.size());
    }

    std::sort (coarse.begin(), coarse.end(), [] (const MatchResult& a, const MatchResult& b)
    {
        return a.similarity.total > b.similarity.total;
    });
    if (coarse.size() > 3) coarse.resize (3);

    std::vector<MatchResult> finals;
    finals.reserve (coarse.size());
    for (size_t index = 0; index < coarse.size(); ++index)
    {
        if (cancel && cancel()) return {};
        const int method = coarse[index].algorithm;
        auto deepSettings = matchSettings;
        deepSettings.algorithm = method;
        deepSettings.iterations = juce::jmax (84, matchSettings.iterations);
        deepSettings.topologyTrials = juce::jmax (16, matchSettings.topologyTrials);
        deepSettings.populationSize = juce::jmax (6, matchSettings.populationSize);
        const float base = 0.25f + (float) index * (0.50f / 3.0f);
        const float span = 0.50f / 3.0f;
        auto deep = SoundMatcher::refineFit (reference, coarse[index].params, deepSettings,
            [progress, base, span] (float p) { if (progress) progress (base + span * p); }, cancel);
        deep.algorithm = method;

        MatchResult bestFull;
        bestFull.similarity.total = -1.0f;
        for (int complexity = 0; complexity < 4; ++complexity)
        {
            if (cancel && cancel()) return {};
            auto rack = makeEmbeddedResynthRack (deep, reference, complexity, method, deep.params.referenceWavetable);
            auto scoreSettings = matchSettings;
            scoreSettings.algorithm = method;
            auto full = SoundMatcher::evaluateFit (reference, rack, scoreSettings);
            full.algorithm = method;
            full.complexity = complexity;
            full.fullRackScore = true;
            full.evaluatedCandidates += deep.evaluatedCandidates;
            full.explanation = "GOLD full-rack verification: the completed "
                             + juce::String (complexity == 0 ? 3 : (complexity == 1 ? 4 : complexity == 2 ? 6 : 8))
                             + "-instance instrument was rendered and scored after layering. " + deep.explanation;
            if (full.similarity.total > bestFull.similarity.total) bestFull = std::move (full);

            const float rackProgress = ((float) index * 4.0f + (float) complexity + 1.0f) / 12.0f;
            if (progress) progress (0.75f + rackProgress * 0.12f);
        }
        // Stage 3: evolve the complete chosen rack, not only the main voice.
        // This is deliberately bounded: Gold is exhaustive, but still needs a predictable ceiling.
        auto rackSettings = matchSettings; rackSettings.algorithm = method;
        const int rackIterations = 28;
        const float rackBase = 0.87f + (float) index * (0.13f / 3.0f);
        const float rackSpan = 0.13f / 3.0f;
        bestFull = evolveGoldRack (reference, std::move (bestFull), rackSettings, rackIterations,
                                   (int64) 0x474f4c445241434b + (int64) method * 4099,
                                   [progress, rackBase, rackSpan] (float p)
                                   { if (progress) progress (rackBase + rackSpan * p); }, cancel);
        finals.push_back (std::move (bestFull));
    }

    std::sort (finals.begin(), finals.end(), [] (const MatchResult& a, const MatchResult& b)
    {
        return a.similarity.total > b.similarity.total;
    });
    for (size_t i = 0; i < result.size() && i < finals.size(); ++i) result[i] = std::move (finals[i]);
    if (progress) progress (1.0f);
    return result;
}

void RetroMatchSynthAudioProcessor::clearCompareFineTuneState() noexcept
{
    compareFineTuneValuesByCandidate = {};
    compareFineTuneMeasuredValuesByCandidate = {};
    compareFineTuneMeasuredByCandidate = {};
    compareFineTuneAppliedByCandidate.fill (false);
    compareFineTunePending = false;
}

bool RetroMatchSynthAudioProcessor::selectCandidate (int index)
{
    if (! juce::isPositiveAndBelow (index, 3) || candidateBank[(size_t) index].confidence <= 0.0f) return false;
    selectedCandidate = index;
    const auto stateIndex = (size_t) index;
    const auto& selected = candidateBank[stateIndex];
    const auto values = compareFineTuneValuesByCandidate[stateIndex];
    const bool measuredCurrent = compareFineTuneMeasuredByCandidate[stateIndex].has_value()
                              && values.nearlyEquals (compareFineTuneMeasuredValuesByCandidate[stateIndex]);
    compareFineTunePending = ! values.isNeutral() && ! measuredCurrent;

    auto setChoice = [this] (const char* id, int value)
    {
        if (auto* parameter = apvts.getParameter (id))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) value));
    };
    if (selected.algorithm >= 0) setChoice ("resynthStrategy", juce::jlimit (0, 6, selected.algorithm));
    if (selected.complexity >= 0) setChoice ("resynthComplexity", juce::jlimit (0, 3, selected.complexity));

    if (values.isNeutral())
        applyGeneratedRack (selected, index);
    else
    {
        auto adjusted = selected;
        adjusted.params = CompareFineTune::apply (selected.params, values);
        applyGeneratedRack (adjusted, index);
    }

    const auto& visual = measuredCurrent ? *compareFineTuneMeasuredByCandidate[stateIndex] : selected;
    lastMatch = visual;
    currentCandidateFeatures = visual.candidateFeatures.duration > 0.0f
                             ? std::optional<SoundFeatures> (visual.candidateFeatures) : std::nullopt;
    return true;
}

bool RetroMatchSynthAudioProcessor::previewCompareFineTune (CompareFineTune::Values values)
{
    if (! juce::isPositiveAndBelow (selectedCandidate, 3)) return false;
    const auto index = (size_t) selectedCandidate;
    const auto baseline = candidateBank[index];
    if (baseline.confidence <= 0.0f) return false;

    values.clamp();
    compareFineTuneValuesByCandidate[index] = values;
    compareFineTuneAppliedByCandidate[index] = false;
    const bool measuredCurrent = compareFineTuneMeasuredByCandidate[index].has_value()
                              && values.nearlyEquals (compareFineTuneMeasuredValuesByCandidate[index]);
    compareFineTunePending = ! values.isNeutral() && ! measuredCurrent;

    auto preview = baseline;
    preview.params = CompareFineTune::apply (baseline.params, values);
    // Preview changes the live engine immediately, but remains transient compare state.
    // Closing Compare without KEEP restores the measured candidate baseline.
    applyGeneratedRack (preview, selectedCandidate);

    const auto& visual = measuredCurrent ? *compareFineTuneMeasuredByCandidate[index] : baseline;
    lastMatch = visual;
    currentCandidateFeatures = visual.candidateFeatures.duration > 0.0f
                             ? std::optional<SoundFeatures> (visual.candidateFeatures) : std::nullopt;
    return true;
}

void RetroMatchSynthAudioProcessor::resetCompareFineTune()
{
    if (! juce::isPositiveAndBelow (selectedCandidate, 3)) return;
    const auto index = (size_t) selectedCandidate;
    compareFineTuneValuesByCandidate[index] = {};
    compareFineTuneMeasuredValuesByCandidate[index] = {};
    compareFineTuneMeasuredByCandidate[index].reset();
    compareFineTuneAppliedByCandidate[index] = false;
    compareFineTunePending = false;
    if (candidateBank[index].confidence > 0.0f)
    {
        applyGeneratedRack (candidateBank[index], selectedCandidate);
        lastMatch = candidateBank[index];
        currentCandidateFeatures = candidateBank[index].candidateFeatures.duration > 0.0f
                                 ? std::optional<SoundFeatures> (candidateBank[index].candidateFeatures) : std::nullopt;
    }
}

bool RetroMatchSynthAudioProcessor::showCompareFineTuneBaseline (bool baselineView)
{
    if (! juce::isPositiveAndBelow (selectedCandidate, 3)) return false;
    const auto index = (size_t) selectedCandidate;
    const auto measuredBaseline = candidateBank[index];
    if (measuredBaseline.confidence <= 0.0f) return false;

    if (baselineView)
    {
        applyGeneratedRack (measuredBaseline, selectedCandidate);
        lastMatch = measuredBaseline;
        currentCandidateFeatures = measuredBaseline.candidateFeatures.duration > 0.0f
                                 ? std::optional<SoundFeatures> (measuredBaseline.candidateFeatures) : std::nullopt;
        return true;
    }

    const auto values = compareFineTuneValuesByCandidate[index];
    auto adjusted = measuredBaseline;
    adjusted.params = CompareFineTune::apply (measuredBaseline.params, values);
    applyGeneratedRack (adjusted, selectedCandidate);

    const bool measuredCurrent = compareFineTuneMeasuredByCandidate[index].has_value()
                              && values.nearlyEquals (compareFineTuneMeasuredValuesByCandidate[index]);
    const auto& visual = measuredCurrent ? *compareFineTuneMeasuredByCandidate[index] : measuredBaseline;
    lastMatch = visual;
    currentCandidateFeatures = visual.candidateFeatures.duration > 0.0f
                             ? std::optional<SoundFeatures> (visual.candidateFeatures) : std::nullopt;
    return true;
}

std::optional<RetroMatchSynthAudioProcessor::CompareFineTuneMeasureRequest>
RetroMatchSynthAudioProcessor::makeCompareFineTuneMeasureRequest() const
{
    if (! currentFeatures || ! juce::isPositiveAndBelow (selectedCandidate, 3)) return std::nullopt;
    const auto index = (size_t) selectedCandidate;
    const auto baseline = candidateBank[index];
    if (baseline.confidence <= 0.0f) return std::nullopt;

    CompareFineTuneMeasureRequest request;
    request.candidateIndex = selectedCandidate;
    request.values = compareFineTuneValuesByCandidate[index];
    request.reference = *currentFeatures;
    request.params = CompareFineTune::apply (baseline.params, request.values);
    request.settings = matchSettings;
    if (baseline.algorithm >= 0) request.settings.algorithm = juce::jlimit (0, 6, baseline.algorithm);
    request.baseline = baseline;
    return request;
}

bool RetroMatchSynthAudioProcessor::acceptCompareFineTuneMeasurement (int candidateIndex,
                                                                       CompareFineTune::Values values,
                                                                       MatchResult measured)
{
    if (! juce::isPositiveAndBelow (candidateIndex, 3)) return false;
    const auto index = (size_t) candidateIndex;
    if (candidateBank[index].confidence <= 0.0f
        || ! compareFineTuneValuesByCandidate[index].nearlyEquals (values))
        return false; // stale worker result: candidate/knobs moved while it rendered.

    const auto& baseline = candidateBank[index];
    measured.algorithm = baseline.algorithm;
    measured.complexity = baseline.complexity;
    measured.fullRackScore = baseline.fullRackScore;
    measured.explanation = "Compare fine-tune measured adjustment. " + measured.explanation;
    compareFineTuneMeasuredValuesByCandidate[index] = values;
    compareFineTuneMeasuredByCandidate[index] = measured;

    if (candidateIndex == selectedCandidate)
    {
        compareFineTunePending = false;
        applyGeneratedRack (measured, selectedCandidate);
        lastMatch = measured;
        currentCandidateFeatures = measured.candidateFeatures.duration > 0.0f
                                 ? std::optional<SoundFeatures> (measured.candidateFeatures) : std::nullopt;
    }
    return true;
}

bool RetroMatchSynthAudioProcessor::measureCompareFineTune()
{
    const auto request = makeCompareFineTuneMeasureRequest();
    if (! request) return false;
    if (request->values.isNeutral())
    {
        resetCompareFineTune();
        return true;
    }
    auto measured = SoundMatcher::evaluateFit (request->reference, request->params, request->settings);
    return acceptCompareFineTuneMeasurement (request->candidateIndex, request->values, std::move (measured));
}

bool RetroMatchSynthAudioProcessor::keepCompareFineTune()
{
    if (! juce::isPositiveAndBelow (selectedCandidate, 3)) return false;
    const auto index = (size_t) selectedCandidate;
    const auto baseline = candidateBank[index];
    if (baseline.confidence <= 0.0f) return false;

    const auto values = compareFineTuneValuesByCandidate[index];
    auto adjusted = baseline;
    adjusted.params = CompareFineTune::apply (baseline.params, values);
    applyGeneratedRack (adjusted, selectedCandidate);
    compareFineTuneAppliedByCandidate[index] = true;
    apvts.state.setProperty ("patchName", "Compare Adjusted " + juce::String ((char) ('A' + selectedCandidate)), nullptr);

    const bool measuredCurrent = compareFineTuneMeasuredByCandidate[index].has_value()
                              && values.nearlyEquals (compareFineTuneMeasuredValuesByCandidate[index]);
    const auto& visual = measuredCurrent ? *compareFineTuneMeasuredByCandidate[index] : baseline;
    lastMatch = visual;
    currentCandidateFeatures = visual.candidateFeatures.duration > 0.0f
                             ? std::optional<SoundFeatures> (visual.candidateFeatures) : std::nullopt;
    compareFineTunePending = ! values.isNeutral() && ! measuredCurrent;
    return true;
}

void RetroMatchSynthAudioProcessor::morphCandidates (int a, int b, float amount)
{
    if (! juce::isPositiveAndBelow (a, 3) || ! juce::isPositiveAndBelow (b, 3)) return;
    if (candidateBank[(size_t) a].confidence <= 0.0f || candidateBank[(size_t) b].confidence <= 0.0f) return;
    const auto& x = candidateBank[(size_t) a].params; const auto& y = candidateBank[(size_t) b].params;
    auto q = x; amount = juce::jlimit (0.0f, 1.0f, amount);
    auto mix = [amount] (float aa, float bb) { return juce::jmap (amount, aa, bb); };
    q.osc1Mix=mix(x.osc1Mix,y.osc1Mix); q.osc2Mix=mix(x.osc2Mix,y.osc2Mix); q.subMix=mix(x.subMix,y.subMix); q.noiseMix=mix(x.noiseMix,y.noiseMix);
    q.additiveMix=mix(x.additiveMix,y.additiveMix); q.wavetableMix=mix(x.wavetableMix,y.wavetableMix); q.referenceWavetableMix=mix(x.referenceWavetableMix,y.referenceWavetableMix);
    q.wavetablePosition=mix(x.wavetablePosition,y.wavetablePosition); q.supersawMix=mix(x.supersawMix,y.supersawMix); q.unisonDetune=mix(x.unisonDetune,y.unisonDetune); q.unisonSpread=mix(x.unisonSpread,y.unisonSpread);
    q.fmMix=mix(x.fmMix,y.fmMix); q.fmFeedback=mix(x.fmFeedback,y.fmFeedback); q.cutoff=mix(x.cutoff,y.cutoff); q.resonance=mix(x.resonance,y.resonance);
    q.attack=mix(x.attack,y.attack); q.decay=mix(x.decay,y.decay); q.sustain=mix(x.sustain,y.sustain); q.release=mix(x.release,y.release);
    q.drive=mix(x.drive,y.drive); q.chorusMix=mix(x.chorusMix,y.chorusMix); q.delayMix=mix(x.delayMix,y.delayMix); q.reverbMix=mix(x.reverbMix,y.reverbMix); q.stereoWidth=mix(x.stereoWidth,y.stereoWidth);
    q.osc1Wave = amount < 0.5f ? x.osc1Wave : y.osc1Wave; q.osc2Wave = amount < 0.5f ? x.osc2Wave : y.osc2Wave; q.fmAlgorithm = amount < 0.5f ? x.fmAlgorithm : y.fmAlgorithm; q.filterType = amount < 0.5f ? x.filterType : y.filterType;
    const auto authored = getMainVoiceParameters();
    q.referenceWavetable = referenceWavetable;
    q.userWavetable = userWavetable;
    q.userWavetableMix = authored.userWavetableMix;
    q.distortionMode = authored.distortionMode; q.distortionMix = authored.distortionMix;
    MatchResult r; r.params=q; if (currentFeatures) r=SoundMatcher::evaluateFit (*currentFeatures,q,matchSettings); applyMatchResult(r);
}

juce::AudioProcessorEditor* RetroMatchSynthAudioProcessor::createEditor()
{
    return new OversamplingQualityEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RetroMatchSynthAudioProcessor();
}
