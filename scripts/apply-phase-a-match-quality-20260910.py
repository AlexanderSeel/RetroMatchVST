from pathlib import Path


def replace_exact(path: str, old: str, new: str, count: int = 1) -> None:
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    actual = text.count(old)
    if actual != count:
        raise RuntimeError(f"{path}: expected {count} occurrence(s), found {actual}: {old[:100]!r}")
    p.write_text(text.replace(old, new), encoding="utf-8")


core = "Source/Matching/SoundMatcherCore.inc"
processor = "Source/PluginProcessor.cpp"
tests = "Tests/SmokeTests.cpp"

# A generic bright spectrum is not evidence of nonlinear processing. Keep the base
# deterministic match clean; explicit algorithms can add fold/drive when justified.
replace_exact(
    core,
    "    p.wavefold = juce::jlimit (0.0f, 0.32f, juce::jmax (0.0f, f.highEnergyRatio - 0.10f) * (1.0f - f.spectralFlatness) * 1.3f);",
    "    // Nonlinear colour must be earned by a synthesis profile, not inferred from brightness alone.\n"
    "    // A clean saw/string/pluck can have substantial high-frequency energy without any clipping/folding.\n"
    "    p.wavefold = 0.0f;",
)
replace_exact(
    core,
    "    p.drive = juce::jlimit (0.0f, 0.45f, (f.highEnergyRatio - 0.12f) * 0.8f);",
    "    // Start every unconstrained match with neutral drive. Algorithm-specific evidence may add it later.\n"
    "    p.drive = 0.0f;",
)

# Texture mode may use folding, but only when the analysed spectrum provides enough
# upper/complex energy. It must not have a non-zero floor for every texture candidate.
replace_exact(
    core,
    "            p.wavefold = juce::jmax (p.wavefold, juce::jlimit (0.03f, 0.32f, reference.highEnergyRatio * 0.55f));",
    "            const float foldEvidence = juce::jmax (0.0f, reference.highEnergyRatio - 0.14f)\n"
    "                                     * juce::jlimit (0.0f, 1.0f, 1.0f - reference.spectralFlatness) * 0.85f;\n"
    "            p.wavefold = juce::jmax (p.wavefold, juce::jlimit (0.0f, 0.28f, foldEvidence));",
)
replace_exact(
    core,
    "            p.modGraphSlots[0] = { (int) ModSource::mseg1, (int) ModDestination::wavefold,\n                                   juce::jlimit (0.10f, 0.42f, 0.12f + motion * 0.30f) };",
    "            if (p.wavefold > 0.02f)\n"
    "                p.modGraphSlots[0] = { (int) ModSource::mseg1, (int) ModDestination::wavefold,\n"
    "                                       juce::jlimit (0.05f, 0.24f, 0.06f + motion * 0.18f) };\n"
    "            else\n"
    "                p.modGraphSlots[0] = { (int) ModSource::mseg1, (int) ModDestination::cutoff,\n"
    "                                       juce::jlimit (0.04f, 0.18f, 0.04f + motion * 0.12f) };",
)

old_drive_seed = """            const int driveType = reference.inharmonicity > 0.24f && reference.highEnergyRatio > 0.10f ? 4 : 3;
            const float driveAmount = juce::jlimit (0.08f, 0.68f,
                                                    0.08f + reference.inharmonicity * 1.25f
                                                          + juce::jmax (0.0f, reference.highEnergyRatio - 0.08f) * 1.10f);

            prime (p.fxModules[0], 2, 0, amountForCutoff (hpHz), 0.0f, 0.06f, 1.0f);                    // HPF
            prime (p.fxModules[1], 13, 0, compression, 0.05f + reference.transientScore * 0.10f,
                   0.20f + reference.sustainLevel * 0.24f, 0.72f);                                      // compressor
            prime (p.fxModules[2], driveType, 0, driveAmount,
                   juce::jlimit (0.18f, 0.86f, 0.60f - reference.highEnergyRatio * 0.55f), 0.50f,
                   juce::jlimit (0.32f, 0.92f, 0.38f + driveAmount * 0.72f));                            // drive
            prime (p.fxModules[3], 1, 0, amountForCutoff (cabHz), 0.0f, 0.05f, 1.0f);                    // cab / speaker LPF
"""
new_drive_seed = """            // FX/Guitar does not automatically mean distortion. Require a combination of
            // upper-harmonic and complex-spectrum evidence before inserting a nonlinear stage.
            const float nonlinearEvidence = juce::jlimit (0.0f, 1.0f,
                juce::jmax (0.0f, reference.highEnergyRatio - 0.16f) * 1.55f
              + juce::jmax (0.0f, reference.inharmonicity - 0.20f) * 1.45f);
            const bool wantsDrive = nonlinearEvidence > 0.20f;
            const bool wantsHardClip = nonlinearEvidence > 0.72f
                                     && reference.highEnergyRatio > 0.20f
                                     && reference.inharmonicity > 0.30f;
            const int driveType = wantsHardClip ? 4 : 3;
            const float driveAmount = juce::jlimit (0.03f, 0.42f, 0.03f + nonlinearEvidence * 0.34f);

            prime (p.fxModules[0], 2, 0, amountForCutoff (hpHz), 0.0f, 0.06f, 1.0f);                    // HPF
            prime (p.fxModules[1], 13, 0, compression, 0.05f + reference.transientScore * 0.10f,
                   0.20f + reference.sustainLevel * 0.24f, 0.72f);                                      // compressor
            if (wantsDrive)
                prime (p.fxModules[2], driveType, 0, driveAmount,
                       juce::jlimit (0.18f, 0.78f, 0.56f - reference.highEnergyRatio * 0.42f), 0.50f,
                       juce::jlimit (0.08f, 0.48f, 0.08f + nonlinearEvidence * 0.36f));                   // evidenced drive
            else
                p.fxModules[2] = {};
            prime (p.fxModules[3], 1, 0, amountForCutoff (cabHz), 0.0f, 0.05f, 1.0f);                    // cab / speaker LPF
"""
replace_exact(core, old_drive_seed, new_drive_seed)

