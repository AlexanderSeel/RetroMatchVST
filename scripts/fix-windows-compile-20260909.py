from pathlib import Path

replacements = {
    "Source/UI/MatchCompareDialog.h": [
        (
            """        if (! compareMeasurePool.addJob (job, true))\n        {\n            delete job;\n            compareMeasureRunning = false;\n        }\n""",
            """        compareMeasurePool.addJob (job, true);\n""",
        ),
    ],
    "Source/Engine/SynthEngine.cpp": [
        (
            """void SynthEngine::processBuiltInEffects (juce::AudioBuffer<float>& audio)\n{\n    const auto processDrive = [&] (auto& block)\n""",
            """void SynthEngine::processBuiltInEffects (juce::AudioBuffer<float>& audio)\n{\n    const auto n = audio.getNumSamples();\n    const auto channels = audio.getNumChannels();\n    const int quality = qualityIndex (current.oversamplingQuality);\n\n    const auto processDrive = [&] (auto& block)\n""",
        ),
    ],
    "Source/UI/SignalLabPage.h": [
        (
            """        if (auto* window = options.launchAsync())\n        {\n            window->setResizeLimits (900, 540, 2400, 1600);\n            window->centreWithSize (1320, 760);\n        }\n""",
            """        if (auto* dialogWindow = options.launchAsync())\n        {\n            dialogWindow->setResizeLimits (900, 540, 2400, 1600);\n            dialogWindow->centreWithSize (1320, 760);\n        }\n""",
        ),
    ],
    "Source/UI/MelodyPage.h": [
        (
            """g.drawText (\"SAMPLE PREVIEW  /  drag START + END\", bounds.getX() + 10, bounds.getY() + 3, bounds.getWidth() - 20, 14, juce::Justification::centredLeft);""",
            """g.drawText (\"SAMPLE PREVIEW  /  drag START + END\", juce::Rectangle<float> (bounds.getX() + 10.0f, bounds.getY() + 3.0f, bounds.getWidth() - 20.0f, 14.0f), juce::Justification::centredLeft);""",
        ),
    ],
    "Source/UI/RetroMatchEditorV3.cpp": [
        (
            """auto left = meterLabels.removeFromLeft ((int) meterLabels.getWidth() / 2);""",
            """auto left = meterLabels.removeFromLeft (meterLabels.getWidth() * 0.5f);""",
        ),
    ],
}

for filename, edits in replacements.items():
    path = Path(filename)
    text = path.read_text(encoding="utf-8")
    original = text
    for before, after in edits:
        count = text.count(before)
        if count != 1:
            raise SystemExit(f"Expected exactly one match in {filename}, got {count}: {before[:80]!r}")
        text = text.replace(before, after, 1)
    path.write_text(text, encoding="utf-8")
    print(f"patched {filename}: {len(original)} -> {len(text)} bytes")
