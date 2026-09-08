from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


def write(path, text):
    (ROOT / path).write_text(text, encoding="utf-8")


def replace_once(path, old, new):
    text = read(path)
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected one marker, got {count}: {old[:120]!r}")
    write(path, text.replace(old, new, 1))


def regex_once(path, pattern, replacement):
    text = read(path)
    new, count = re.subn(pattern, replacement, text, count=1, flags=re.S)
    if count != 1:
        raise RuntimeError(f"{path}: regex marker not found: {pattern[:120]!r}")
    write(path, new)


# ---------------------------------------------------------------------------
# Offline whole-instrument bus: Gold must measure the same class of global
# filters/effects that the plug-in can apply after all instances are combined.
# The live processor still owns the APVTS global rack; this internal field is
# intentionally non-automatable and is used by OfflineRenderer / Gold candidates.
replace_once(
    "Source/Engine/SynthEngine.h",
    "    std::array<FxModuleParameters, FxModuleParameters::slotCount> fxModules {};\n\n    float drive = 0.0f;",
    "    std::array<FxModuleParameters, FxModuleParameters::slotCount> fxModules {};\n"
    "    // Whole-instrument rack used by offline/full-rack rendering. The live plug-in\n"
    "    // mirrors this into its append-only globalFxModule APVTS bus when a Gold result is selected.\n"
    "    std::array<FxModuleParameters, FxModuleParameters::slotCount> globalFxModules {};\n\n"
    "    float drive = 0.0f;"
)
replace_once(
    "Source/Engine/SynthEngine.h",
    "    ModuleRack moduleRack;\n    double sampleRate = 44100.0;",
    "    ModuleRack moduleRack;\n    ModuleRack wholeInstrumentRack;\n    double sampleRate = 44100.0;"
)
replace_once(
    "Source/Engine/SynthEngine.cpp",
    "    moduleRack.prepare (sr, samplesPerBlock, channels);\n    if (withLayers)",
    "    moduleRack.prepare (sr, samplesPerBlock, channels);\n"
    "    wholeInstrumentRack.prepare (sr, samplesPerBlock, channels);\n"
    "    if (withLayers)"
)
replace_once(
    "Source/Engine/SynthEngine.cpp",
    "    moduleRack.reset();\n    chorus.reset();",
    "    moduleRack.reset();\n    wholeInstrumentRack.reset();\n    chorus.reset();"
)
replace_once(
    "Source/Engine/SynthEngine.cpp",
    "        p.layers.fill (nullptr); p.mainLayerGain = 1.0f;\n        p.masterTuneCents += juce::jlimit (-24.0f, 24.0f, current.layerTune[i]) * 100.0f;",
    "        p.layers.fill (nullptr); p.mainLayerGain = 1.0f;\n"
    "        // A whole-instrument rack belongs to the parent only; never repeat it inside companions.\n"
    "        p.globalFxModules = {};\n"
    "        p.masterTuneCents += juce::jlimit (-24.0f, 24.0f, current.layerTune[i]) * 100.0f;"
)
replace_once(
    "Source/Engine/SynthEngine.cpp",
    "        }\n    }\n}\n",
    "        }\n    }\n\n"
    "    // Gold/offline whole-instrument chain: exactly once after the main voice and\n"
    "    // every companion have been combined. Live APVTS global FX are processed by\n"
    "    // PluginProcessor and therefore are not copied into this field by readParams().\n"
    "    wholeInstrumentRack.process (audio, current.globalFxModules, 0, current.tempoBpm);\n"
    "    wholeInstrumentRack.process (audio, current.globalFxModules, 1, current.tempoBpm);\n"
    "}\n",
)

# The replacement above targets the first render-ending block after its unique
# layer-combine tail. Assert the expected whole-rack call exists only once in cpp.
engine_cpp = read("Source/Engine/SynthEngine.cpp")
if engine_cpp.count("wholeInstrumentRack.process (audio, current.globalFxModules") != 2:
    raise RuntimeError("whole-instrument rack was not inserted exactly at the render tail")

# ---------------------------------------------------------------------------
# Gold rack seed + second-stage rack evolution.
# Companion voices must never carry the global bus themselves.
replace_once(
    "Source/PluginProcessor.cpp",
    "    p.layers.fill (nullptr); p.mainLayerGain = 1.0f;\n    p.referenceWavetable = table;",
    "    p.layers.fill (nullptr); p.mainLayerGain = 1.0f; p.globalFxModules = {};\n    p.referenceWavetable = table;"
)