# The effect probe is diagnostic/tie-breaking evidence. It must not outweigh a cleaner
# musical render simply because an FX chain creates a similar transfer signature.
replace_exact(
    core,
    "        result.similarity.total = juce::jlimit (0.0f, 1.0f, result.similarity.total * 0.86f + result.effectProbeSimilarity * 0.14f);",
    "        result.similarity.total = juce::jlimit (0.0f, 1.0f, result.similarity.total * 0.95f + result.effectProbeSimilarity * 0.05f);",
)

# Generic mutation may refine nonlinear colour that was already evidenced/locked, but it
# must not invent drive/fold from a neutral seed merely because clipping happens to improve
# a coarse spectral score.
replace_exact(
    core,
    "    p.wavefold = mutateLinear (p.wavefold, 0.0f, 1.0f, amount * 0.8f, random);",
    "    p.wavefold = source.wavefold > 0.0001f\n"
    "               ? mutateLinear (p.wavefold, 0.0f, 0.65f, amount * 0.45f, random)\n"
    "               : 0.0f;",
)
replace_exact(
    core,
    "        auto& module = p.fxModules[(size_t) random.nextInt (FxModuleParameters::slotCount)];\n        module.type = random.nextInt ((int) fxModuleCatalog.size()); module.stage = random.nextInt (2); module.bypass = false;",
    "        auto& module = p.fxModules[(size_t) random.nextInt (FxModuleParameters::slotCount)];\n"
    "        static constexpr std::array<int, 10> safeExplorationTypes {{ 1, 2, 6, 7, 8, 9, 10, 11, 12, 13 }};\n"
    "        module.type = safeExplorationTypes[(size_t) random.nextInt ((int) safeExplorationTypes.size())];\n"
    "        module.stage = random.nextInt (2); module.bypass = false;",
)
replace_exact(
    core,
    "    p.drive = mutateLinear (p.drive, 0.0f, 1.0f, amount, random);",
    "    p.drive = source.drive > 0.0001f\n"
    "            ? mutateLinear (p.drive, 0.0f, 0.55f, amount * 0.45f, random)\n"
    "            : 0.0f;",
)

# A generated match must apply the exact distortion state that was evaluated, not retain
# a hard-clip/fold mode left behind in the editor from another patch.
replace_exact(
    processor,
    "    set (\"drive\", q.drive); set (\"chorusMix\", q.chorusMix); set (\"chorusRate\", q.chorusRate); set (\"chorusDepth\", q.chorusDepth);",
    "    set (\"drive\", q.drive); set (\"distortionMode\", (float) q.distortionMode); set (\"distortionMix\", q.distortionMix);\n"
    "    set (\"chorusMix\", q.chorusMix); set (\"chorusRate\", q.chorusRate); set (\"chorusDepth\", q.chorusDepth);",
)

# Matching only inherits authored distortion character when the user explicitly locks FX.
replace_exact(
    processor,
    "    seed.params.distortionMode = authored.distortionMode; seed.params.distortionMix = authored.distortionMix;",
    "    seed.params.distortionMode = matchSettings.lockEffects ? authored.distortionMode : 0;\n"
    "    seed.params.distortionMix = matchSettings.lockEffects ? authored.distortionMix : 1.0f;",
)
replace_exact(
    processor,
    "    seed.distortionMode = authored.distortionMode; seed.distortionMix = authored.distortionMix;",
    "    seed.distortionMode = matchSettings.lockEffects ? authored.distortionMode : 0;\n"
    "    seed.distortionMix = matchSettings.lockEffects ? authored.distortionMix : 1.0f;",
    count=2,
)
replace_exact(
    processor,
    "    base.distortionMode = authored.distortionMode; base.distortionMix = authored.distortionMix;",
    "    base.distortionMode = matchSettings.lockEffects ? authored.distortionMode : 0;\n"
    "    base.distortionMix = matchSettings.lockEffects ? authored.distortionMix : 1.0f;",
)

