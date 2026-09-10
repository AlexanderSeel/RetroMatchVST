#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def replace_once(path: Path, old: str, new: str, label: str) -> None:
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected 1 match in {path}, found {count}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")


matcher_h = ROOT / "Source/Matching/SoundMatcher.h"
replace_once(
    matcher_h,
    '''    static MatchResult evaluateFit (const SoundFeatures& reference, const VoiceParameters& params,\n                                    const MatchSettings& settings = {});\n    static MatchResult refineFit (const SoundFeatures& reference,\n''',
    '''    static MatchResult evaluateFit (const SoundFeatures& reference, const VoiceParameters& params,\n                                    const MatchSettings& settings = {});\n\n    // Reference lifecycle is a hard generation invariant, separate from similarity scoring.\n    // This is intentionally public so AI variants and layered/GOLD racks cannot reintroduce\n    // keyboard sustain, looping amplitude motion or time-based tails after matching.\n    static bool referenceSelfTerminates (const SoundFeatures& reference);\n    static void enforceReferenceLifecycle (const SoundFeatures& reference, VoiceParameters& params);\n\n    static MatchResult refineFit (const SoundFeatures& reference,\n''',
    "SoundMatcher public lifecycle API")

matcher_cpp = ROOT / "Source/Matching/SoundMatcher.cpp"
replace_once(
    matcher_cpp,
    '''    removeTimeBasedTailEffects (p);\n}\n\nfloat rmsBetween''',
    '''    removeTimeBasedTailEffects (p);\n}\n\nvoid enforceSelfTerminatingTree (VoiceParameters& p, const SoundFeatures& reference, int depth = 0)\n{\n    enforceSelfTerminatingEnvelope (p, reference);\n    if (depth >= VoiceParameters::extraLayerCount) return;\n\n    // VoiceParameters copies share layer pointers. Clone before normalising so evaluating\n    // or constraining one generated rack never mutates another candidate through aliasing.\n    for (auto& layer : p.layers)\n    {\n        if (! layer) continue;\n        layer = std::make_shared<VoiceParameters> (*layer);\n        enforceSelfTerminatingTree (*layer, reference, depth + 1);\n    }\n}\n\nfloat rmsBetween''',
    "recursive lifecycle helper")

replace_once(
    matcher_cpp,
    '''}\n\nMatchResult SoundMatcher::initialFit (const SoundFeatures& reference)\n{\n''',
    '''}\n\nbool SoundMatcher::referenceSelfTerminates (const SoundFeatures& reference)\n{\n    return isSelfTerminatingReference (reference);\n}\n\nvoid SoundMatcher::enforceReferenceLifecycle (const SoundFeatures& reference, VoiceParameters& params)\n{\n    if (! isSelfTerminatingReference (reference)) return;\n    enforceSelfTerminatingTree (params, reference);\n}\n\nMatchResult SoundMatcher::initialFit (const SoundFeatures& reference)\n{\n''',
    "public lifecycle implementation")

# Use the reusable lifecycle API in the public seed/refinement paths too.  evaluateFit stays\n# deliberately non-normalising so it can still expose/penalise an invalid sustained candidate.
text = matcher_cpp.read_text(encoding="utf-8")
text = text.replace('''        enforceSelfTerminatingEnvelope (result.params, reference);''',
                    '''        enforceReferenceLifecycle (reference, result.params);''')
text = text.replace('''    enforceSelfTerminatingEnvelope (profiledSeed, reference);''',
                    '''    enforceReferenceLifecycle (reference, profiledSeed);''')
text = text.replace('''        enforceSelfTerminatingEnvelope (candidate, reference);''',
                    '''        enforceReferenceLifecycle (reference, candidate);''')
matcher_cpp.write_text(text, encoding="utf-8")

processor = ROOT / "Source/PluginProcessor.cpp"
replace_once(
    processor,
    '''        rack.layerAmount[(size_t) layer] = 0.78f;\n    }\n    return rack;\n}\n\nfloat goldGaussian''',
    '''        rack.layerAmount[(size_t) layer] = 0.78f;\n    }\n\n    // Layer roles are allowed to add motion/space for sustained references, but a detected\n    // one-shot must remain self-ending as a complete instrument, not only as its main voice.\n    SoundMatcher::enforceReferenceLifecycle (reference, rack);\n    return rack;\n}\n\nfloat goldGaussian''',
    "GOLD rack construction lifecycle")
replace_once(
    processor,
    '''            rack.msegTarget = usefulMsegTargets[random.nextInt ((int) std::size (usefulMsegTargets))];\n        }\n    }\n    return rack;\n}\n\nMatchResult evolveGoldRack''',
    '''            rack.msegTarget = usefulMsegTargets[random.nextInt ((int) std::size (usefulMsegTargets))];\n        }\n    }\n\n    // Evolution may mutate MSEG/FX topology after the initial rack was made. Re-apply the\n    // lifecycle constraint before scoring so mutation cannot resurrect a held tail.\n    SoundMatcher::enforceReferenceLifecycle (reference, rack);\n    return rack;\n}\n\nMatchResult evolveGoldRack''',
    "GOLD rack mutation lifecycle")

