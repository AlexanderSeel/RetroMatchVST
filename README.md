# RetroMatch Synth

<p align="center">
  <img src="Assets/retromatch-mark.svg" width="92" alt="RetroMatch logo" />
</p>

<p align="center"><strong>Sample-to-synth reconstruction as an editable VST3/AU instrument.</strong></p>

RetroMatch is a C++20/JUCE hybrid synthesizer that analyzes reference audio and reconstructs it as a **real editable synthesis patch**. It combines virtual analog, wavetable, reference-derived wavetable, additive, unison, phase modulation, six-operator FM, modulation, filtering and effects with a closed-loop renderer/analyzer/matcher.

The result is not a hidden sampler preset: oscillator topology, FM operators, envelopes, filters, modulation, layers, effects and the extracted wavetable material remain available for editing, automation and further sound design.

<p align="center">
  <img src="Assets/screenshots/retromatch-hardware-overview.svg" width="100%" alt="RetroMatch RM-01 hardware-style synthesizer interface" />
</p>

> **Formats:** VST3, Audio Unit (macOS), Standalone  
> **Framework:** JUCE 9.0.1 / C++20 / CMake  
> **Platforms:** Windows 10/11 and macOS  
> **Development plan:** [`plan.md`](plan.md)

## Implementation status

The current source baseline includes the Phase A/B matching and Compare safety contracts, the validated Patch Map foundation, the sequencer/arpeggiator engine and UI, factory sequence demonstrations, XML pattern files, validated `.rmpack` export/import including bulk user-library operations, a legacy preset migration fixture, and the complete bounded Magic preview/commit workflow. Remaining roadmap entries are explicit closure gates: native release validation, generalized arbitrary-processor graph execution, final GOLD/listening review, larger reviewed factory-content work, and final loudness/content review.

### Validation snapshot

The repository-side checks currently pass: `python scripts/static-check.py`, `git diff --check`, and PowerShell parsing of the Docker/build helpers. The static checker also guards the sequencer macro/pattern plumbing, validated pack workflow, Magic preview/diff integration and optional CTest registration of the editor/session migration and transition-soak fixture. Native CMake/compiler validation is pending on this workstation: no host CMake/MSVC/clang-cl toolchain is installed. The cached `retromatch-source-windows:1.0.0` image is present, but Docker Windows container startup still fails at HCS (`0xc0370106`) before CMake runs, with no reusable container available to resume. These are recorded as release gates in [`plan.md`](plan.md), not reported as passing builds.

---

## What makes RetroMatch different

A normal spectrum analyzer tells you what is present; RetroMatch attempts to turn those measurements into a reusable instrument patch.

```text
Reference audio
      ↓
Region selection / cleanup
      ↓
Pitch + spectral + temporal + timbre analysis
      ↓
Feature-derived seed + reference wavetable extraction
      ↓
Offline synth rendering
      ↓
Perceptual comparison
      ↓
Evolution / topology-biased refinement
      ↓
Editable synth + layered patch
```

The matcher renders the **same SynthEngine used for live playback**, analyzes that render with the same feature path used for the source, scores the difference, and searches for a better candidate.

---

## Current highlights

### Reference editor for short sounds and full tracks

The reference workflow supports more than a tiny one-shot waveform. A large reference editor is available for:

- zoom and pan
- draggable start/end range
- move the selected region
- preview the selection
- normalize
- fade in / fade out
- cut the selection into a new reference
- choose a focused region for resynthesis
- choose a separate region for MIDI analysis

Long recordings can therefore be treated as source material rather than forcing the matcher to analyze an entire song as one timbre.

### Professional resynthesis modes

The current engine exposes strategy and complexity controls for deeper matching:

- **Balanced Hybrid**
- **Reference Wavetable**
- **Spectral Subtractive**
- **FM / Harmonic**
- **Layered Studio**
- **Texture / Chop**

Complexity can scale from the legacy compact patch to **4, 6 or 8 synthesis instances**, allowing complementary roles such as body, sub/foundation, air, motion, harmonic colour, width and texture instead of simply duplicating one sound.

### Reference-derived wavetable synthesis

RetroMatch extracts phase-stabilized periodic material from the selected reference region into morphable 2048-sample frames. The matcher can deliberately favor that source when it improves the match, and Texture/Chop-style workflows can derive more animated timbral material from the selected audio.

### Visual comparison

The large compare view can inspect reference vs resynthesis across waveform/envelope and spectral fingerprints, with score dimensions for spectrum, timbre, temporal behavior, harmonics, envelope, pitch and stereo image.

