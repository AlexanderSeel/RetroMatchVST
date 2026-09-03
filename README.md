# RetroMatch Synth

**Sample-to-synth reconstruction as an editable VST3/AU instrument.**

RetroMatch Synth is a C++20/JUCE hybrid synthesizer whose primary goal is to take a short reference sound, analyze its acoustic and spectral character, and recreate that sound as closely as possible with a **real, editable synthesis patch**.

Instead of turning the reference into a static sampler preset, RetroMatch combines subtractive, wavetable, additive, unison, phase-modulation and six-operator FM synthesis with filtering, modulation and effects. A closed-loop matcher renders candidate patches offline, compares them with the reference, and iteratively searches for better parameter combinations.

The result remains a normal synthesizer sound: every oscillator, FM operator, envelope, filter, modulation route and effect can be adjusted, automated, saved and reused after matching.

> **Version:** 1.0.0 — first complete source milestone  
> **Formats:** VST3, Audio Unit (macOS), Standalone  
> **Framework:** JUCE 9.0.1 / C++20 / CMake  
> **Platforms:** Windows and macOS

---

## Why RetroMatch exists

Recreating a synth sound by ear often involves a long cycle of guessing oscillator types, envelopes, filters, FM relationships, modulation and effects. A spectrum analyzer helps, but it still leaves the user translating measurements into synthesis decisions manually.

RetroMatch automates much of that process:

```text
Reference sample
      ↓
Audio analysis
      ↓
Feature-derived seed patch
      ↓
Offline resynthesis
      ↓
Perceptual comparison
      ↓
Population optimization
      ↓
Editable synth patch
```

The matcher does **not** merely copy the sample and does not require a neural black-box generator. It searches the parameters of the same synth engine that is used for live playback, so a successful match produces a patch you can understand and continue editing.

---

## Main workflow

1. **Load a reference** WAV, AIFF or FLAC file, or drag it onto the plug-in.
2. **Quick Match** analyzes the source and creates an initial synthesis patch.
3. **Refine Match** renders candidate sounds in the background and evolves the parameters against a perceptual similarity score.
4. **Build A/B/C** generates three alternative reconstruction strategies with different synthesis biases.
5. Select **A**, **B** or **C**, or continuously **morph** between them.
6. Use **Match Locks** to preserve parts of the sound while refining other areas.
7. Edit any synth parameter manually or automate it from the DAW.
8. **Save Patch** as `.rmsynth` or **Export WAV** as a 24-bit preview.

Reference-derived wavetable data is stored inside the patch/session state, so the original reference audio does not need to remain available after extraction.

---

## Hybrid synthesis engine

RetroMatch 1.0 uses a deliberately broad synthesis architecture because many sounds cannot be represented well by one synthesis method alone.

### Virtual-analog oscillators

- sine
- triangle
- anti-aliased BLEP saw
- anti-aliased BLEP square/pulse
- variable pulse width
- two primary oscillators
- master tuning/detuning
- sub oscillator
- white noise
- ring modulation

### Additive synthesis

A 12-partial harmonic layer provides additional spectral shaping for sounds whose overtone balance is difficult to reproduce with standard oscillators alone.

### Factory wavetable engine

- five morphable wavetable frames
- 2048 samples per table
- continuous frame interpolation
- phase warp
- independent wavetable mix
- modulation-matrix control of wavetable position

### Reference-derived wavetable

RetroMatch can extract periodic cycle snapshots from the uploaded reference sound:

```text
Reference audio
   ├─ snapshot 1 → phase-stabilized cycle
   ├─ snapshot 2 → phase-stabilized cycle
   ├─ snapshot 3 → phase-stabilized cycle
   ├─ snapshot 4 → phase-stabilized cycle
   └─ snapshot 5 → phase-stabilized cycle
                         ↓
              morphing reference bank
```

The extracted five-frame bank is normalized, stored as synth data and blended through the **Reference WT** control. It is serialized into `.rmsynth` presets and DAW state.

### Supersaw / unison

- seven oscillator voices
- symmetric detune distribution
- stereo spread
- constant-power panning
- independent unison mix

This gives the matcher a practical way to reproduce wide pads, trance leads and modern detuned textures.

### Phase modulation

A lightweight phase-modulation path remains available for simple FM/PM tones and as another search dimension.

### Six-operator FM

The deeper FM engine provides:

- six sine operators
- six routing algorithms
- operator feedback
- global FM mix
- ratio mode
- fixed-frequency mode
- ratio range from 0.125× to 16×
- fixed frequencies from approximately 10 Hz to 16 kHz
- individual operator level
- individual ADSR per operator
- velocity sensitivity
- key scaling

