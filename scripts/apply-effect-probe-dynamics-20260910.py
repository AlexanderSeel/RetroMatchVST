from pathlib import Path


def replace_exact(path: str, old: str, new: str, count: int = 1) -> None:
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    actual = text.count(old)
    if actual != count:
        raise RuntimeError(f"{path}: expected {count} occurrence(s), found {actual}: {old[:120]!r}")
    p.write_text(text.replace(old, new), encoding="utf-8")


probe = "Source/Matching/EffectChainProbe.h"
tests = "Tests/SmokeTests.cpp"

# Use one physical-domain mapping for both reference and candidate: normalized crest factor.
# This avoids comparing transient/sustain metadata on one side to actual peak/RMS dynamics
# on the other side.
replace_exact(
    probe,
    "class EffectChainProbe\n{\npublic:\n    static EffectProbeSignature referenceTarget (const SoundFeatures& f)",
    "class EffectChainProbe\n{\npublic:\n    static float crestDynamics (float peak, float rms) noexcept\n"
    "    {\n"
    "        if (peak <= 1.0e-6f || rms <= 1.0e-6f) return 0.0f;\n"
    "        const float crest = peak / juce::jmax (1.0e-5f, rms);\n"
    "        return juce::jlimit (0.0f, 1.0f, (crest - 1.0f) / 3.5f);\n"
    "    }\n\n"
    "    static EffectProbeSignature referenceTarget (const SoundFeatures& f)",
)
replace_exact(
    probe,
    "        s.dynamics = juce::jlimit (0.0f, 1.0f, f.transientScore * 0.68f + (1.0f - f.sustainLevel) * 0.32f);",
    "        s.dynamics = crestDynamics (f.peak, f.rms);",
)
replace_exact(
    probe,
    "        const float activeRms = std::sqrt ((float) (activeSq / noiseSamples));\n        const float crest = peak / juce::jmax (1.0e-5f, activeRms);\n        s.dynamics = juce::jlimit (0.0f, 1.0f, (crest - 1.0f) / 3.5f);",
    "        const float activeRms = std::sqrt ((float) (activeSq / noiseSamples));\n        s.dynamics = crestDynamics (peak, activeRms);",
)

needle = """        if (globalDrivenSignature.nonlinear <= globalDrySignature.nonlinear)
            return fail (\"two-tone FX probe ignored post-sum global saturation\");
    }
    {
        SoundFeatures guitar;"""
insert = """        if (globalDrivenSignature.nonlinear <= globalDrySignature.nonlinear)
            return fail (\"two-tone FX probe ignored post-sum global saturation\");

        SoundFeatures dynamicsReference;
        dynamicsReference.rms = 0.25f;
        dynamicsReference.peak = 0.50f; // crest = 2 -> (2 - 1) / 3.5
        dynamicsReference.transientScore = 0.0f;
        dynamicsReference.sustainLevel = 1.0f;
        const auto dynamicsA = EffectChainProbe::referenceTarget (dynamicsReference);
        if (std::abs (dynamicsA.dynamics - (1.0f / 3.5f)) > 1.0e-5f)
            return fail (\"FX probe reference dynamics did not use peak/RMS crest factor\");

        dynamicsReference.transientScore = 1.0f;
        dynamicsReference.sustainLevel = 0.0f;
        const auto dynamicsB = EffectChainProbe::referenceTarget (dynamicsReference);
        if (std::abs (dynamicsA.dynamics - dynamicsB.dynamics) > 1.0e-6f)
            return fail (\"FX probe reference dynamics still depends on transient/sustain surrogate metadata\");
    }
    {
        SoundFeatures guitar;"""
replace_exact(tests, needle, insert)

print("Effect probe crest-domain correction applied")
