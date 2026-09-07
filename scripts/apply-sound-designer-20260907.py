from pathlib import Path

ROOT = Path('.')


def read(path):
    return (ROOT / path).read_text(encoding='utf-8')


def write(path, text):
    p = ROOT / path
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(text, encoding='utf-8')


def must_replace(path, old, new, count=1):
    text = read(path)
    found = text.count(old)
    if found < count:
        raise RuntimeError(f'{path}: expected at least {count} occurrence(s), found {found}: {old[:120]!r}')
    text = text.replace(old, new, count)
    write(path, text)


def replace_between(path, start_marker, end_marker, replacement):
    text = read(path)
    start = text.find(start_marker)
    if start < 0:
        raise RuntimeError(f'{path}: start marker not found: {start_marker}')
    end = text.find(end_marker, start)
    if end < 0:
        raise RuntimeError(f'{path}: end marker not found: {end_marker}')
    write(path, text[:start] + replacement.rstrip() + '\n\n' + text[end:])


# ---------------------------------------------------------------------------
# 1) Matching profiles: the selector now changes the search space, not only UI.
# ---------------------------------------------------------------------------
must_replace('Source/Matching/SoundMatcher.h',
'''    float maxRenderSeconds = 12.0f;\n\n    bool lockPitch = false;''',
'''    float maxRenderSeconds = 12.0f;\n    // 0 balanced hybrid, 1 reference wavetable, 2 subtractive/spectral,\n    // 3 FM/harmonic, 4 layered studio, 5 texture/chopped wavetable.\n    int algorithm = 0;\n\n    bool lockPitch = false;''')

matcher = read('Source/Matching/SoundMatcher.cpp')
profile_code = r'''
void applyAlgorithmProfile (VoiceParameters& p, int algorithm, const SoundFeatures& reference)
{
    algorithm = juce::jlimit (0, 5, algorithm);
    const bool hasReferenceTable = p.referenceWavetable && p.referenceWavetable->valid;
    const float motion = juce::jlimit (0.0f, 1.0f, reference.spectralMotion * 2.5f);

    switch (algorithm)
    {
        case 1: // Reference wavetable / closest cycle fingerprint.
            if (hasReferenceTable)
            {
                p.referenceWavetableMix = juce::jmax (p.referenceWavetableMix, 0.62f + 0.22f * reference.pitchConfidence);
                p.osc1Mix *= 0.42f; p.osc2Mix *= 0.35f; p.fmMix *= 0.55f;
                p.wavetableMix *= 0.35f;
                p.wavetablePosition = juce::jlimit (0.05f, 0.95f, 0.18f + reference.highEnergyRatio * 1.5f + motion * 0.25f);
                if (reference.spectralMotion > 0.06f)
                    p.modGraphSlots[0] = { (int) ModSource::lfo2, (int) ModDestination::wavetablePosition,
                                           juce::jlimit (0.08f, 0.42f, reference.spectralMotion * 1.8f) };
            }
            p.cutoff = juce::jmax (p.cutoff, juce::jmin (19000.0f, reference.spectralRolloffHz * 1.25f));
            break;

        case 2: // Classic hardware subtractive / spectral envelope match.
            p.referenceWavetableMix *= 0.12f; p.wavetableMix *= 0.20f; p.userWavetableMix *= 0.35f;
            p.fmMix *= 0.28f; p.fmAmount *= 0.35f; p.wavefold *= 0.35f;
            p.osc1Mix = juce::jmax (0.52f, p.osc1Mix); p.osc2Mix = juce::jmax (0.08f, p.osc2Mix * 0.8f);
            p.resonance = juce::jlimit (0.04f, 0.68f, p.resonance);
            p.cutoff = juce::jlimit (90.0f, 19000.0f, reference.spectralCentroidHz * 2.6f + 180.0f);
            break;

        case 3: // Six-operator harmonic reconstruction.
            p.referenceWavetableMix *= 0.22f; p.wavetableMix *= 0.22f; p.supersawMix *= 0.25f;
            p.osc1Mix *= 0.38f; p.osc2Mix *= 0.30f;
            p.fmMix = juce::jmax (p.fmMix, juce::jlimit (0.48f, 0.88f, 0.48f + reference.harmonicity * 0.32f));
            p.fmAmount = juce::jmax (p.fmAmount, juce::jlimit (0.05f, 0.30f, reference.highEnergyRatio * 0.75f));
            p.fmFeedback = juce::jmax (p.fmFeedback, juce::jlimit (0.0f, 0.30f, reference.inharmonicity * 0.55f));
            p.fmAlgorithm = reference.oddHarmonicRatio < 0.42f ? 2 : (reference.inharmonicity > 0.18f ? 4 : 1);
            p.fmOpRatio = {{ 1.0f, 2.0f, 3.0f, 4.0f, 1.5f, 6.0f }};
            break;

        case 4: // Studio layered hybrid: wide, animated, but still reference-led.
            if (hasReferenceTable) p.referenceWavetableMix = juce::jmax (p.referenceWavetableMix, 0.34f);
            p.supersawMix = juce::jmax (p.supersawMix, juce::jlimit (0.08f, 0.42f, reference.stereoWidth * 0.34f));
            p.unisonSpread = juce::jmax (p.unisonSpread, 0.55f);
            p.chorusMix = juce::jmax (p.chorusMix, juce::jlimit (0.04f, 0.24f, reference.stereoWidth * 0.22f));
            if (reference.spectralMotion > 0.05f)
                p.modGraphSlots[0] = { (int) ModSource::lfo2, (int) ModDestination::wavetablePosition,
                                       juce::jlimit (0.06f, 0.32f, reference.spectralMotion * 1.25f) };
            break;

        case 5: // Texture / chopped-table reconstruction.
            if (hasReferenceTable) p.referenceWavetableMix = juce::jmax (p.referenceWavetableMix, 0.58f);
            p.wavetableMix = juce::jmax (p.wavetableMix, 0.18f + motion * 0.28f);
            p.wavefold = juce::jmax (p.wavefold, juce::jlimit (0.03f, 0.32f, reference.highEnergyRatio * 0.55f));
            p.noiseMix = juce::jmax (p.noiseMix, juce::jlimit (0.0f, 0.18f, reference.spectralFlatness * 0.22f));
            p.modGraphSlots[0] = { (int) ModSource::lfo2, (int) ModDestination::wavetablePosition,
                                   juce::jlimit (0.12f, 0.52f, 0.16f + motion * 0.38f) };
            p.extraLfoRate[0] = juce::jlimit (0.05f, 1.8f, 0.10f + reference.spectralMotion * 2.4f);
            break;

        default: // Balanced hybrid: use a reference table whenever it is meaningful.
            if (hasReferenceTable)
            {
                const float tableWeight = juce::jlimit (0.16f, 0.58f,
                    0.14f + reference.pitchConfidence * 0.20f + reference.harmonicity * 0.14f + motion * 0.22f);
                p.referenceWavetableMix = juce::jmax (p.referenceWavetableMix, tableWeight);
            }
            break;
    }
}
'''
marker = '\n}\n\nMatchResult SoundMatcher::initialFit'
if marker not in matcher:
    raise RuntimeError('SoundMatcher namespace insertion marker missing')