helpers = r'''

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
            voice->outputGainDb = goldMutateLinear (voice->outputGainDb, -16.0f, 2.0f, amount * 0.45f, random);
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
        static const int sensibleTypes[] { 0, 1, 2, 3, 6, 8, 9, 13 };
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
        candidate.evaluatedCandidates += best.evaluatedCandidates + evaluated + 1;
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
'''
replace_once(
    "Source/PluginProcessor.cpp",
    "    return rack;\n}\n\nclass OversamplingQualityEditor",
    "    return rack;\n}" + helpers + "\nclass OversamplingQualityEditor"
)

# Seed the post-sum rack during full-rack creation.
replace_once(
    "Source/PluginProcessor.cpp",
    "    const int wantedLayers = juce::jmin (VoiceParameters::extraLayerCount, totalInstances - 1);",
    "    seedGoldWholeInstrumentRack (rack, mainResult.candidateFeatures.duration > 0.0f ? mainResult.candidateFeatures : SoundFeatures {}, strategy);\n\n"
    "    const int wantedLayers = juce::jmin (VoiceParameters::extraLayerCount, totalInstances - 1);"
)

# The helper above needs the actual reference, not candidate features. Change its
# signature and every call to accept the real SoundFeatures explicitly.
replace_once(
    "Source/PluginProcessor.cpp",
    "VoiceParameters makeEmbeddedResynthRack (const MatchResult& mainResult, int complexity, int strategy,\n                                         const std::shared_ptr<const ReferenceWavetableData>& table)",
    "VoiceParameters makeEmbeddedResynthRack (const MatchResult& mainResult, const SoundFeatures& reference,\n                                         int complexity, int strategy,\n                                         const std::shared_ptr<const ReferenceWavetableData>& table)"
)
replace_once(
    "Source/PluginProcessor.cpp",
    "    seedGoldWholeInstrumentRack (rack, mainResult.candidateFeatures.duration > 0.0f ? mainResult.candidateFeatures : SoundFeatures {}, strategy);",
    "    seedGoldWholeInstrumentRack (rack, reference, strategy);"
)
text = read("Source/PluginProcessor.cpp")
text = text.replace("makeEmbeddedResynthRack (deep, complexity, method, deep.params.referenceWavetable)",
                    "makeEmbeddedResynthRack (deep, reference, complexity, method, deep.params.referenceWavetable)")
write("Source/PluginProcessor.cpp", text)

# Rack-depth evaluation now reserves progress for a second-stage full-rack optimizer.
replace_once(
    "Source/PluginProcessor.cpp",
    "            const float rackProgress = ((float) index * 4.0f + (float) complexity + 1.0f) / 12.0f;\n            if (progress) progress (0.75f + rackProgress * 0.25f);",
    "            const float rackProgress = ((float) index * 4.0f + (float) complexity + 1.0f) / 12.0f;\n"
    "            if (progress) progress (0.75f + rackProgress * 0.12f);"
)
replace_once(
    "Source/PluginProcessor.cpp",
    "        finals.push_back (std::move (bestFull));",
    "        // Stage 3: evolve the complete chosen rack, not only the main voice.\n"
    "        // This is deliberately bounded: Gold is exhaustive, but still needs a predictable ceiling.\n"
    "        auto rackSettings = matchSettings; rackSettings.algorithm = method;\n"
    "        const int rackIterations = 28;\n"
    "        const float rackBase = 0.87f + (float) index * (0.13f / 3.0f);\n"
    "        const float rackSpan = 0.13f / 3.0f;\n"
    "        bestFull = evolveGoldRack (reference, std::move (bestFull), rackSettings, rackIterations,\n"
    "                                   (int64) 0x474f4c445241434b + (int64) method * 4099,\n"
    "                                   [progress, rackBase, rackSpan] (float p)\n"
    "                                   { if (progress) progress (rackBase + rackSpan * p); }, cancel);\n"
    "        finals.push_back (std::move (bestFull));"
)

