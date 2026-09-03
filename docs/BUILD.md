# Building RetroMatch Synth

RetroMatch Synth is a C++20/JUCE project. JUCE provides the VST3 integration used by this repository, so a separate Steinberg VST3 SDK checkout is not required. Audio Unit builds are macOS-only.

## 1. Recommended Windows build

Open PowerShell in the repository root and run:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\build-windows.ps1
```

The build script now performs a prerequisite scan before configuring CMake. It checks:

- Git for Windows;
- CMake 3.24+;
- compatibility between CMake and the installed Visual Studio generator;
- Visual Studio 2022/2026 C++ Build Tools with the MSVC C++ toolchain.

If something is missing, the script prints the missing components and asks:

```text
Install the missing prerequisites automatically with winget? [Y/n]
```

Only missing components are installed. The automatic C++ toolchain install uses Visual Studio Build Tools 2022 with `Microsoft.VisualStudio.Workload.VCTools` and recommended components; it does not require the full Visual Studio IDE.

For unattended setup/build:

```powershell
.\scripts\build-windows.ps1 -InstallMissing -NonInteractive
```

To bypass the prerequisite scan when the environment is managed externally:

```powershell
.\scripts\build-windows.ps1 -SkipPrerequisiteCheck
```

To build and run the DSP/matcher smoke suite:

```powershell
.\scripts\build-windows.ps1 -RunTests
```

To copy the VST3 to JUCE's normal plug-in destination after the build:

```powershell
.\scripts\build-windows.ps1 -CopyPlugin
```

Expected native Windows artifacts:

```text
build-windows/RetroMatchSynth_artefacts/Release/VST3/RetroMatch Synth.vst3
build-windows/RetroMatchSynth_artefacts/Release/Standalone/RetroMatch Synth.exe
```

## 2. JUCE handling

If `-JuceDir` is not supplied, the Windows build checks `extern/JUCE`. If JUCE 9.0.1 is not present, it clones the official JUCE 9.0.1 repository there.

Use an existing checkout to avoid the network dependency:

```powershell
.\scripts\build-windows.ps1 -JuceDir C:\dev\JUCE
```

Or configure CMake manually with:

```text
-DRETROMATCH_JUCE_DIR=/path/to/JUCE
```

## 3. Docker clean-room builds

Docker support is included for users who want the host machine to remain free of CMake, Git, JUCE and Visual Studio Build Tools.

There are two container images because native plug-in binaries are operating-system specific:

| Docker file | Container OS | Output |
|---|---|---|
| `Dockerfile.windows` | Windows Server Core 2022 + VS Build Tools | Windows VST3 + Windows Standalone EXE |
| `Dockerfile` | Ubuntu 24.04 + GCC/JUCE dependencies | Linux VST3 + Linux Standalone + smoke tests |

### Native Windows VST3 in Docker

From Windows PowerShell:

```powershell
.\scripts\build-windows.ps1 -UseDocker
```

or directly:

```powershell
.\scripts\build-docker.ps1 -Target Windows
```

The Docker helper:

1. checks whether Docker Desktop is installed;
2. asks to install Docker Desktop with `winget` if it is missing;
3. starts Docker Desktop when possible;
4. checks the active container engine;
5. switches Docker Desktop to Windows containers when required;
6. builds a Windows Server Core image containing Visual Studio Build Tools, CMake and JUCE;
7. compiles the Windows VST3, Standalone executable and smoke tests inside the image;
8. copies only the release artifacts back to `dist/docker-windows-release`;
9. removes the temporary container and, by default, the build image.

The only host prerequisite for this mode is Docker Desktop and the Windows container features it requires.

To automatically install Docker Desktop without prompting:

```powershell
.\scripts\build-windows.ps1 -UseDocker -InstallMissing -NonInteractive
```

Keep the large build image between runs to make subsequent builds faster:

```powershell
.\scripts\build-docker.ps1 -Target Windows -KeepImage
```

Force a completely fresh image build:

```powershell
.\scripts\build-docker.ps1 -Target Windows -NoCache
```

### Windows-container requirements

Native Windows containers require a supported Windows Pro/Enterprise host and Docker Desktop installed with Windows-container support. Docker Desktop can switch container engines from the CLI with `docker desktop engine use windows`; the helper uses this command automatically.

Windows Home/Education systems cannot use the native Windows-container path. On those systems use either:

```powershell
.\scripts\build-docker.ps1 -Target Linux
```

for isolated validation/Linux binaries, or GitHub Actions for native Windows binaries.

The Windows image is intentionally large because Visual Studio Build Tools and the Windows SDK are installed inside it. This trades disk/download size for a clean host. The helper removes the image after exporting artifacts unless `-KeepImage` is specified.

### Linux clean-room build

The Ubuntu image follows JUCE's Linux dependency requirements and builds the same synth engine plus smoke tests:

```powershell
.\scripts\build-docker.ps1 -Target Linux
```

Artifacts are copied to:

```text
dist/docker-linux-release/
```

These are **Linux binaries** and will not load in a Windows DAW. The Linux image is useful for clean-room compilation, static validation, test execution and Linux VST3 output.

## 4. Why Docker cannot replace every native build

Containers do not make plug-in formats platform-independent.

- Windows VST3/EXE requires a Windows/MSVC environment. `Dockerfile.windows` provides that using Windows containers.
- Linux Docker builds produce Linux VST3/Standalone binaries only.
- Audio Units still require macOS/Xcode and cannot be produced by a Windows or Linux Docker image.

For a machine with Docker configured only for Linux containers, GitHub Actions is usually the simplest way to obtain native Windows and macOS outputs without installing local build toolchains.

## 5. macOS universal AU/VST3 build

Install Xcode, Xcode Command Line Tools, CMake 3.24+ and Git, then run:

```bash
chmod +x scripts/build-macos.sh
./scripts/build-macos.sh
```

To include the smoke suite:

```bash
RUN_TESTS=1 ./scripts/build-macos.sh
```

Expected artifacts:

```text
build-macos/RetroMatchSynth_artefacts/Release/VST3/RetroMatch Synth.vst3
build-macos/RetroMatchSynth_artefacts/Release/AU/RetroMatch Synth.component
build-macos/RetroMatchSynth_artefacts/Release/Standalone/RetroMatch Synth.app
```

## 6. Manual Windows prerequisites

If you do not want automatic installation, install:

1. Visual Studio 2026 or Visual Studio 2022 / Build Tools;
2. the C++ workload (`Microsoft.VisualStudio.Workload.VCTools` or Desktop development with C++);
3. a current Windows SDK;
4. Git for Windows;
5. CMake 3.24+.

Visual Studio 2026 requires a CMake version that supports the `Visual Studio 18 2026` generator. The build script detects generator compatibility instead of assuming that any installed CMake is sufficient.

The older bootstrap script remains available:

```powershell
.\scripts\setup-windows.ps1
```

but it is no longer required for the normal build because `build-windows.ps1` performs its own checks and optional installation.

## 7. Create a Windows distribution ZIP

After a successful native Windows Release build:

```powershell
.\scripts\package-windows.ps1
```

The package is written under `dist/`.

## 8. Source integrity check

Before configuring JUCE:

```bash
python3 scripts/static-check.py
```

This validates project version/languages, delimiter balance and important v1.0 DSP/parameter/UI/matcher plumbing. GitHub Actions runs the source gate before native build jobs.

## 9. GitHub Actions clean-machine builds

`.github/workflows/ci-build.yml` builds on native hosted runners and publishes:

- `RetroMatchSynth-Windows-x64` — Windows VST3 + Standalone EXE;
- `RetroMatchSynth-macOS-Universal` — macOS VST3 + AU + Standalone app.

This is the recommended fallback when local native prerequisites or Windows-container support are unavailable.

## 10. Release validation

Before distributing binaries:

- run `pluginval` against the VST3;
- load/scan in Cubase and at least one additional VST3 host;
- test 44.1/48/96 kHz and buffer sizes 32–2048;
- exercise DAW automation and project recall;
- test `.rmsynth` patch save/load;
- test note stealing, chords and FX tails;
- on macOS validate the AU with `auval`.

See [`VERIFICATION.md`](VERIFICATION.md) for the current verification status.

## 11. Licensing notes

JUCE licensing depends on distribution and monetization. Review the current JUCE licence before shipping commercial binaries.

Docker Desktop also has its own licence/subscription terms. The Docker setup is optional; native scripts and GitHub Actions remain supported alternatives.