matcher = matcher.replace(marker, '\n' + profile_code + '}\n\nMatchResult SoundMatcher::initialFit', 1)
matcher = matcher.replace('insertElite (evaluateFit (reference, seed, settings));',
'''auto profiledSeed = seed;\n    applyAlgorithmProfile (profiledSeed, settings.algorithm, reference);\n    insertElite (evaluateFit (reference, profiledSeed, settings));''', 1)
matcher = matcher.replace('auto candidate = seed;', 'auto candidate = profiledSeed;', 1)
matcher = matcher.replace('applyLocks (candidate, seed, settings);',
'''applyAlgorithmProfile (candidate, settings.algorithm, reference);\n        applyLocks (candidate, seed, settings);''')
write('Source/Matching/SoundMatcher.cpp', matcher)


# ---------------------------------------------------------------------------
# 2) Processor: global master trim, resynth strategy/complexity, richer layers.
# ---------------------------------------------------------------------------
must_replace('Source/PluginProcessor.cpp',
'''    if (id.startsWith ("layer") || id == "mainLayerGain" || id == "oversamplingQuality" || id == "resynthInstances")''',
'''    if (id.startsWith ("layer") || id == "mainLayerGain" || id == "oversamplingQuality" || id == "resynthInstances"\n        || id == "masterOutputGain" || id == "resynthStrategy" || id == "resynthComplexity")''')

processor = read('Source/PluginProcessor.cpp')
helper_code = r'''
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
    return juce::jlimit (0.0f, 0.92f, weight);
}

VoiceParameters makeResynthCompanion (const VoiceParameters& source, int role, int strategy,
                                      const std::shared_ptr<ReferenceWavetableData>& table)
{
    auto p = source;
    p.layers.fill (nullptr); p.mainLayerGain = 1.0f;
    p.referenceWavetable = table;
    p.outputGainDb = juce::jlimit (-12.0f, -4.0f, source.outputGainDb - 1.5f);
    p.delayMix *= 0.65f; p.reverbMix *= 0.75f;

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
        case 3: // Motion table.
            if (table) p.referenceWavetableMix = juce::jmax (0.52f, p.referenceWavetableMix);
            p.wavetableMix = juce::jmax (0.16f, p.wavetableMix);
            p.extraLfoRate[0] = 0.11f;
            p.modGraphSlots[0] = { (int) ModSource::lfo2, (int) ModDestination::wavetablePosition, 0.36f };
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
'''
marker = '\nclass OversamplingQualityEditor final'
if marker not in processor:
    raise RuntimeError('PluginProcessor helper insertion marker missing')
processor = processor.replace(marker, '\n' + helper_code + '\nclass OversamplingQualityEditor final', 1)
write('Source/PluginProcessor.cpp', processor)

must_replace('Source/PluginProcessor.cpp',
'''    setDefault ("resynthInstances", 1);\n    setDefault ("tempoSource", 1);''',
'''    setDefault ("resynthInstances", 1);\n    setDefault ("resynthStrategy", 0);\n    setDefault ("resynthComplexity", 0);\n    setDefault ("masterOutputGain", 0.0f);\n    setDefault ("tempoSource", 1);''')

must_replace('Source/PluginProcessor.cpp',
'''    else if (mode == ReferenceAuditionMode::synthOnly)\n    {\n        referenceLatencyDelay.reset();\n    }\n\n    if (b.getNumSamples() > 0 && b.getNumChannels() > 0)''',
'''    else if (mode == ReferenceAuditionMode::synthOnly)\n    {\n        referenceLatencyDelay.reset();\n    }\n\n    // Global hardware-style master trim. Unlike patch OUTPUT this survives preset changes\n    // and controls the complete instrument, including reference A/B audition.\n    const float masterDb = apvts.getRawParameterValue ("masterOutputGain")->load();\n    b.applyGain (juce::Decibels::decibelsToGain (masterDb));\n\n    if (b.getNumSamples() > 0 && b.getNumChannels() > 0)''')

must_replace('Source/PluginProcessor.cpp',
'''    for (int i = 1; i <= FxModuleParameters::slotCount; ++i)\n    {\n        const auto prefix = "fxModule" + juce::String (i);\n        l.add (std::make_unique<B> (prefix + "TempoSync", prefix + " Tempo Sync", false));\n        l.add (std::make_unique<C> (prefix + "Division", prefix + " Division", divisions, 3));\n    }\n    return l;''',
'''    for (int i = 1; i <= FxModuleParameters::slotCount; ++i)\n    {\n        const auto prefix = "fxModule" + juce::String (i);\n        l.add (std::make_unique<B> (prefix + "TempoSync", prefix + " Tempo Sync", false));\n        l.add (std::make_unique<C> (prefix + "Division", prefix + " Division", divisions, 3));\n    }\n\n    // Append-only professional resynthesis / master section. Existing automation indices stay intact.\n    l.add (std::make_unique<C> ("resynthStrategy", "Resynthesis Strategy",\n        juce::StringArray { "Balanced Hybrid", "Reference Wavetable", "Spectral Subtractive",\n                            "FM / Harmonic", "Layered Studio", "Texture / Chop" }, 0));\n    l.add (std::make_unique<C> ("resynthComplexity", "Resynthesis Complexity",\n        juce::StringArray { "Classic / legacy 1-3", "Studio / 4 instances", "Deep / 6 instances", "Maximum / 8 instances" }, 0));\n    l.add (std::make_unique<P> ("masterOutputGain", "Master Output",\n        juce::NormalisableRange<float> (-36.0f, 12.0f, 0.1f), 0.0f));\n    return l;''')