# Generated GOLD companion voices remain below unity before the post-sum rack. This avoids
# one mutated layer hitting a nonlinear/global processor several dB hotter than intended.
replace_exact(
    processor,
    "            voice->outputGainDb = goldMutateLinear (voice->outputGainDb, -16.0f, 2.0f, amount * 0.45f, random);",
    "            voice->outputGainDb = goldMutateLinear (voice->outputGainDb, -16.0f, -2.0f, amount * 0.45f, random);",
)

# Always replace the generated global bus. Previously a non-GOLD result could leave the
# previous GOLD rack active, so the live sound no longer matched the newly measured result.
old_global = """    bool hasEmbeddedRack = false;
    for (const auto& layer : mainResult.params.layers) hasEmbeddedRack |= layer != nullptr;
    if (hasEmbeddedRack)
    {
        if (mainResult.fullRackScore)
        {
            auto setGlobal = [this] (const juce::String& id, float value)
            {
                if (auto* parameter = apvts.getParameter (id))
                    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
            };
            for (int i = 0; i < FxModuleParameters::slotCount; ++i)
            {
                const auto prefix = "globalFxModule" + juce::String (i + 1);
                const auto& module = mainResult.params.globalFxModules[(size_t) i];
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
        }
"""
new_global = """    bool hasEmbeddedRack = false;
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
"""
replace_exact(processor, old_global, new_global)

# Add regression fixtures proving clean analysis/mutation paths do not manufacture
# nonlinear colour. Insert immediately before the existing FX module processing tests.
needle = """    {
        VoiceParameters tone; tone.osc1Wave = 1; tone.osc2Mix = 0; tone.release = 0.02f;
"""
insert = """    {
        SoundFeatures clean;
        clean.duration = 1.0f; clean.fundamentalHz = 220.0f; clean.pitchConfidence = 0.96f;
        clean.harmonicity = 0.88f; clean.inharmonicity = 0.08f; clean.transientScore = 0.56f;
        clean.spectralFlatness = 0.07f; clean.spectralCentroidHz = 2600.0f; clean.spectralRolloffHz = 7600.0f;
        clean.lowEnergyRatio = 0.16f; clean.highEnergyRatio = 0.22f; clean.zeroCrossingRate = 0.08f;
        clean.attackSeconds = 0.008f; clean.decaySeconds = 0.31f; clean.sustainLevel = 0.54f; clean.releaseSeconds = 0.18f;
        clean.stereoWidth = 0.10f; clean.spectralMotion = 0.025f;
        const auto cleanSeed = SoundMatcher::initialFit (clean).params;
        if (cleanSeed.drive > 1.0e-6f || cleanSeed.wavefold > 1.0e-6f)
            return fail ("clean bright reference seed manufactured nonlinear drive/fold");

        MatchSettings cleanFxSettings;
        cleanFxSettings.algorithm = 6; cleanFxSettings.iterations = 0; cleanFxSettings.topologyTrials = 0;
        cleanFxSettings.populationSize = 2; cleanFxSettings.renderSampleRate = 12000.0; cleanFxSettings.maxRenderSeconds = 0.5f;
        const auto cleanFx = SoundMatcher::refineFit (clean, cleanSeed, cleanFxSettings);
        for (const auto& module : cleanFx.params.fxModules)
            if ((module.type == 3 || module.type == 4 || module.type == 5) && ! module.bypass && module.mix > 0.001f)
                return fail ("FX/Guitar profile forced nonlinear processing without sufficient evidence");

        for (int i = 0; i < 48; ++i)
        {
            const auto variation = SoundMatcher::makeVariation (VoiceParameters {}, i);
            if (variation.drive > 1.0e-6f || variation.wavefold > 1.0e-6f)
                return fail ("generic variation invented built-in nonlinear colour from a neutral patch");
            for (const auto& module : variation.fxModules)
                if (module.type == 3 || module.type == 4 || module.type == 5)
                    return fail ("generic topology mutation invented nonlinear FX from a neutral patch");
        }
    }
    {
        VoiceParameters tone; tone.osc1Wave = 1; tone.osc2Mix = 0; tone.release = 0.02f;
"""
replace_exact(tests, needle, insert)

print("Phase A match-quality patch applied")