# Applying an exact Gold candidate now mirrors its offline global bus into the live
# append-only APVTS global rack, so the user can see and edit what was measured.
needle = "    if (hasEmbeddedRack)\n    {\n        if (auto* mainGain = apvts.getParameter (\"mainLayerGain\"))"
replacement = "    if (hasEmbeddedRack)\n    {\n        if (mainResult.fullRackScore)\n        {\n            auto setGlobal = [this] (const juce::String& id, float value)\n            {\n                if (auto* parameter = apvts.getParameter (id))\n                    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));\n            };\n            for (int i = 0; i < FxModuleParameters::slotCount; ++i)\n            {\n                const auto prefix = \"globalFxModule\" + juce::String (i + 1);\n                const auto& module = mainResult.params.globalFxModules[(size_t) i];\n                setGlobal (prefix + \"Type\", (float) module.type);\n                setGlobal (prefix + \"Stage\", (float) module.stage);\n                setGlobal (prefix + \"Bypass\", module.bypass ? 1.0f : 0.0f);\n                setGlobal (prefix + \"Amount\", module.amount);\n                setGlobal (prefix + \"Rate\", module.rate);\n                setGlobal (prefix + \"Feedback\", module.feedback);\n                setGlobal (prefix + \"Mix\", module.mix);\n                setGlobal (prefix + \"TempoSync\", module.tempoSync ? 1.0f : 0.0f);\n                setGlobal (prefix + \"Division\", (float) module.tempoDivision);\n            }\n        }\n        if (auto* mainGain = apvts.getParameter (\"mainLayerGain\"))"
replace_once("Source/PluginProcessor.cpp", needle, replacement)

# ---------------------------------------------------------------------------
# More informative effect-chain excitation: retain the robust low/mid/high score,
# add an 8-band white-noise transfer fingerprint and an actual two-tone nonlinear probe.
replace_once(
    "Source/Matching/EffectChainProbe.h",
    "struct EffectProbeSignature\n{\n    float low = 0.0f, mid = 0.0f, high = 0.0f;\n    float tail = 0.0f, stereo = 0.0f, nonlinear = 0.0f, dynamics = 0.0f;\n};",
    "struct EffectProbeSignature\n{\n"
    "    static constexpr int detailedBandCount = 8;\n"
    "    float low = 0.0f, mid = 0.0f, high = 0.0f;\n"
    "    std::array<float, detailedBandCount> detailedBands {};\n"
    "    float tail = 0.0f, stereo = 0.0f, nonlinear = 0.0f, dynamics = 0.0f;\n"
    "};"
)
replace_once(
    "Source/Matching/EffectChainProbe.h",
    "        s.low /= bandSum; s.mid /= bandSum; s.high /= bandSum;\n\n        const float releaseContext",
    "        s.low /= bandSum; s.mid /= bandSum; s.high /= bandSum;\n\n"
    "        float detailSum = 0.0f;\n"
    "        for (int band = 0; band < EffectProbeSignature::detailedBandCount; ++band)\n"
    "        {\n"
    "            float energy = 0.0f;\n"
    "            const int first = band * SoundFeatures::spectralBandCount / EffectProbeSignature::detailedBandCount;\n"
    "            const int last = (band + 1) * SoundFeatures::spectralBandCount / EffectProbeSignature::detailedBandCount;\n"
    "            for (int i = first; i < last; ++i) energy += juce::jmax (0.0f, f.spectralBands[(size_t) i]);\n"
    "            s.detailedBands[(size_t) band] = energy; detailSum += energy;\n"
    "        }\n"
    "        detailSum = juce::jmax (1.0e-6f, detailSum);\n"
    "        for (auto& energy : s.detailedBands) energy /= detailSum;\n\n"
    "        const float releaseContext"
)

# Save deterministic excitation for transfer-ratio analysis.
replace_once(
    "Source/Matching/EffectChainProbe.h",
    "        juce::Random random ((int64) 0x524d465850524f42);\n        for (int i = 0; i < noiseSamples; ++i)\n        {\n            const float n = random.nextFloat() * 2.0f - 1.0f;\n            context.buffer.setSample (0, i, n);\n            context.buffer.setSample (1, i, n);\n        }",
    "        juce::Random random ((int64) 0x524d465850524f42);\n"
    "        std::array<float, noiseSamples> excitation {};\n"
    "        for (int i = 0; i < noiseSamples; ++i)\n"
    "        {\n"
    "            const float n = random.nextFloat() * 2.0f - 1.0f;\n"
    "            excitation[(size_t) i] = n;\n"
    "            context.buffer.setSample (0, i, n);\n"
    "            context.buffer.setSample (1, i, n);\n"
    "        }"
)