This allows harmonic and inharmonic structures such as bells, electric-piano transients, metallic percussion, digital basses and evolving FM pads.

### Wavefolder

A pre-filter wavefolder adds controllable nonlinear harmonic generation and is available as a modulation destination and optimizer dimension.

---

## Filter, modulation and effects

### Filter

- multimode state-variable filter
- low-pass
- high-pass
- band-pass
- cutoff
- resonance

### Amp and modulation

- polyphonic amp ADSR
- LFO
- four-slot modulation matrix

Modulation sources:

- LFO 1
- velocity
- key tracking
- random-per-note
- amp envelope

Modulation destinations:

- pitch
- filter cutoff
- amplitude
- pulse width
- phase-modulation amount
- six-operator FM mix
- wavetable position
- wavefold amount

Each modulation slot has a bipolar amount.

### Effects

The global effects chain currently includes:

- nonlinear drive
- stereo chorus
- feedback delay
- algorithmic reverb
- stereo-width processing
- output gain

Effects are part of the matching space because a reference sound's identity often depends heavily on chorus, ambience, delay and nonlinear processing rather than oscillator choice alone.

---

## Reference analysis

The analyzer extracts a compact feature representation that can be calculated for both the reference and the generated candidates.

### Pitch and harmonic information

- fundamental frequency (F0)
- pitch confidence
- harmonicity
- inharmonicity
- odd/even harmonic balance
- zero-crossing rate

### Spectral information

- 32 logarithmic spectral bands
- spectral centroid
- 85% spectral rolloff
- spectral bandwidth
- spectral flatness
- low-frequency energy
- high-frequency energy

### Temporal information

- attack estimate
- decay estimate
- sustain estimate
- release estimate
- transient strength
- eight temporal analysis frames
- 16 logarithmic spectral bands per temporal frame
- normalized RMS contour
- spectral-motion descriptor

### Timbre and stereo information

- 12 cepstral/DCT timbre coefficients
- stereo width derived from the original L/R signal
- compact waveform preview used by the UI

The same analysis path is used for uploaded audio and candidate renders, reducing mismatches between the measurement domains.

---

## Closed-loop matching

Quick Match is only the seed stage. The deeper matcher works by rendering the actual synth engine and evaluating the result.

```text
Candidate parameters
       ↓
OfflineRenderer
       ↓
SynthEngine
       ↓
Generated audio
       ↓
SampleAnalyzer
       ↓
SimilarityScorer
       ↓
score / rank / mutate / crossover
       ↺
```

The current weighted similarity model combines approximately:

| Feature group | Weight |
|---|---:|
| Global spectral shape | 25% |
| Temporal spectrum / energy | 18% |
| Cepstral timbre | 12% |
| Brightness / bandwidth / flatness | 14% |
| Envelope / transient behavior | 14% |
| Harmonic / inharmonic character | 10% |
| Pitch | 4% |
| Stereo image | 3% |

Raw loudness is intentionally not allowed to dominate the score.

### Population optimizer

Refine Match uses a derivative-free evolutionary search rather than gradient descent because the parameter space contains discrete oscillator/filter/FM topology decisions as well as continuous values.

The optimizer:

- retains the feature-derived seed
- tests different oscillator/filter/FM topologies
- ranks an elite population by perceptual score
- selects stronger parents more frequently
- applies coarse-to-fine mutation
- crosses over parameter groups between candidates
- increases exploration after stagnation
- clamps parameters to valid ranges
- applies Match Locks after topology changes, mutation and crossover
- always retains the best/seed candidate so refinement cannot intentionally return a lower-scoring result

---

## A/B/C alternatives and morphing

One reference sound may have several convincing synthesis explanations. RetroMatch therefore keeps three deliberately biased solutions:

- **A — Reference-oriented:** favors the extracted reference wavetable and direct timbral reconstruction.
- **B — FM-oriented:** favors six-operator FM/inharmonic structures.
- **C — Hybrid-oriented:** favors wavetable, unison and broader hybrid synthesis.

The candidate morph control maps continuously from A → B → C. Continuous parameters are interpolated; discrete topology choices switch at appropriate boundaries. The resulting intermediate sound is still a normal editable patch.

---

## Match Locks

The following synthesis groups can be locked during refinement:

- Pitch
- Oscillators
- FM
- Envelope
- Filter
- Modulation
- Effects