ai = ROOT / "Source/AI/AISeedProvider.cpp"
replace_once(
    ai,
    '''        applySuggestion (params, *suggestion);\n        params.referenceWavetable = base.referenceWavetable;\n        auto evaluated = SoundMatcher::evaluateFit (reference, params, matchSettings);\n''',
    '''        applySuggestion (params, *suggestion);\n        params.referenceWavetable = base.referenceWavetable;\n        // Provider JSON is only a proposal. Reference lifecycle is authoritative so an AI\n        // variant cannot turn a detected one-shot back into a keyboard-sustained patch.\n        SoundMatcher::enforceReferenceLifecycle (reference, params);\n        auto evaluated = SoundMatcher::evaluateFit (reference, params, matchSettings);\n''',
    "AI candidate lifecycle")

# Extend the existing regression with a deliberately invalid layered rack. This catches the\n# exact post-matcher regression path: child sustain/MSEG/FX must be normalised recursively.
test = ROOT / "Tests/OneShotEnvelopeTests.cpp"
replace_once(
    test,
    '''    if (badScore.tailSilenceSimilarity >= goodScore.tailSilenceSimilarity - 0.40f)\n        return fail ("sustained and self-terminating candidates were not separated by tail scoring");\n\n    const auto refined = SoundMatcher::refineFit (reference, sustainedCandidate, settings);\n''',
    '''    if (badScore.tailSilenceSimilarity >= goodScore.tailSilenceSimilarity - 0.40f)\n        return fail ("sustained and self-terminating candidates were not separated by tail scoring");\n\n    auto layeredCandidate = seed;\n    layeredCandidate.sustain = 0.66f;\n    layeredCandidate.mseg.enabled = true;\n    layeredCandidate.mseg.loopEnabled = true;\n    layeredCandidate.delayMix = 0.35f;\n    layeredCandidate.reverbMix = 0.30f;\n    layeredCandidate.globalFxModules[0].type = 8;\n    layeredCandidate.globalFxModules[0].mix = 0.45f;\n    auto child = std::make_shared<VoiceParameters> (layeredCandidate);\n    child->layers.fill (nullptr);\n    child->sustain = 0.74f;\n    child->mseg.enabled = true;\n    child->mseg.loopEnabled = true;\n    child->delayMix = 0.40f;\n    child->reverbMix = 0.32f;\n    child->fxModules[0].type = 9;\n    child->fxModules[0].mix = 0.50f;\n    layeredCandidate.layers[0] = std::move (child);\n\n    SoundMatcher::enforceReferenceLifecycle (reference, layeredCandidate);\n    if (layeredCandidate.sustain > 1.0e-6f || layeredCandidate.mseg.loopEnabled\n        || layeredCandidate.delayMix > 1.0e-6f || layeredCandidate.reverbMix > 1.0e-6f\n        || layeredCandidate.globalFxModules[0].type == 8 || layeredCandidate.globalFxModules[0].type == 9)\n        return fail ("root rack violated the enforced one-shot lifecycle");\n    if (! layeredCandidate.layers[0])\n        return fail ("lifecycle enforcement unexpectedly removed a rack layer");\n    const auto& constrainedChild = *layeredCandidate.layers[0];\n    if (constrainedChild.sustain > 1.0e-6f || constrainedChild.mseg.loopEnabled\n        || constrainedChild.delayMix > 1.0e-6f || constrainedChild.reverbMix > 1.0e-6f\n        || constrainedChild.fxModules[0].type == 8 || constrainedChild.fxModules[0].type == 9)\n        return fail ("child rack voice violated the enforced one-shot lifecycle");\n\n    const auto heldLayered = OfflineRenderer::renderPatch (layeredCandidate, sampleRate, 1.30f, fundamental, 128, {}, true);\n    const float layeredBodyRms = rmsBetween (heldLayered, 0, bodyEnd);\n    const float layeredTailRms = rmsBetween (heldLayered, tailStart, heldLayered.getNumSamples());\n    if (layeredBodyRms <= 1.0e-5f || layeredTailRms > layeredBodyRms * 0.03f + 1.0e-5f)\n        return fail ("constrained layered one-shot did not self-terminate while note remained held");\n\n    const auto refined = SoundMatcher::refineFit (reference, sustainedCandidate, settings);\n''',
    "layered lifecycle regression")

static = ROOT / "scripts/static-check.py"
replace_once(
    static,
    '''    'AI local scoring': ['buildPrompt', 'postJson', 'SoundMatcher::evaluateFit', 'generateVariants'],''',
    '''    'AI local scoring': ['buildPrompt', 'postJson', 'SoundMatcher::enforceReferenceLifecycle', 'SoundMatcher::evaluateFit', 'generateVariants'],''',
    "AI lifecycle static contract")
replace_once(
    static,
    '''    'layered resynthesis lifecycle': ['\"resynthInstances\"', 'applyGeneratedRack', 'clearLayer (i)', 'captureLayer (layer)', 'RESYNTH'],''',
    '''    'layered resynthesis lifecycle': ['\"resynthInstances\"', 'applyGeneratedRack', 'clearLayer (i)', 'captureLayer (layer)', 'SoundMatcher::enforceReferenceLifecycle', 'RESYNTH'],''',
    "layered lifecycle static contract")

print("Applied reference lifecycle invariant across matcher, AI and layered/GOLD generation")
