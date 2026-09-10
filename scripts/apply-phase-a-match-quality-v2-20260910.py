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

# Gold uses the same fields on separate lines, so keep its matching seed neutral unless
# the user explicitly enabled the FX lock.
replace_exact = namespace["replace_exact"]
replace_exact(
    "Source/PluginProcessor.cpp",
    """        seed.distortionMode = authored.distortionMode;
        seed.distortionMix = authored.distortionMix;
        seed.layers.fill (nullptr);""",
    """        seed.distortionMode = matchSettings.lockEffects ? authored.distortionMode : 0;
        seed.distortionMix = matchSettings.lockEffects ? authored.distortionMix : 1.0f;
        seed.layers.fill (nullptr);""",
)

print("Phase A v2 staging correction applied")