must_replace('Source/PluginProcessor.cpp',
'''            if (identified->paramID != "oversamplingQuality") parameter->setValueNotifyingHost (parameter->getDefaultValue());''',
'''            if (identified->paramID != "oversamplingQuality" && identified->paramID != "masterOutputGain"\n                && identified->paramID != "resynthStrategy" && identified->paramID != "resynthComplexity")\n                parameter->setValueNotifyingHost (parameter->getDefaultValue());''')

must_replace('Source/PluginProcessor.cpp',
'''    melodyTransport.stop();\n    apvts.replaceState (stateWithPost10Defaults (*xml));\n    restoreLayers();''',
'''    melodyTransport.stop();\n    const float preservedMaster = apvts.getRawParameterValue ("masterOutputGain")->load();\n    const float preservedStrategy = apvts.getRawParameterValue ("resynthStrategy")->load();\n    const float preservedComplexity = apvts.getRawParameterValue ("resynthComplexity")->load();\n    apvts.replaceState (stateWithPost10Defaults (*xml));\n    auto restoreGlobal = [this] (const char* id, float value)\n    {\n        if (auto* parameter = apvts.getParameter (id))\n            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));\n    };\n    restoreGlobal ("masterOutputGain", preservedMaster);\n    restoreGlobal ("resynthStrategy", preservedStrategy);\n    restoreGlobal ("resynthComplexity", preservedComplexity);\n    restoreLayers();''')

# Strategy-aware first fit / refine and much more frequent reference-table use.
must_replace('Source/PluginProcessor.cpp',
'''    seed.params.referenceWavetable = referenceWavetable;\n    seed.params.referenceWavetableMix = referenceWavetable && seed.params.osc1Wave != 0 ? 0.32f : 0.0f;\n    seed.params.userWavetable = userWavetable;''',
'''    const int strategy = juce::jlimit (0, 5, (int) apvts.getRawParameterValue ("resynthStrategy")->load());\n    auto strategyTable = referenceWavetable;\n    if (strategy == 5 && loadedReferenceFile.existsAsFile())\n        if (auto chopped = ReferenceWavetableExtractor::chop (loadedReferenceFile, analysisStartSeconds.load(), analysisEndSeconds.load()))\n            strategyTable = std::move (chopped);\n    seed.params.referenceWavetable = strategyTable;\n    seed.params.referenceWavetableMix = strategyTable ? referenceTableWeight (*currentFeatures, strategy) : 0.0f;\n    seed.params.userWavetable = userWavetable;''', 1)

must_replace('Source/PluginProcessor.cpp',
'''    auto evaluated = SoundMatcher::evaluateFit (*currentFeatures, seed.params);''',
'''    auto fitSettings = matchSettings; fitSettings.algorithm = strategy;\n    auto evaluated = SoundMatcher::evaluateFit (*currentFeatures, seed.params, fitSettings);''', 1)

must_replace('Source/PluginProcessor.cpp',
'''    const auto settings = matchSettings;\n    const auto reference = *currentFeatures;''',
'''    auto settings = matchSettings;\n    const int strategy = juce::jlimit (0, 5, (int) apvts.getRawParameterValue ("resynthStrategy")->load());\n    settings.algorithm = strategy;\n    const auto reference = *currentFeatures;''', 1)

must_replace('Source/PluginProcessor.cpp',
'''    seed.referenceWavetable = referenceWavetable;\n    seed.userWavetable = userWavetable;''',
'''    auto strategyTable = referenceWavetable;\n    if (strategy == 5 && loadedReferenceFile.existsAsFile())\n        if (auto chopped = ReferenceWavetableExtractor::chop (loadedReferenceFile, analysisStartSeconds.load(), analysisEndSeconds.load()))\n            strategyTable = std::move (chopped);\n    seed.referenceWavetable = strategyTable;\n    if (strategyTable) seed.referenceWavetableMix = juce::jmax (seed.referenceWavetableMix, referenceTableWeight (reference, strategy));\n    seed.userWavetable = userWavetable;''', 1)

# Replace generated-rack lifecycle with 1..8 instance sound-design roles.
new_rack = r'''void RetroMatchSynthAudioProcessor::applyGeneratedRack (const MatchResult& mainResult, int selectedBankIndex)
{
    selectEditingLayer (-1);
    allEditorNotesOff();
    for (int i = 0; i < VoiceParameters::extraLayerCount; ++i) clearLayer (i);

    applyMatchResult (mainResult);
    const int legacyInstances = juce::jlimit (1, 3, 1 + (int) apvts.getRawParameterValue ("resynthInstances")->load());
    const int complexity = juce::jlimit (0, 3, (int) apvts.getRawParameterValue ("resynthComplexity")->load());
    const int totalInstances = complexity == 0 ? legacyInstances : (complexity == 1 ? 4 : complexity == 2 ? 6 : 8);
    const int strategy = juce::jlimit (0, 5, (int) apvts.getRawParameterValue ("resynthStrategy")->load());

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

    // Restore the selected main voice after temporary layer captures.
    applyMatchResult (mainResult);
}'''
replace_between('Source/PluginProcessor.cpp',
                'void RetroMatchSynthAudioProcessor::applyGeneratedRack (const MatchResult& mainResult, int selectedBankIndex)',
                'void RetroMatchSynthAudioProcessor::updateCandidatePreview (const MatchResult& result)',
                new_rack)