# Insert detailed transfer analysis after the coarse white-noise colour calculation.
replace_once(
    "Source/Matching/EffectChainProbe.h",
    "        s.high = (float) (highSq / bandTotal);\n        const float activeRms = std::sqrt ((float) (activeSq / noiseSamples));",
    "        s.high = (float) (highSq / bandTotal);\n\n"
    "        double detailedSum = 0.0;\n"
    "        for (int band = 0; band < EffectProbeSignature::detailedBandCount; ++band)\n"
    "        {\n"
    "            const double t = (double) band / (double) (EffectProbeSignature::detailedBandCount - 1);\n"
    "            const double frequency = 70.0 * std::pow (5200.0 / 70.0, t);\n"
    "            double inRe = 0.0, inIm = 0.0, outRe = 0.0, outIm = 0.0;\n"
    "            for (int i = 0; i < noiseSamples; ++i)\n"
    "            {\n"
    "                const double phase = juce::MathConstants<double>::twoPi * frequency * (double) i / sampleRate;\n"
    "                const double c = std::cos (phase), sn = std::sin (phase);\n"
    "                const double input = excitation[(size_t) i];\n"
    "                const double output = 0.5 * ((double) context.buffer.getSample (0, i) + (double) context.buffer.getSample (1, i));\n"
    "                inRe += input * c; inIm -= input * sn;\n"
    "                outRe += output * c; outIm -= output * sn;\n"
    "            }\n"
    "            const double inputMagnitude = std::sqrt (inRe * inRe + inIm * inIm);\n"
    "            const double outputMagnitude = std::sqrt (outRe * outRe + outIm * outIm);\n"
    "            const float transfer = (float) juce::jlimit (0.0, 8.0, outputMagnitude / juce::jmax (1.0e-9, inputMagnitude));\n"
    "            const float energy = transfer * transfer;\n"
    "            s.detailedBands[(size_t) band] = energy; detailedSum += energy;\n"
    "        }\n"
    "        detailedSum = juce::jmax (1.0e-9, detailedSum);\n"
    "        for (auto& energy : s.detailedBands) energy = (float) (energy / detailedSum);\n\n"
    "        const float activeRms = std::sqrt ((float) (activeSq / noiseSamples));"
)

# Add a two-tone nonlinear measurement after the impulse pass and before module metadata blending.
replace_once(
    "Source/Matching/EffectChainProbe.h",
    "        s.stereo = juce::jlimit (0.0f, 1.0f, sideRms / juce::jmax (1.0e-5f, midRms + sideRms));\n\n        for (const auto& module : p.fxModules)",
    "        s.stereo = juce::jlimit (0.0f, 1.0f, sideRms / juce::jmax (1.0e-5f, midRms + sideRms));\n\n"
    "        // Pass 3: two non-harmonically-related tones expose harmonics and\n"
    "        // intermodulation generated by saturation/clip/wavefold stages.\n"
    "        context.rack.reset(); context.buffer.clear();\n"
    "        constexpr double toneA = 233.0, toneB = 617.0;\n"
    "        constexpr int toneSamples = 4096;\n"
    "        for (int i = 0; i < toneSamples; ++i)\n"
    "        {\n"
    "            const float x = 0.19f * (float) std::sin (juce::MathConstants<double>::twoPi * toneA * i / sampleRate)\n"
    "                          + 0.19f * (float) std::sin (juce::MathConstants<double>::twoPi * toneB * i / sampleRate);\n"
    "            context.buffer.setSample (0, i, x); context.buffer.setSample (1, i, x);\n"
    "        }\n"
    "        processRack();\n"
    "        auto magnitudeAt = [&] (double frequency)\n"
    "        {\n"
    "            double re = 0.0, im = 0.0;\n"
    "            for (int i = 0; i < toneSamples; ++i)\n"
    "            {\n"
    "                const double x = 0.5 * ((double) context.buffer.getSample (0, i) + (double) context.buffer.getSample (1, i));\n"
    "                const double phase = juce::MathConstants<double>::twoPi * frequency * i / sampleRate;\n"
    "                re += x * std::cos (phase); im -= x * std::sin (phase);\n"
    "            }\n"
    "            return std::sqrt (re * re + im * im);\n"
    "        };\n"
    "        const double fundamentals = magnitudeAt (toneA) + magnitudeAt (toneB);\n"
    "        const double products = magnitudeAt (toneA * 2.0) + magnitudeAt (toneB * 2.0)\n"
    "                              + magnitudeAt (toneB - toneA) + magnitudeAt (toneA + toneB)\n"
    "                              + magnitudeAt (toneB + toneA * 2.0);\n"
    "        const float measuredNonlinear = juce::jlimit (0.0f, 1.0f, (float) (products / juce::jmax (1.0e-9, fundamentals) * 2.8));\n"
    "        s.nonlinear = juce::jmax (s.nonlinear, measuredNonlinear);\n\n"
    "        for (const auto& module : p.fxModules)"
)

