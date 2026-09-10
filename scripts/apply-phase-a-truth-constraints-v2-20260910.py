from pathlib import Path

path = Path("Source/Matching/SoundMatcherCore.inc")
text = path.read_text(encoding="utf-8")
old = "            if (random.nextFloat() < 0.35f) candidate.wavefold = random.nextFloat() * 0.42f;"
new = (
    "            if (profiledSeed.wavefold > 0.02f && random.nextFloat() < 0.20f)\n"
    "                candidate.wavefold = random.nextFloat() * juce::jmin (0.42f, juce::jmax (0.08f, profiledSeed.wavefold * 1.6f));"
)
actual = text.count(old)
if actual != 1:
    raise RuntimeError(f"Expected exactly one shared-core random wavefold topology trial, found {actual}")
path.write_text(text.replace(old, new), encoding="utf-8")
print("Shared matcher nonlinear topology constraint applied")