# Randomize now starts from one of the intentionally designed layered factory families.
must_replace('Source/PluginProcessor.cpp',
'''    const int family = random.nextInt ((int) factoryPresetCatalog.size());\n    auto patch = SoundMatcher::makeVariation (makeFactoryPreset (family), seed, 0.10f + random.nextFloat() * 0.15f);\n    patch.outputGainDb = -12; patch.noiseMix = juce::jmin (patch.noiseMix, 0.15f);\n    applyPresetParameters (patch, "Random / " + juce::String (factoryPresetCatalog[(size_t) family].name) + " / " + juce::String::toHexString (seed).substring (0, 6));''',
'''    const int family = random.nextInt (10);\n    const int variation = 5 + random.nextInt (5);\n    const int presetIndex = 10 + family * 10 + variation;\n    auto patch = SoundMatcher::makeVariation (makeFactoryPreset (presetIndex), seed, 0.06f + random.nextFloat() * 0.08f);\n    patch.outputGainDb = juce::jlimit (-10.0f, -5.0f, patch.outputGainDb);\n    patch.noiseMix = juce::jmin (patch.noiseMix, 0.15f);\n    applyPresetParameters (patch, "Designed / " + juce::String (factoryPresetCatalog[(size_t) presetIndex].name) + " / " + juce::String::toHexString (seed).substring (0, 6));''')

# Master trim must also affect exported preview WAV files.
must_replace('Source/PluginProcessor.cpp',
'''    auto audio = OfflineRenderer::renderPatch (params, sr, juce::jlimit (0.25f, 12.0f, seconds), f0, 256);\n    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();''',
'''    auto audio = OfflineRenderer::renderPatch (params, sr, juce::jlimit (0.25f, 12.0f, seconds), f0, 256);\n    audio.applyGain (juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("masterOutputGain")->load()));\n    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();''')


# ---------------------------------------------------------------------------
# 3) Factory presets: deeper musical layer roles + safer loudness consistency.
# ---------------------------------------------------------------------------
preset = read('Source/Engine/PresetLibrary.h')
preset = preset.replace(
'''                families[family] + " / " + juce::String (variation >= 5 ? 2 + variation % 3 : 1) + " instances. "''',
'''                families[family] + " / " + juce::String (variation < 3 ? 1 : juce::jmin (6, variation - 1)) + " instances. "''')
old_layers = '''        if (variation >= 5)\n            for (int layer = 0; layer < 1 + variation % 3; ++layer)\n            {\n                auto companion = std::make_shared<VoiceParameters> (makeFactoryPreset ((seeds[family] + layer + 2) % 10));\n                companion->layers.fill (nullptr); companion->outputGainDb = -12;\n                companion->attack = p.attack * (1.0f + 0.4f * layer);\n                companion->cutoff = p.cutoff * (0.7f + layer * 0.35f);\n                p.layers[(size_t) layer] = companion;\n                p.layerGain[(size_t) layer] = 0.3f + t * 0.15f;\n                p.layerPan[(size_t) layer] = layer % 2 == 0 ? -0.35f : 0.35f;\n                p.layerTune[(size_t) layer] = layer == 0 ? -12.0f : layer == 1 ? 12.0f : 7.0f;\n                p.layerOperation[(size_t) layer] = family == 5 ? 3 : family == 8 ? 2 : 0;\n                p.layerAmount[(size_t) layer] = family == 5 ? 0.35f : 0.75f;\n            }\n        return p;'''
new_layers = '''        const int companionCount = variation < 3 ? 0 : juce::jlimit (1, 5, variation - 2);\n        p.mainLayerGain = companionCount > 0 ? 0.80f : 0.95f;\n        for (int layer = 0; layer < companionCount; ++layer)\n        {\n            auto companion = std::make_shared<VoiceParameters> (makeFactoryPreset ((seeds[family] + layer + 2) % 10));\n            companion->layers.fill (nullptr); companion->mainLayerGain = 1.0f; companion->outputGainDb = -7.5f;\n            const int role = layer % 5;\n            if (role == 0)\n            {\n                companion->attack = juce::jmax (0.002f, p.attack * 0.75f); companion->cutoff = p.cutoff * 0.78f;\n                companion->supersawMix = juce::jmax (companion->supersawMix, family == 3 || family == 9 ? 0.28f : 0.08f);\n            }\n            else if (role == 1)\n            {\n                companion->filterType = 1; companion->cutoff = juce::jlimit (2200.0f, 9800.0f, p.cutoff * 0.75f + 1800.0f);\n                companion->fmMix = juce::jmax (companion->fmMix, 0.18f); companion->stereoWidth = 1.45f; companion->reverbMix = juce::jmax (0.12f, companion->reverbMix);\n            }\n            else if (role == 2)\n            {\n                companion->osc1Wave = 0; companion->osc1Mix = 0.55f; companion->osc2Mix = 0.0f; companion->subMix = 0.28f;\n                companion->fmMix = companion->wavetableMix = companion->supersawMix = 0.0f; companion->cutoff = juce::jlimit (180.0f, 1600.0f, p.cutoff * 0.24f);\n                companion->chorusMix = companion->delayMix = companion->reverbMix = 0.0f; companion->stereoWidth = 0.85f;\n            }\n            else if (role == 3)\n            {\n                companion->wavetableMix = juce::jmax (0.32f, companion->wavetableMix); companion->supersawMix = juce::jmax (0.20f, companion->supersawMix);\n                companion->extraLfoRate[0] = 0.09f + t * 0.18f; companion->moduleModSlots[0] = { (int) ModSource::lfo2, (int) ModDestination::wavetablePosition, 0.34f };\n                companion->chorusMix = juce::jmax (0.10f, companion->chorusMix);\n            }\n            else\n            {\n                companion->fmMix = juce::jmax (0.36f, companion->fmMix); companion->fmAlgorithm = (family + variation) % 6;\n                companion->fmOpRatio[1] = 2.0f + t * 2.0f; companion->fmOpRatio[2] = 3.0f + t; companion->reverbMix = juce::jmax (0.10f, companion->reverbMix);\n            }\n            p.layers[(size_t) layer] = companion;\n            const float gains[] { 0.27f, 0.17f, 0.22f, 0.18f, 0.14f };\n            const float pans[] { -0.16f, 0.34f, 0.0f, -0.32f, 0.28f };\n            const float tunes[] { 0.0f, 12.0f, -12.0f, 0.0f, 7.0f };\n            p.layerGain[(size_t) layer] = gains[role];\n            p.layerPan[(size_t) layer] = pans[role];\n            p.layerTune[(size_t) layer] = tunes[role];\n            p.layerOperation[(size_t) layer] = 0;\n            p.layerAmount[(size_t) layer] = 0.80f;\n        }\n        const float sourceDensity = p.osc1Mix + p.osc2Mix + p.subMix + p.fmMix + p.wavetableMix + p.supersawMix;\n        p.outputGainDb = juce::jlimit (-10.5f, -5.0f, -6.0f - companionCount * 0.55f - juce::jmax (0.0f, sourceDensity - 1.2f) * 1.1f);\n        return p;'''
if old_layers not in preset:
    raise RuntimeError('Preset generated-layer block marker missing')
