# Building RetroMatch Synth

RetroMatch Synth is a C++20/JUCE project. JUCE contains the VST3 SDK support required by a modern JUCE VST3 build, so you do **not** need to install Steinberg's VST3 SDK separately for this project. Audio Unit builds are macOS-only.

## 1. Required tools

For a Windows-only VST3 build you do not need Xcode. For an AU build you must use macOS/Xcode.

### Windows 10/11 — VST3 + Standalone
Install:

1. **Visual Studio 2026** (preferred) or Visual Studio 2022.
2. In Visual Studio Installer select **Desktop development with C++**. Include MSVC, a current Windows SDK, and CMake tools.
3. **Git for Windows**.
4. **CMake**: 3.24+ is sufficient with Visual Studio 2022. Visual Studio 2026 requires **CMake 4.2+**, because the `Visual Studio 18 2026` generator was added in CMake 4.2.
5. Internet access for the first build, unless you already have a local JUCE 9.0.1 checkout.

### Optional one-command Windows tool bootstrap

On a clean Windows 11 machine, open PowerShell and run:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\setup-windows.ps1
```

This uses `winget` to install/update Git, current CMake, and Visual Studio Community with the **Desktop development with C++** workload. Restart Windows if the Visual Studio installer requests it, then open a new PowerShell window.

If you already have a suitable Visual Studio C++ toolchain and only want Git/CMake:

```powershell
.\scripts\setup-windows.ps1 -SkipVisualStudio
```

Verify from PowerShell:

```powershell
cd RetroMatchSynth
.\scripts\check-tools.ps1
```

### macOS — AU + VST3 + Standalone
Install:

1. Current **Xcode** from Apple.
2. Xcode Command Line Tools: `xcode-select --install` if not already installed.
3. CMake 3.24+ (Homebrew: `brew install cmake`).
4. Git (provided by Xcode CLT or Homebrew).

## 2. Easiest Windows build

Open PowerShell in the project root:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\build-windows.ps1
```

To build and run the DSP/matcher smoke suite in the same build tree:

```powershell
.\scripts\build-windows.ps1 -RunTests
```

The script:

- checks CMake and Git;
- detects Visual Studio 2026 first, then Visual Studio 2022;
- clones the official JUCE **9.0.1** source into `extern/JUCE` if needed;
- configures x64 Release;
- builds the VST3 and Standalone targets.

Expected output:

```text
build-windows/RetroMatchSynth_artefacts/Release/VST3/RetroMatch Synth.vst3
build-windows/RetroMatchSynth_artefacts/Release/Standalone/RetroMatch Synth.exe
```

To copy the plug-in to JUCE's normal user/system destination after build:

```powershell
.\scripts\build-windows.ps1 -CopyPlugin
```

Or install the `.vst3` bundle manually into the standard Windows VST3 folder:

```text
C:\Program Files\Common Files\VST3\
```

Then rescan VST3 plug-ins in Cubase, REAPER, Ableton Live, Studio One, etc.

## 3. Build with an existing JUCE checkout

If JUCE is already installed, avoid all network dependency:

```powershell
.\scripts\build-windows.ps1 -JuceDir C:\dev\JUCE
```

Or configure manually:

```powershell
cmake -S . -B build-windows -G "Visual Studio 18 2026" -A x64 `
  -DRETROMATCH_JUCE_DIR=C:\dev\JUCE
cmake --build build-windows --config Release --target RetroMatchSynth_VST3 RetroMatchSynth_Standalone --parallel
```

If you use Visual Studio 2022 replace the generator with `Visual Studio 17 2022`.

## 4. macOS universal AU/VST3 build

From Terminal:

```bash
chmod +x scripts/build-macos.sh
./scripts/build-macos.sh
```

To include and run the DSP/matcher smoke suite:

```bash
RUN_TESTS=1 ./scripts/build-macos.sh
```

The default script requests a Universal Binary containing Apple Silicon (`arm64`) and Intel (`x86_64`). Expected artifacts:

```text
build-macos/RetroMatchSynth_artefacts/Release/VST3/RetroMatch Synth.vst3
build-macos/RetroMatchSynth_artefacts/Release/AU/RetroMatch Synth.component
build-macos/RetroMatchSynth_artefacts/Release/Standalone/RetroMatch Synth.app
```

Typical install locations:

```text
~/Library/Audio/Plug-Ins/VST3/
~/Library/Audio/Plug-Ins/Components/
```

For distribution outside your own Mac, Apple signing/notarization should be added. A local development build does not require a paid Apple Developer account just to compile and test locally.

## 5. Create a Windows distribution ZIP

After a successful Windows Release build:

```powershell
.\scripts\package-windows.ps1
```

The package is written under `dist/`.

## 6. Source integrity check

Before configuring JUCE you can run the dependency-free project check:

```bash
python3 scripts/static-check.py
```

It checks project version/languages, delimiter balance and the key v1.0 DSP/parameter/UI/matcher plumbing. GitHub Actions runs this gate before either native build job.

## 7. Recommended validation tools

Before treating a build as release-ready:

- run JUCE/Tracktion **pluginval** against the VST3;
- scan/load in Cubase and at least one second VST3 host;
- test 44.1/48/96 kHz and buffer sizes 32–2048;
- automate parameters from the DAW;
- save/reload a DAW session;
- test `.rmsynth` patch save/load;
- test mono MIDI, chords, repeated note stealing and long FX tails;
- on macOS validate both VST3 and AU and run `auval` for the AU.

Example AU validation once installed:

```bash
auval -a | grep -i RetroMatch
```

## 8. JUCE licensing

JUCE licensing depends on how you distribute and monetize the plug-in. Review the current JUCE licence terms before publishing a commercial binary. The source build itself uses the official JUCE repository; no third-party binary SDK is embedded in this repository.

## 9. Why there is no single cross-platform compiled bundle

A Windows `.vst3` must be compiled with a Windows toolchain, while an Audio Unit must be compiled on macOS/Xcode. A Linux build machine cannot produce a native, properly validated AU, and this project intentionally avoids unsupported cross-compilation tricks for release artifacts.

## 10. Build binaries automatically with GitHub Actions

The repository contains `.github/workflows/ci-build.yml`. After you push this project to GitHub, open **Actions → Build plug-ins → Run workflow**. It builds on native hosted runners, runs the v1.0 DSP/matcher smoke tests, and publishes two downloadable workflow artifacts:

- `RetroMatchSynth-Windows-x64` — VST3 + Standalone EXE;
- `RetroMatchSynth-macOS-Universal` — AU + VST3 + Standalone app.

This is also a useful way to prove that a clean machine can build the project and removes dependency on your workstation configuration.

## 11. Verification notes

See [`VERIFICATION.md`](VERIFICATION.md) for the static audit and the exact reason a native binary could not be emitted from the generation container.