# Compare both coarse and detailed transfer colour.
replace_once(
    "Source/Matching/EffectChainProbe.h",
    "        const float colour = similarity (target.low, candidate.low) * 0.30f\n                           + similarity (target.mid, candidate.mid) * 0.36f\n                           + similarity (target.high, candidate.high) * 0.34f;\n        return juce::jlimit (0.0f, 1.0f,\n                            colour * 0.36f",
    "        const float coarseColour = similarity (target.low, candidate.low) * 0.30f\n"
    "                                 + similarity (target.mid, candidate.mid) * 0.36f\n"
    "                                 + similarity (target.high, candidate.high) * 0.34f;\n"
    "        float detailedColour = 0.0f;\n"
    "        for (int i = 0; i < EffectProbeSignature::detailedBandCount; ++i)\n"
    "            detailedColour += similarity (target.detailedBands[(size_t) i], candidate.detailedBands[(size_t) i]);\n"
    "        detailedColour /= (float) EffectProbeSignature::detailedBandCount;\n"
    "        const float colour = coarseColour * 0.34f + detailedColour * 0.66f;\n"
    "        return juce::jlimit (0.0f, 1.0f,\n"
    "                            colour * 0.36f"
)

# ---------------------------------------------------------------------------
# Tests: global rack must process only after full rack, and the new probe must be finite.
smoke = read("Tests/SmokeTests.cpp")
marker = "    {\n        SoundFeatures guitar;"
insert = r'''    {
        VoiceParameters fullRack;
        fullRack.osc1Wave = 1; fullRack.osc2Mix = 0.0f; fullRack.release = 0.03f;
        const auto dry = OfflineRenderer::renderPatch (fullRack, 22050.0, 0.55f, 220.0f);
        fullRack.globalFxModules[0] = { 1, 0, false, 0.58f, 0.0f, 0.08f, 0.85f };
        const auto globallyFiltered = OfflineRenderer::renderPatch (fullRack, 22050.0, 0.55f, 220.0f);
        if (! finiteAudio (globallyFiltered) || maxDifference (dry, globallyFiltered) < 0.001f)
            return fail ("offline whole-instrument global filter did not process the completed rack");

        VoiceParameters probeDry;
        const auto drySignature = EffectChainProbe::probe (probeDry);
        probeDry.fxModules[0] = { 3, 0, false, 0.72f, 0.45f, 0.50f, 1.0f };
        const auto drivenSignature = EffectChainProbe::probe (probeDry);
        float detailedSum = 0.0f;
        for (const auto value : drivenSignature.detailedBands)
        {
            if (! std::isfinite (value)) return fail ("detailed FX probe contained non-finite values");
            detailedSum += value;
        }
        if (std::abs (detailedSum - 1.0f) > 0.02f) return fail ("detailed FX transfer fingerprint was not normalized");
        if (drivenSignature.nonlinear <= drySignature.nonlinear)
            return fail ("two-tone FX probe did not detect added saturation");
    }
'''
if marker not in smoke:
    raise RuntimeError("Smoke test insertion marker missing")
write("Tests/SmokeTests.cpp", smoke.replace(marker, insert + marker, 1))

# Static checks keep these architectural properties from silently regressing.
replace_once(
    "scripts/static-check.py",
    "    'gold full rack search': ['buildGoldCandidateBank', 'makeEmbeddedResynthRack', 'fullRackScore', 'GOLD / FULL RACK'],",
    "    'gold full rack search': ['buildGoldCandidateBank', 'makeEmbeddedResynthRack', 'fullRackScore', 'GOLD / FULL RACK'],\n"
    "    'gold rack evolution': ['evolveGoldRack', 'mutateGoldRack', 'globalFxModules', 'wholeInstrumentRack', 'seedGoldWholeInstrumentRack'],"
)
replace_once(
    "scripts/static-check.py",
    "    'gold full rack search': processor + processor_h + editor_all,",
    "    'gold full rack search': processor + processor_h + editor_all,\n"
    "    'gold rack evolution': processor + processor_h + engine_h + engine_cpp,"
)

plan = read("plan.md")
if "Rack-level evolution" not in plan:
    plan += """

### Rack-level evolution

- Gold does not stop after choosing a synthesis method and layer count.
- Evolve the completed rack with bounded full renders: main/layer gain, pan, tune, role timbre and MSEG movement.
- Include a whole-instrument post-sum filter/FX rack in the offline Gold model and mirror the winning rack into the live global FX controls.
- Use white-noise transfer analysis at higher spectral resolution plus a multi-tone nonlinear probe for guitar/pedal/amp-chain matching.
- Keep excitation probes diagnostic/secondary; the musical full-rack similarity remains the displayed truth.
"""
    write("plan.md", plan)

print("Gold rack-level evolution migration applied")
