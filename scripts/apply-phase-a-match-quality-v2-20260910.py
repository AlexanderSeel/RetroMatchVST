from pathlib import Path

source_path = Path("scripts/apply-phase-a-match-quality-20260910.py")
text = source_path.read_text(encoding="utf-8")
old = """    count=2,
)
replace_exact(
    processor,
    \"    base.distortionMode = authored.distortionMode; base.distortionMix = authored.distortionMix;\","""
new = """    count=1,
)
replace_exact(
    processor,
    \"    base.distortionMode = authored.distortionMode; base.distortionMix = authored.distortionMix;\","""
if text.count(old) != 1:
    raise RuntimeError("Could not locate the stale seed-distortion occurrence guard")
text = text.replace(old, new)

namespace = {"__name__": "__main__", "__file__": str(source_path)}
exec(compile(text, str(source_path), "exec"), namespace)

replace_exact = namespace["replace_exact"]

# The texture profile now owns a local evidence variable, therefore give the switch case
# an explicit scope so MSVC/Clang never jump across its initialization.
replace_exact(
    "Source/Matching/SoundMatcherCore.inc",
    "        case 5: // Texture / chopped-table reconstruction.\n            if (hasReferenceTable)",
    "        case 5: // Texture / chopped-table reconstruction.\n        {\n            if (hasReferenceTable)",
)
replace_exact(
    "Source/Matching/SoundMatcherCore.inc",
    "            p.extraLfoRate[0] = juce::jlimit (0.05f, 1.8f, 0.10f + reference.spectralMotion * 2.4f);\n            break;\n\n\n        case 6:",
    "            p.extraLfoRate[0] = juce::jlimit (0.05f, 1.8f, 0.10f + reference.spectralMotion * 2.4f);\n            break;\n        }\n\n\n        case 6:",
)

# Gold uses the same fields on separate lines, so keep its matching seed neutral unless
# the user explicitly enabled the FX lock.
replace_exact(
    "Source/PluginProcessor.cpp",
    """        seed.distortionMode = authored.distortionMode;
        seed.distortionMix = authored.distortionMix;
        seed.layers.fill (nullptr);""",
    """        seed.distortionMode = matchSettings.lockEffects ? authored.distortionMode : 0;
        seed.distortionMix = matchSettings.lockEffects ? authored.distortionMix : 1.0f;
        seed.layers.fill (nullptr);""",
)

print("Phase A v3 staging correction applied")