preset = preset.replace(old_layers, new_layers, 1)
write('Source/Engine/PresetLibrary.h', preset)


# ---------------------------------------------------------------------------
# 4) Hardware UI: always-visible MASTER, strategy/complexity, large compare popup.
# ---------------------------------------------------------------------------
compare_h = r'''#pragma once
#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "RetroLookAndFeel.h"
#include "Hardware3DKit.h"

class MatchCompareDialog final : public juce::Component, private juce::Timer
{
public:
    explicit MatchCompareDialog (RetroMatchSynthAudioProcessor& p) : proc (p)
    {
        for (auto* b : { &candidateA, &candidateB, &candidateC, &synth, &reference, &mix, &stop }) addAndMakeVisible (*b);
        candidateA.setButtonText ("A"); candidateB.setButtonText ("B"); candidateC.setButtonText ("C");
        synth.setButtonText ("SYNTH"); reference.setButtonText ("REFERENCE"); mix.setButtonText ("MIX"); stop.setButtonText ("STOP");
        candidateA.onClick = [this] { select (0); }; candidateB.onClick = [this] { select (1); }; candidateC.onClick = [this] { select (2); };
        synth.onClick = [this] { audition (RetroMatchSynthAudioProcessor::ReferenceAuditionMode::synthOnly); };
        reference.onClick = [this] { audition (RetroMatchSynthAudioProcessor::ReferenceAuditionMode::referenceOnly); };
        mix.onClick = [this] { audition (RetroMatchSynthAudioProcessor::ReferenceAuditionMode::mixed); };
        stop.onClick = [this] { proc.allEditorNotesOff(); proc.setReferenceAuditionMode (RetroMatchSynthAudioProcessor::ReferenceAuditionMode::synthOnly); };
        startTimerHz (12);
    }
    ~MatchCompareDialog() override { proc.allEditorNotesOff(); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (18); r.removeFromTop (42);
        auto top = r.removeFromTop (34); candidateA.setBounds (top.removeFromLeft (56).reduced (2)); candidateB.setBounds (top.removeFromLeft (56).reduced (2)); candidateC.setBounds (top.removeFromLeft (56).reduced (2));
        top.removeFromLeft (16); synth.setBounds (top.removeFromLeft (100).reduced (2)); reference.setBounds (top.removeFromLeft (120).reduced (2)); mix.setBounds (top.removeFromLeft (80).reduced (2)); stop.setBounds (top.removeFromLeft (70).reduced (2));
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff050809));
        const auto led = findColour (RetroLookAndFeel::primaryLed);
        const auto gold = findColour (RetroLookAndFeel::secondaryLed);
        const RetroHardware3D::Palette palette { led, gold, findColour (RetroLookAndFeel::tertiaryLed) };
        auto frame = getLocalBounds().toFloat().reduced (8);
        RetroHardware3D::drawRecessedPanel (g, frame, palette, 10.0f);
        g.setColour (gold); g.setFont (juce::Font (juce::FontOptions (18.0f, juce::Font::bold)));
        g.drawText ("REFERENCE  ↔  RESYNTH VISUAL COMPARE", 24, 16, getWidth() - 48, 26, juce::Justification::centredLeft);

        if (! proc.currentFeatures)
        { g.setColour (led.withAlpha (0.7f)); g.setFont (16.0f); g.drawText ("Load and analyze a reference first.", getLocalBounds(), juce::Justification::centred); return; }
        const auto& ref = *proc.currentFeatures;
        const SoundFeatures* candidate = proc.currentCandidateFeatures ? &*proc.currentCandidateFeatures : nullptr;
        auto body = getLocalBounds().reduced (22); body.removeFromTop (82);
        auto waveArea = body.removeFromTop (body.getHeight() * 34 / 100).toFloat().reduced (3);
        auto spectrumArea = body.removeFromTop (body.getHeight() * 52 / 100).toFloat().reduced (3);
        auto metricsArea = body.toFloat().reduced (3);
        panel (g, waveArea, "WAVEFORM / ENVELOPE", led); panel (g, spectrumArea, "SPECTRAL FINGERPRINT", gold); panel (g, metricsArea, "SIMILARITY", led);
        drawWave (g, waveArea.reduced (12, 26), ref, led, 1.5f);
        if (candidate) drawWave (g, waveArea.reduced (12, 26), *candidate, gold, 1.15f);
        drawSpectrum (g, spectrumArea.reduced (12, 26), ref, candidate, led, gold);
        drawMetrics (g, metricsArea.reduced (12, 24), led, gold);
    }

private:
    RetroMatchSynthAudioProcessor& proc;
    juce::TextButton candidateA, candidateB, candidateC, synth, reference, mix, stop;

    void timerCallback() override { repaint(); }
    void select (int index) { if (proc.selectCandidate (index)) repaint(); }
    void audition (RetroMatchSynthAudioProcessor::ReferenceAuditionMode mode)
    {
        proc.allEditorNotesOff(); proc.setReferenceAuditionMode (mode);
        proc.noteOnFromEditor (proc.getReferenceBaseMidiNote(), 0.78f);
    }
    static void panel (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title, juce::Colour accent)
    {
        g.setColour (juce::Colour (0xff071012)); g.fillRoundedRectangle (r, 8);
        g.setColour (juce::Colour (0xff445457)); g.drawRoundedRectangle (r, 8, 1);
        g.setColour (accent); g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
        g.drawText (title, r.withHeight (22).reduced (8, 0), juce::Justification::centredLeft);
    }
    static void drawWave (juce::Graphics& g, juce::Rectangle<float> r, const SoundFeatures& f, juce::Colour colour, float width)
    {
        if (f.waveformPreview.empty()) return;
        juce::Path path;
        for (size_t i = 0; i < f.waveformPreview.size(); ++i)
        {
            const float x = r.getX() + (float) i / (float) juce::jmax<size_t> (1, f.waveformPreview.size() - 1) * r.getWidth();
            const float y = r.getCentreY() - f.waveformPreview[i] * r.getHeight() * 0.45f;
            if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
        }
        g.setColour (colour.withAlpha (0.15f)); g.strokePath (path, juce::PathStrokeType (width + 6));
        g.setColour (colour); g.strokePath (path, juce::PathStrokeType (width));
    }
    static void drawSpectrum (juce::Graphics& g, juce::Rectangle<float> r, const SoundFeatures& ref, const SoundFeatures* candidate, juce::Colour a, juce::Colour b)
    {
        const float w = r.getWidth() / SoundFeatures::spectralBandCount;
        for (int i = 0; i < SoundFeatures::spectralBandCount; ++i)
        {
            const float x = r.getX() + i * w;
            const float rh = juce::jlimit (0.0f, 1.0f, ref.spectralBands[(size_t) i]) * r.getHeight();
            g.setColour (a.withAlpha (0.48f)); g.fillRect (x, r.getBottom() - rh, juce::jmax (1.0f, w * 0.44f), rh);
            if (candidate)
            {
                const float ch = juce::jlimit (0.0f, 1.0f, candidate->spectralBands[(size_t) i]) * r.getHeight();
                g.setColour (b.withAlpha (0.62f)); g.fillRect (x + w * 0.48f, r.getBottom() - ch, juce::jmax (1.0f, w * 0.44f), ch);
            }
        }
    }
    void drawMetrics (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour led, juce::Colour gold)
    {
        const auto& s = proc.lastMatch.similarity;
        const std::array<std::pair<const char*, float>, 8> values {{
            { "TOTAL", s.total }, { "SPECTRUM", s.spectrum }, { "TIMBRE", s.timbre }, { "TEMPORAL", s.temporal },
            { "HARMONIC", s.harmonic }, { "ENVELOPE", s.envelope }, { "PITCH", s.pitch }, { "STEREO", s.stereo }
        }};
        const int columns = 4; const float cw = r.getWidth() / columns; const float rh = r.getHeight() / 2.0f;
        for (int i = 0; i < (int) values.size(); ++i)
        {
            auto cell = juce::Rectangle<float> (r.getX() + (i % columns) * cw, r.getY() + (i / columns) * rh, cw, rh).reduced (7, 5);
            g.setColour (juce::Colour (0xff142023)); g.fillRoundedRectangle (cell, 5);
            g.setColour (i == 0 ? gold : led); g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
            g.drawText (values[(size_t) i].first, cell.removeFromTop (18), juce::Justification::centredLeft);
            auto bar = cell.removeFromBottom (8); g.setColour (juce::Colour (0xff263235)); g.fillRoundedRectangle (bar, 3);
            g.setColour (i == 0 ? gold : led); g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, values[(size_t) i].second)), 3);
            g.drawText (juce::String (values[(size_t) i].second * 100.0f, 1) + "%", cell, juce::Justification::centredRight);
        }
    }
};
'''
write('Source/UI/MatchCompareDialog.h', compare_h)

