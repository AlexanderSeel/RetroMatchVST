# Verification — v1.0.0

## Passed in this environment

- `python3 scripts/static-check.py` — PASS.
- Source bracket/delimiter checks — PASS.
- v1 reference-wavetable/candidate-bank plumbing checks — PASS.
- CMake detects GNU C 14.2.0 and GNU C++ 14.2.0 and reaches JUCE FetchContent.
- macOS/Windows build scripts and GitHub Actions workflow are included from the v0.4 line and target JUCE 9.0.1.

## Blocked by environment

The current container cannot resolve `github.com`, so FetchContent cannot clone JUCE 9.0.1. The actual JUCE compilation and CTest execution cannot run here. This is an environment/network stop before source compilation, not a reported compiler failure.

## Native release gate

Before publishing binaries, run:

- Windows: `scripts/build-windows.ps1 -RunTests`
- macOS: `RUN_TESTS=1 ./scripts/build-macos.sh`
- Load the resulting VST3 in at least one VST3 host and the AU in Logic/AUVal on macOS.
- Verify sample load, Quick Match, Refine Match, A/B/C, morph, preset reload, DAW session reload, MIDI polyphony and WAV export.