Example: once the oscillator/FM structure is convincing, lock those groups and allow only filter and effects parameters to continue evolving.

---

## Presets and state

RetroMatch uses `juce::AudioProcessorValueTreeState` for its host-visible parameter model.

The v1.0 plug-in exposes **122 DAW-automatable parameters**:

- 104 continuous parameters
- 18 choice parameters

`.rmsynth` patches and DAW session state include the synth settings and the extracted reference wavetable data. This allows a matched patch to remain self-contained after the original sample has been moved or deleted.

---

## User interface

The custom JUCE UI is inspired by the functional language of late-1980s/1990s professional Japanese hardware synthesizers without copying any specific manufacturer's protected panel design.

Design goals include:

- dark graphite/chassis surfaces
- machined retro-style rotary controls
- hardware-like panel grouping
- green/cyan analyzer display character
- waveform/spectrum feedback
- clear dedicated matching controls
- dynamic six-operator detail editing
- scalable editor layout
- practical access to the large parameter surface without presenting every FM parameter simultaneously

The interface is intended to feel like a modern instrument with classic hardware ergonomics rather than a generic web-style plug-in panel.

---

## Supported plug-in formats

| Platform | VST3 | AU | Standalone |
|---|:---:|:---:|:---:|
| Windows 10/11 | ✓ | — | ✓ |
| macOS | ✓ | ✓ | ✓ |

Audio Units require macOS/Xcode and cannot be built natively on Windows or Linux.

---

## Building

Full setup, local-JUCE and CI instructions are in [`docs/BUILD.md`](docs/BUILD.md).

### Windows quick start

On a clean Windows development machine:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\setup-windows.ps1
```

Open a new PowerShell after installation/restart, then:

```powershell
.\scripts\check-tools.ps1
.\scripts\build-windows.ps1 -RunTests
```

Expected release artifacts:

```text
build-windows/RetroMatchSynth_artefacts/Release/VST3/RetroMatch Synth.vst3
build-windows/RetroMatchSynth_artefacts/Release/Standalone/RetroMatch Synth.exe
```

### macOS quick start

```bash
chmod +x scripts/build-macos.sh
RUN_TESTS=1 ./scripts/build-macos.sh
```

Expected release artifacts:

```text
build-macos/RetroMatchSynth_artefacts/Release/VST3/RetroMatch Synth.vst3
build-macos/RetroMatchSynth_artefacts/Release/AU/RetroMatch Synth.component
build-macos/RetroMatchSynth_artefacts/Release/Standalone/RetroMatch Synth.app
```

### Existing local JUCE checkout

To avoid any network fetch, point CMake at JUCE 9.0.1:

```powershell
.\scripts\build-windows.ps1 -JuceDir C:\dev\JUCE -RunTests
```

or configure manually with:

```text
-DRETROMATCH_JUCE_DIR=/path/to/JUCE
```

JUCE provides the VST3 SDK integration required by this project; a separate Steinberg SDK checkout is not required for the normal JUCE build.

---

## GitHub Actions builds

`.github/workflows/ci-build.yml` contains native Windows and macOS jobs. The workflow runs the static source gate and DSP/matcher smoke tests before publishing build artifacts.

Expected CI artifacts:

- `RetroMatchSynth-Windows-x64` — VST3 + Standalone EXE
- `RetroMatchSynth-macOS-Universal` — VST3 + AU + Standalone app

This is the recommended clean-machine build path until signed installers are introduced.

---

## Tests and verification

Run the dependency-free integrity check first:

```bash
python3 scripts/static-check.py
```

When JUCE and a native toolchain are available, enable the CTest smoke suite using the supplied platform scripts.

The smoke tests exercise key first-version behavior including:

- synth rendering
- non-silent output
- stereo unison behavior
- six-operator FM
- fixed-frequency FM operators
- operator envelopes
- wavetable motion
- wavefolding
- modulation routing
- reference analysis
- temporal/cepstral descriptors
- Quick Match
- Refine Match
- best-score non-regression
- reference-wavetable serialization round trip

Before distributing binaries, also validate with `pluginval`, at least two VST3 hosts, DAW automation/session recall, and `auval` on macOS. See [`docs/VERIFICATION.md`](docs/VERIFICATION.md).

### Current source verification status

The v1.0 source tree passes the included static integrity checks. During generation, the available Linux container successfully reached the JUCE dependency-fetch stage with both C and C++ compilers configured, but outbound DNS to GitHub was blocked before JUCE could be downloaded. Consequently the repository deliberately does **not** claim that the generated source has been natively compiled in that container; native CI/local builds are the release gate.

---

## Project structure

```text
RetroMatchVST/
├─ CMakeLists.txt
├─ README.md
├─ CHANGELOG.md
├─ Source/
│  ├─ Analysis/
│  │  ├─ SampleAnalyzer.h
│  │  └─ SampleAnalyzer.cpp
│  ├─ Engine/
│  │  ├─ SynthEngine.h
│  │  ├─ SynthEngine.cpp
│  │  ├─ ReferenceWavetable.h
│  │  └─ ReferenceWavetable.cpp
│  ├─ Matching/
│  │  ├─ OfflineRenderer.*
│  │  ├─ SimilarityScorer.*
│  │  └─ SoundMatcher.*
│  ├─ UI/
│  │  └─ RetroLookAndFeel.h
│  ├─ PluginProcessor.*
│  └─ PluginEditor.*
├─ Tests/
│  └─ SmokeTests.cpp
├─ docs/
│  ├─ ARCHITECTURE.md
│  ├─ BUILD.md
│  ├─ ROADMAP.md
│  └─ VERIFICATION.md
├─ scripts/
│  ├─ setup-windows.ps1
│  ├─ check-tools.ps1
│  ├─ build-windows.ps1
│  ├─ package-windows.ps1
│  ├─ build-macos.sh
│  └─ static-check.py
└─ .github/workflows/
   └─ ci-build.yml