must_replace('Source/UI/RetroMatchEditorV3.h',
'''    juce::TextButton lightSwitch { "LED: MINT" };\n    int displayedPalette = -1;''',
'''    juce::TextButton lightSwitch { "LED: MINT" };\n    juce::Slider masterOutput;\n    juce::Label masterOutputLabel;\n    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> masterOutputAttachment;\n    int displayedPalette = -1;''')

must_replace('Source/UI/RetroMatchEditorV3.h',
'''    juce::TextButton load { "LOAD REFERENCE" }, quick { "QUICK x3" }, refine { "REFINE x3" }, aiVariants { "AI x3" };''',
'''    juce::TextButton load { "LOAD REFERENCE" }, quick { "QUICK x3" }, refine { "REFINE x3" }, aiVariants { "AI x3" }, compareMatch { "COMPARE" };''')

must_replace('Source/UI/RetroMatchEditorV3.cpp',
'''#include "LayersPage.h"\n#include "ReferenceEditorDialog.h"\n#include <BinaryData.h>''',
'''#include "LayersPage.h"\n#include "ReferenceEditorDialog.h"\n#include "MatchCompareDialog.h"\n#include <BinaryData.h>''')

must_replace('Source/UI/RetroMatchEditorV3.cpp',
'''    for (auto* button : { &savePatch, &loadPatch, &exportPreview, &keyboardToggle }) addAndMakeVisible (*button);''',
'''    for (auto* button : { &savePatch, &loadPatch, &exportPreview, &keyboardToggle }) addAndMakeVisible (*button);\n\n    masterOutputLabel.setText ("MASTER", juce::dontSendNotification);\n    masterOutputLabel.setJustificationType (juce::Justification::centred);\n    masterOutputLabel.setColour (juce::Label::textColourId, goldColour (*this));\n    masterOutputLabel.setFont (juce::Font (juce::FontOptions (9.0f, juce::Font::bold)));\n    masterOutput.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);\n    masterOutput.setTextBoxStyle (juce::Slider::TextBoxRight, false, 50, 18);\n    masterOutput.setTextValueSuffix (" dB");\n    masterOutput.setNumDecimalPlacesToDisplay (1);\n    masterOutput.setTooltip ("Global master output trim. This is independent from each patch OUTPUT and stays unchanged while browsing presets.");\n    masterOutputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "masterOutputGain", masterOutput);\n    addAndMakeVisible (masterOutputLabel); addAndMakeVisible (masterOutput);''')