### Master gain staging

A dedicated **MASTER OUTPUT** control sits after the complete synth/reference mix so patches with very different internal gain structures can be auditioned and mixed consistently without destroying the patch's own output-gain design.

### Sequencer / arpeggiator

The editor has a dedicated **SEQUENCER** tab for building complete melodic presets. It supports 64 steps, pattern and held-note arpeggiator modes, internal or DAW-synced clocking, swing, ratchets, ties, note probability, modulation probability and per-step micro-timing. Two macro lanes can target cutoff, resonance, pitch, amplitude or wavetable position using Hold, Linear, Smooth or bounded Random interpolation with independent lane rates.

Sequencer state is included in `.rmsynth` plug-in state. The tab includes twelve reusable musical starting patterns, and patterns can also be saved and loaded as XML files. The Sequence factory presets provide playable examples with enabled melodic motion and multiple macro destinations.

The Presets page also exports and imports self-contained `.rmpack` archives. A pack contains a validated manifest and the `.rmsynth` asset, so sequencer patterns and embedded wavetable data travel with the sound. Imports persist validated patches into the user library and offer Keep, Replace or Import as Copy handling for collisions.

The library supports exporting all user patches to one pack and importing folders of `.rmsynth` files with imported/skipped/failed reporting. Factory entries remain read-only catalog entries; user edits are saved in the user preset folder.

Preset-page audition uses a short bounded offline RMS/peak estimate to compensate the trigger level while browsing. This is transient audition state and does not rewrite the patch's saved output gain.

Factory presets carry searchable category, subcategory, tags, author, recommended octave range and macro-label metadata. The factory-library safety test renders the catalog across representative notes and rejects invalid or severely clipped patches.

Magic variations preserve the authored Patch Map topology while applying bounded semantic mutations. Variants are live, non-destructive previews: KEEP commits while retaining the origin trail, APPLY commits and establishes a new origin, and RESTORE/BRANCH can discard a preview. Origin capture, restore and a bounded branch history are persisted with both plug-in state and presets, including each branch's routing topology. The Magic status line reports changed dimensions, origin distance and rendered RMS/peak safety.

---

## Signal Lab and patch map

Signal Lab combines live output scope, spectrum, stereo field and a whole-synth patch map.

<p align="center">
  <img src="Assets/screenshots/retromatch-patch-map.svg" width="100%" alt="RetroMatch Signal Lab interactive patch map" />
</p>

The patch map is being expanded into a safe semi-modular playground. The current canvas foundation supports node navigation, pan/zoom, node dragging, snap-to-grid, auto arrange and fit/zoom controls. The next implementation stages add typed ports, editable cables and a validated DSP routing compiler so changing a connection changes the actual sound.

See [`plan.md`](plan.md) for the implementation order and safety/compatibility rules.

---

## Hybrid synthesis engine

RetroMatch deliberately uses several synthesis methods because a single architecture is not enough to reconstruct a broad set of sounds.

- two virtual-analog oscillators: sine, triangle, BLEP saw, square/pulse
- variable pulse width, sub oscillator, noise and ring modulation
- 12-partial additive layer
- factory wavetable engine with continuous frame interpolation and warp
- reference-derived wavetable bank
- seven-voice supersaw/unison with stereo spread
- phase modulation
- six-operator FM with multiple algorithms, ratio/fixed modes, per-operator ADSR, key scale and velocity
- wavefolder / nonlinear harmonic shaping
- multimode state-variable filter
- polyphonic amp ADSR
- LFO, MSEG and modulation graph
- modular + built-in effects
- multi-instance layered synthesis

---

## Main workflow

1. **Load Reference** — WAV, AIFF or FLAC, or drag a file onto the plug-in.
2. Open the large reference editor when the source needs a precise region or cleanup.
3. **Quick x3** builds three initial reconstruction candidates.
4. **Refine x3** runs closed-loop render/analyze/score optimization.
5. **AI x3** can request structured seed ideas from the configured provider and still score them locally.
6. Compare/select **A / B / C** or morph between the alternatives.
7. Edit synthesis, layers, modulation, FX and wavetable controls manually.
8. Save a self-contained `.rmsynth` patch or export audio/MIDI.

Reference-derived wavetable data is embedded in patch/session state so the original source file is not required for normal synth playback after extraction.

---

## Melody / MIDI extraction

Reference audio can also be analyzed into editable timed notes. Long-track transcription is chunked instead of silently stopping at the original short-analysis limit.

