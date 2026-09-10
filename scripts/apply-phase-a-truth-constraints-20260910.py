from pathlib import Path


def replace_exact(path: str, old: str, new: str, count: int = 1) -> None:
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    actual = text.count(old)
    if actual != count:
        raise RuntimeError(f"{path}: expected {count} occurrence(s), found {actual}: {old[:120]!r}")
    p.write_text(text.replace(old, new), encoding="utf-8")


matcher = "Source/Matching/SoundMatcher.cpp"
processor = "Source/PluginProcessor.cpp"
advisor = "Source/Matching/ResynthesisAdvisor.h"
tests = "Tests/SmokeTests.cpp"

# The one-shot wrapper has its own evaluation path. Keep the transfer probe diagnostic,
# exactly like the shared matcher core, rather than letting FX colour move the score by 14%.
replace_exact(
    matcher,
    "            result.similarity.total * 0.86f + result.effectProbeSimilarity * 0.14f);",
    "            result.similarity.total * 0.95f + result.effectProbeSimilarity * 0.05f);",
)

# Topology trials may explore wavefold only when the selected profile already established
# nonlinear fold as part of the inverse model. A clean one-shot cannot acquire it by chance.
replace_exact(
    matcher,
    "            if (random.nextFloat() < 0.35f) candidate.wavefold = random.nextFloat() * 0.42f;",
    "            if (profiledSeed.wavefold > 0.02f && random.nextFloat() < 0.20f)\n"
    "                candidate.wavefold = random.nextFloat() * juce::jmin (0.42f, juce::jmax (0.08f, profiledSeed.wavefold * 1.6f));",
)

# GOLD may tune existing nonlinear topology but must not invent saturation on the post-sum
# bus. Creative saturation belongs to Magic, not reference-truth matching.
replace_exact(
    processor,
    "        static const int sensibleTypes[] { 0, 1, 2, 3, 6, 7, 8, 9, 13 };",
    "        static const int sensibleTypes[] { 0, 1, 2, 6, 7, 8, 9, 13 };",
)

old_voice_target = """                    static const int usefulMsegTargets[] {
                        (int) ModDestination::amplitude,
                        (int) ModDestination::cutoff,
                        (int) ModDestination::wavetablePosition,
                        (int) ModDestination::wavefold
                    };
                    voice->msegTarget = usefulMsegTargets[random.nextInt ((int) std::size (usefulMsegTargets))];"""
new_voice_target = """                    static const int safeMsegTargets[] {
                        (int) ModDestination::amplitude,
                        (int) ModDestination::cutoff,
                        (int) ModDestination::wavetablePosition
                    };
                    if (voice->wavefold > 0.02f && random.nextFloat() < 0.18f)
                        voice->msegTarget = (int) ModDestination::wavefold;
                    else
                        voice->msegTarget = safeMsegTargets[random.nextInt ((int) std::size (safeMsegTargets))];"""
replace_exact(processor, old_voice_target, new_voice_target)

old_rack_target = """            static const int usefulMsegTargets[] {
                (int) ModDestination::amplitude,
                (int) ModDestination::cutoff,
                (int) ModDestination::wavetablePosition,
                (int) ModDestination::wavefold
            };
            rack.msegTarget = usefulMsegTargets[random.nextInt ((int) std::size (usefulMsegTargets))];"""
new_rack_target = """            static const int safeMsegTargets[] {
                (int) ModDestination::amplitude,
                (int) ModDestination::cutoff,
                (int) ModDestination::wavetablePosition
            };
            if (rack.wavefold > 0.02f && random.nextFloat() < 0.18f)
                rack.msegTarget = (int) ModDestination::wavefold;
            else
                rack.msegTarget = safeMsegTargets[random.nextInt ((int) std::size (safeMsegTargets))];"""
replace_exact(processor, old_rack_target, new_rack_target)

# 'Pitched + transient + band-limited' is far too broad: acid/303 plucks satisfy it. The
# FX/Guitar strategy now requires a guitar/pluck body AND independent evidence of a processed
# or naturally complex source (inharmonicity or a spatial/tail signature).
old_guitar = """        const bool guitarLike = pitch > 0.30f && transient > 0.34f && harmonic > 0.26f && flat < 0.50f
                             && f.spectralCentroidHz > 350.0f && f.spectralCentroidHz < 6500.0f
                             && f.spectralRolloffHz > 1200.0f && f.spectralRolloffHz < 16000.0f;"""
new_guitar = """        const bool pluckedPitchedBody = pitch > 0.30f && transient > 0.34f && harmonic > 0.26f && flat < 0.50f
                                  && f.spectralCentroidHz > 350.0f && f.spectralCentroidHz < 6500.0f
                                  && f.spectralRolloffHz > 1200.0f && f.spectralRolloffHz < 16000.0f;
        const bool processedPluckEvidence = f.inharmonicity > 0.12f
                                         || (tail > 0.24f && stereo > 0.10f)
                                         || (f.releaseSeconds > 0.22f && f.duration > 0.85f && stereo > 0.06f);
        const bool guitarLike = pluckedPitchedBody && processedPluckEvidence;"""
replace_exact(advisor, old_guitar, new_guitar)
replace_exact(
    advisor,
    "            case 6: a.reason = \"A pitched transient body plus band-limited colour/tail is guitar-like; rebuild the body and search compressor, drive, cabinet filtering, modulation, delay and reverb as an ordered chain.\"; break;",
    "            case 6: a.reason = \"A pitched transient body plus independent processed/complex evidence is guitar-like; rebuild the body and search dynamics, cabinet filtering, modulation and space, adding drive only when nonlinear evidence supports it.\"; break;",
)

# Regression: a clean acid/303-style pitched transient must not be routed to FX/Guitar simply
# because it is bright, harmonic and band-limited. The existing guitar fixture must still pass.
needle = """        if (! std::isfinite (fxMatch.effectProbeSimilarity) || fxMatch.effectProbeSimilarity < 0.0f || fxMatch.effectProbeSimilarity > 1.0f)
            return fail (\"FX / Guitar Chain diagnostic probe score invalid\");
    }
    {
        SoundFeatures clean;"""
insert = """        if (! std::isfinite (fxMatch.effectProbeSimilarity) || fxMatch.effectProbeSimilarity < 0.0f || fxMatch.effectProbeSimilarity > 1.0f)
            return fail (\"FX / Guitar Chain diagnostic probe score invalid\");

        SoundFeatures acid;
        acid.duration = 0.65f; acid.fundamentalHz = 110.0f; acid.pitchConfidence = 0.97f;
        acid.harmonicity = 0.94f; acid.inharmonicity = 0.04f; acid.transientScore = 0.64f;
        acid.spectralFlatness = 0.04f; acid.spectralCentroidHz = 2400.0f; acid.spectralRolloffHz = 9000.0f;
        acid.lowEnergyRatio = 0.18f; acid.highEnergyRatio = 0.24f; acid.zeroCrossingRate = 0.10f;
        acid.attackSeconds = 0.002f; acid.decaySeconds = 0.18f; acid.sustainLevel = 0.36f; acid.releaseSeconds = 0.09f;
        acid.stereoWidth = 0.02f; acid.spectralMotion = 0.05f;
        const auto acidAdvice = ResynthesisAdvisor::advise (acid);
        if (acidAdvice.method == 6)
            return fail (\"clean acid/303-like reference was misrouted to FX / Guitar Chain\");
    }
    {
        SoundFeatures clean;"""
replace_exact(tests, needle, insert)

print("Phase A truth-matching constraints applied")