```

---

## Technical design principles

RetroMatch follows a few important rules:

1. **No expensive analysis on the real-time audio thread.** File I/O, FFT extraction and optimization stay outside the callback.
2. **The matcher renders the same synth engine used for playback.** There is no simplified proxy synthesizer that can drift away from the actual plug-in sound.
3. **Matching produces editable synthesis.** Reference audio can assist reconstruction, but the goal is not a hidden sampler.
4. **Discrete topology and continuous parameter search are treated differently.** The search is derivative-free and can explore oscillator/FM/filter choices.
5. **Session reproducibility matters.** Extracted reference synthesis data is embedded in state rather than depending on external files.
6. **Similarity is perceptual and temporal, not only one static FFT snapshot.**

More implementation detail is available in [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

---

## Limitations of the first version

RetroMatch 1.0 is a complete first source milestone, but it is not expected to perfectly reverse-engineer every commercial preset or processed recording.

Current limitations include:

- the matcher is feature/evolution based rather than a learned perceptual embedding model
- only four modulation-matrix slots are currently exposed
- no MSEG or full modulation graph yet
- no MPE/poly-aftertouch routing yet
- no arbitrary user wavetable import beyond reference extraction
- no granular or physical-model/resonator engine yet
- no convolution reverb/IR matching
- optimizer execution is CPU-oriented and not GPU accelerated
- release signing/notarization and installers are not yet part of v1.0

A heavily processed sample, chord, drum loop or sound containing multiple simultaneous notes may not map cleanly to a single-note synthesizer topology. Short, isolated notes with a clear fundamental generally provide a better reconstruction target.

---

## Roadmap

Post-1.0 candidates include:

- multistage MSEG/modulation graph
- oversampling/quality modes
- arbitrary user wavetable imports
- MPE and poly-aftertouch
- convolution/IR effect matching
- granular/resonator synthesis engines
- optional neural perceptual embedding scorer
- parallel/GPU-assisted optimizer acceleration
- preset browser, tags and searchable patch library
- signed/notarized installers and release packaging

See [`docs/ROADMAP.md`](docs/ROADMAP.md).

---

## Reference audio and rights

RetroMatch is a synthesis/reconstruction tool. Users are responsible for having the rights or permission required for any reference audio they analyze or distribute. Matching a timbre does not automatically grant rights to redistribute copyrighted recordings or other protected audio material.

---

## JUCE licensing

This repository uses JUCE as a build dependency. JUCE's licensing terms may depend on how a plug-in is distributed or monetized. Review the current JUCE licence before publishing commercial binaries.

No third-party JUCE binary or VST3 SDK is committed into this repository; the build can use an existing local JUCE checkout or fetch the configured JUCE version.

---

## Status

**RetroMatch Synth 1.0.0 is the first complete source release.**

It contains the full reference-analysis → synthesis → render → compare → optimize workflow, a broad hybrid synth architecture, editable A/B/C alternatives, persistent reference-derived wavetable data, presets, WAV preview export, automated smoke tests and native Windows/macOS build automation.

The next development phase can therefore focus on improving match quality, synthesis breadth and release polish rather than filling missing core architecture.