- predominant-melody mode
- experimental layered-note mode
- interactive piano roll
- replay through the current RetroMatch patch
- standard MIDI export with tempo metadata
- separate selection for MIDI vs timbre resynthesis

Mixed/mastered material is inherently ambiguous: drums, vocals, overlapping harmonics and effects can create missed or additional notes. MIDI extraction is therefore an editable estimate, not source separation.

---

## Closed-loop matching

The deeper matcher uses derivative-free population search because the patch contains both continuous values and discrete topology decisions.

It evaluates combinations of:

- global spectral shape
- temporal spectrum / RMS contour
- cepstral timbre descriptors
- brightness, bandwidth and flatness
- envelope and transient behavior
- harmonic / inharmonic character
- pitch
- stereo image

The optimizer retains strong candidates, mutates coarse-to-fine, crosses over parameter groups and respects Match Locks so already-convincing parts of a patch can be frozen while other groups continue evolving.

---

## Hardware-style UI

The custom JUCE interface follows an original RM-01 hardware language: dark machined metal, walnut cheeks, recessed panels/displays, illuminated controls and dense but direct instrument ergonomics. It is inspired by the workflow of professional hardware synthesizers without copying a specific manufacturer's protected panel design.

The interface includes dedicated pages for:

- Layers / Presets
- Synth
- FM
- Filter + Amp
- Modulation / MSEG
- FX
- Wavetable
- Settings / AI Log
- Signal Lab
- Melody / MIDI
- Sequencer / Arpeggiator

---

## Building

Full setup and CI details are in [`docs/BUILD.md`](docs/BUILD.md).

### Windows

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\setup-windows.ps1
# reopen PowerShell if setup changed the toolchain
.\scripts\check-tools.ps1
.\scripts\build-windows.ps1 -RunTests
```

Expected artifacts:

```text
build-windows/RetroMatchSynth_artefacts/Release/VST3/RetroMatch Synth.vst3
build-windows/RetroMatchSynth_artefacts/Release/Standalone/RetroMatch Synth.exe
```

### macOS

```bash
chmod +x scripts/build-macos.sh
RUN_TESTS=1 ./scripts/build-macos.sh
```

Expected artifacts:

```text
build-macos/RetroMatchSynth_artefacts/Release/VST3/RetroMatch Synth.vst3
build-macos/RetroMatchSynth_artefacts/Release/AU/RetroMatch Synth.component
build-macos/RetroMatchSynth_artefacts/Release/Standalone/RetroMatch Synth.app
```

For an existing JUCE checkout, pass `-JuceDir C:\dev\JUCE` on Windows or configure with `-DRETROMATCH_JUCE_DIR=/path/to/JUCE`.

---

## Verification

Run the dependency-free source contract first:

```bash
python scripts/static-check.py
```

Then build with `RETROMATCH_BUILD_TESTS=ON` / the platform scripts and run the DSP smoke tests. Release validation should also include pluginval, at least two VST3 hosts, DAW automation/session recall and `auval` on macOS.

Relevant documents:

- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)
- [`docs/BUILD.md`](docs/BUILD.md)
- [`docs/VERIFICATION.md`](docs/VERIFICATION.md)
- [`docs/ROADMAP.md`](docs/ROADMAP.md)
- [`plan.md`](plan.md) — active implementation plan

---

## Core design principles

1. **No expensive work on the real-time audio thread.**
2. **The matcher renders the same synth engine used for playback.**
3. **Matching produces editable synthesis, not a hidden sampler.**
4. **Discrete topology and continuous parameter search are treated differently.**
5. **Reference synthesis data required for playback is embedded in state.**
6. **Similarity is temporal and perceptual, not just one static FFT snapshot.**
7. **New automation parameters are append-only for host/session compatibility.**
8. **The semi-modular graph must reject unsafe or meaningless routing before it reaches DSP.**

---

## Project layout

```text
RetroMatchVST/
├─ Assets/
│  └─ screenshots/
├─ Source/
│  ├─ Analysis/
│  ├─ Engine/
│  ├─ Matching/
│  ├─ Reference/
│  ├─ UI/
│  └─ PluginProcessor.*
├─ Tests/
├─ docs/
├─ scripts/
├─ plan.md
└─ CMakeLists.txt
```

RetroMatch is an evolving source project. The active roadmap favors **better resynthesis, better reference handling, richer sound-design presets and a safe semi-modular patch map** while preserving real-time stability and session compatibility.