must_replace('Source/UI/RetroMatchEditorV3.cpp',
'''    candidateA.onClick = [this] { selectCandidate (0); };\n    candidateB.onClick = [this] { selectCandidate (1); };\n    candidateC.onClick = [this] { selectCandidate (2); };\n    for (auto* button : { &candidateA, &candidateB, &candidateC }) addAndMakeVisible (*button);''',
'''    candidateA.onClick = [this] { selectCandidate (0); };\n    candidateB.onClick = [this] { selectCandidate (1); };\n    candidateC.onClick = [this] { selectCandidate (2); };\n    for (auto* button : { &candidateA, &candidateB, &candidateC }) addAndMakeVisible (*button);\n    addAndMakeVisible (compareMatch);\n    compareMatch.setTooltip ("Open a large waveform, spectral and metric comparison with A/B audition controls.");\n    compareMatch.onClick = [this]\n    {\n        if (! proc.currentFeatures || ! proc.currentCandidateFeatures) return;\n        auto* content = new MatchCompareDialog (proc); content->setSize (1040, 700);\n        juce::DialogWindow::LaunchOptions options; options.content.setOwned (content);\n        options.dialogTitle = "RM-01  /  REFERENCE ↔ RESYNTH COMPARE";\n        options.dialogBackgroundColour = juce::Colour (0xff06090a);\n        options.escapeKeyTriggersCloseButton = true; options.useNativeTitleBar = true; options.resizable = true; options.componentToCentreAround = this;\n        if (auto* window = options.launchAsync()) window->setResizeLimits (780, 520, 1600, 1050);\n    };''')

must_replace('Source/UI/RetroMatchEditorV3.cpp',
'''    subtitle.setBounds (header.removeFromLeft (juce::jmax (120, header.getWidth() - 490)));\n    const int actionW = juce::jmax (78, header.getWidth() / 5);\n    lightSwitch.setBounds (header.removeFromRight (actionW).reduced (3, 9));''',
'''    subtitle.setBounds (header.removeFromLeft (juce::jmax (120, header.getWidth() - 490)));\n    auto masterArea = header.removeFromLeft (100);\n    masterOutputLabel.setBounds (masterArea.removeFromTop (15));\n    masterOutput.setBounds (masterArea.reduced (2, 1));\n    const int actionW = juce::jmax (72, header.getWidth() / 5);\n    lightSwitch.setBounds (header.removeFromRight (actionW).reduced (3, 9));''')

must_replace('Source/UI/RetroMatchEditorV3.cpp',
'''    auto actionRow = w.removeFromTop (32);\n    const int buttonW = actionRow.getWidth() / 3;\n    quick.setBounds (actionRow.removeFromLeft (buttonW).reduced (2, 0));\n    refine.setBounds (actionRow.removeFromLeft (buttonW).reduced (2, 0));\n    aiVariants.setBounds (actionRow.reduced (2, 0));''',
'''    auto actionRow = w.removeFromTop (32);\n    const int buttonW = actionRow.getWidth() / 4;\n    quick.setBounds (actionRow.removeFromLeft (buttonW).reduced (2, 0));\n    refine.setBounds (actionRow.removeFromLeft (buttonW).reduced (2, 0));\n    aiVariants.setBounds (actionRow.removeFromLeft (buttonW).reduced (2, 0));\n    compareMatch.setBounds (actionRow.reduced (2, 0));''')

must_replace('Source/UI/RetroMatchEditorV3.cpp',
'''    const bool canSelect = proc.hasReferenceSample() && ! (worker && worker->isThreadRunning());''',
'''    compareMatch.setEnabled (proc.currentFeatures.has_value() && proc.currentCandidateFeatures.has_value());\n    const bool canSelect = proc.hasReferenceSample() && ! (worker && worker->isThreadRunning());''')

# Strategy-aware local A/B/C search.
must_replace('Source/UI/RetroMatchEditorV3.cpp',
'''    auto settings = proc.matchSettings;\n    if (! refined)''',
'''    auto settings = proc.matchSettings;\n    const int strategy = juce::jlimit (0, 5, (int) paramValue (proc, "resynthStrategy", 0));\n    settings.algorithm = strategy;\n    auto strategyTable = proc.referenceWavetable;\n    if (strategy == 5 && proc.getReferenceFile().existsAsFile())\n        if (auto chopped = ReferenceWavetableExtractor::chop (proc.getReferenceFile(), proc.getAnalysisStartSeconds(), proc.getAnalysisEndSeconds()))\n            strategyTable = std::move (chopped);\n    for (auto& seed : seeds)\n    {\n        seed.referenceWavetable = strategyTable;\n        if (strategyTable)\n        {\n            float target = 0.24f + reference.pitchConfidence * 0.16f + reference.harmonicity * 0.12f;\n            if (strategy == 1) target = 0.76f; else if (strategy == 2) target = 0.05f; else if (strategy == 3) target = 0.14f; else if (strategy == 4) target = 0.42f; else if (strategy == 5) target = 0.68f;\n            seed.referenceWavetableMix = juce::jmax (seed.referenceWavetableMix, juce::jlimit (0.0f, 0.90f, target));\n        }\n    }\n    if (! refined)''', 1)

# Layers page exposes the professional resynthesis modes while retaining legacy 1-3 automation.
must_replace('Source/UI/LayersPage.h',
'''        addAndMakeVisible (hint); hint.setText ("INSTANCE RACK / Refine and candidate selection own the generated rack: old loaded/edited instances are removed first. Layered resynthesis can combine 1-3 complementary candidates; manual instances can still be added up to 8 total. EDIT jumps the selected instance into the normal synth tabs.", juce::dontSendNotification);''',
'''        addAndMakeVisible (hint); hint.setText ("INSTANCE RACK / Generated resynthesis can stay classic or build a 4, 6 or 8-instance studio stack. Roles are voiced as body, air, foundation, motion, harmonic colour, width and texture instead of cloning the same patch. EDIT jumps any instance into the normal synth tabs.", juce::dontSendNotification);''')

must_replace('Source/UI/LayersPage.h',
'''        resynthAttachment = std::make_unique<ComboAttachment> (proc.apvts, "resynthInstances", resynthInstances);\n        addAndMakeVisible (editMain);''',
'''        resynthAttachment = std::make_unique<ComboAttachment> (proc.apvts, "resynthInstances", resynthInstances);\n        addAndMakeVisible (strategyLabel); strategyLabel.setText ("RESYNTH METHOD", juce::dontSendNotification); strategyLabel.setJustificationType (juce::Justification::centredRight);\n        addAndMakeVisible (strategy); strategy.addItemList ({ "BALANCED HYBRID", "REFERENCE WAVETABLE", "SPECTRAL SUBTRACTIVE", "FM / HARMONIC", "LAYERED STUDIO", "TEXTURE / CHOP" }, 1);\n        strategyAttachment = std::make_unique<ComboAttachment> (proc.apvts, "resynthStrategy", strategy);\n        strategy.setTooltip ("Changes the search topology. Reference Wavetable and Texture/Chop deliberately lean on cycles extracted from the selected sample region.");\n        addAndMakeVisible (complexityLabel); complexityLabel.setText ("STACK DEPTH", juce::dontSendNotification); complexityLabel.setJustificationType (juce::Justification::centredRight);\n        addAndMakeVisible (complexity); complexity.addItemList ({ "CLASSIC / 1-3", "STUDIO / 4", "DEEP / 6", "MAXIMUM / 8" }, 1);\n        complexityAttachment = std::make_unique<ComboAttachment> (proc.apvts, "resynthComplexity", complexity);\n        complexity.setTooltip ("Studio and deeper modes add complementary sound-design roles at conservative levels instead of simply duplicating a candidate.");\n        addAndMakeVisible (editMain);''')

must_replace('Source/UI/LayersPage.h',
'''        auto resynth = r.removeFromTop (30); resynthLabel.setBounds (resynth.removeFromLeft (150)); resynthInstances.setBounds (resynth.removeFromLeft (220).reduced (2));\n        editMain.setBounds (r.removeFromTop (30).reduced (2));''',
'''        auto resynth = r.removeFromTop (30); resynthLabel.setBounds (resynth.removeFromLeft (150)); resynthInstances.setBounds (resynth.removeFromLeft (220).reduced (2));\n        auto method = r.removeFromTop (30); strategyLabel.setBounds (method.removeFromLeft (150)); strategy.setBounds (method.removeFromLeft (260).reduced (2)); complexityLabel.setBounds (method.removeFromLeft (110)); complexity.setBounds (method.reduced (2));\n        editMain.setBounds (r.removeFromTop (30).reduced (2));''')

must_replace('Source/UI/LayersPage.h',
'''    juce::Label hint, resynthLabel; juce::ComboBox resynthInstances; juce::TextButton add, editMain; juce::Slider mainGain; SynthInstanceVisual mainVisual;\n    std::unique_ptr<SliderAttachment> mainAttachment;\n    std::unique_ptr<ComboAttachment> resynthAttachment;''',
'''    juce::Label hint, resynthLabel, strategyLabel, complexityLabel;\n    juce::ComboBox resynthInstances, strategy, complexity; juce::TextButton add, editMain; juce::Slider mainGain; SynthInstanceVisual mainVisual;\n    std::unique_ptr<SliderAttachment> mainAttachment;\n    std::unique_ptr<ComboAttachment> resynthAttachment, strategyAttachment, complexityAttachment;''')

# Rename random preset action to reflect the curated layered generator.
must_replace('Source/UI/PresetsPage.h',
'''        randomize.setButtonText ("RANDOMIZE NEW PATCH"); audition.setButtonText ("AUDITION");''',
'''        randomize.setButtonText ("DESIGN NEW LAYERED PATCH"); audition.setButtonText ("AUDITION");''')


# ---------------------------------------------------------------------------
# 5) Tests: require genuinely deep factory presets. Remove the temporary long
#    state probe that caused a Windows runner crash while retaining chunked MIDI.
# ---------------------------------------------------------------------------
melody_tests = read('Tests/MelodyTests.cpp')
long_probe = '''    MelodyClip longClip; longClip.duration = 185.0; longClip.notes.push_back ({ 72, 121.25, 0.5, 0.7f, 0.9f });\n    const auto longRestored = MelodyClip::fromState (longClip.toState());\n    if (longRestored.duration < 180.0 || longRestored.notes.empty() || longRestored.notes[0].start < 120.0)\n        return fail ("long-track melody state was truncated to the old 60 second limit");\n'''
if long_probe in melody_tests:
    melody_tests = melody_tests.replace(long_probe, '', 1)
write('Tests/MelodyTests.cpp', melody_tests)

must_replace('Tests/SmokeTests.cpp',
'''        if (factoryPresetCatalog.size() != 110) return fail ("factory catalog must contain 110 presets");''',
'''        if (factoryPresetCatalog.size() != 110) return fail ("factory catalog must contain 110 presets");\n        bool foundDeepFactoryPreset = false;\n        for (int i = 10; i < (int) factoryPresetCatalog.size(); ++i)\n        {\n            const auto designed = makeFactoryPreset (i); int activeLayers = 0;\n            for (const auto& layer : designed.layers) if (layer) ++activeLayers;\n            foundDeepFactoryPreset |= activeLayers >= 4;\n        }\n        if (! foundDeepFactoryPreset) return fail ("factory library must contain genuinely deep multi-layer patches");''')

print('Professional sound-designer migration applied.')
