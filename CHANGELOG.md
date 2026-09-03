# Changelog

## 1.0.0

- First complete product milestone.
- Added reference-derived five-frame wavetable extraction and morphing playback.
- Embedded extracted wavetable data in `.rmsynth` presets and DAW session state.
- Added Reference Wavetable Mix and optimizer search dimension.
- Added A/B/C alternative candidate generation with wavetable-, FM-, and hybrid-biased seeds.
- Added continuous candidate morphing.
- Added 24-bit WAV preview export.
- Extended smoke tests with reference-wavetable serialization round-trip.
- Updated CMake project version and static validation to 1.0.0.


## 0.4.0
- Added a five-frame morphing wavetable bank with interpolation and phase warp.
- Added seven-voice supersaw/unison with detune and stereo spread.
- Added a pre-filter wavefolder and modulation destinations for wavetable position and wavefold amount.
- Expanded every FM operator with ADSR, ratio/fixed-frequency mode, fixed-Hz frequency, key scaling and velocity sensitivity.
- Added a dedicated dynamic FM-operator detail editor to keep the large parameter surface usable.
- Added inharmonicity analysis and made it part of harmonic similarity scoring and FM seed selection.
- Made buffer analysis stereo-aware and removed candidate mono-downmix duplication, allowing stereo image to influence closed-loop matching correctly.
- Extended Quick Match, mutation, topology trials, crossover, clamping and Match Locks to the new wavetable/unison/wavefold/FM dimensions.
- Expanded the modulation matrix with wavetable-position and wavefold destinations.
- Expanded the DAW-automatable surface to 122 parameters: 104 continuous and 18 choices.
- Extended smoke tests to exercise wavetable motion, supersaw stereo, wavefolding and fixed-frequency/operator-envelope FM.
- Added a Python static source-integrity check and a CI source-check gate before native platform builds.
- Enabled C as well as C++ in the top-level CMake project for JUCE 9 compatibility.

## 0.3.0
- Added a six-operator FM engine with six generic routing algorithms, feedback, per-operator ratio/level controls and FM wet mix.
- Added four routable modulation slots with LFO, velocity, key tracking, note-random and amp-envelope sources.
- Added pitch, cutoff, amplitude, pulse-width, PM-amount and six-op-FM-mix modulation destinations.
- Added time-varying 8×16 logarithmic spectral analysis and spectral-motion measurement.
- Added a 12-coefficient cepstral timbre descriptor.
- Expanded perceptual scoring to global spectrum + temporal spectrum + cepstral timbre + envelope/harmonic/pitch/stereo terms.
- Replaced single-best stochastic refinement with a small elite population, topology trials and parameter-group crossover.
- Added Match Locks for pitch, oscillators, FM, envelope, filter, modulation and effects.
- Added 6-op FM and modulation-matrix controls to the hardware-inspired UI.
- Added DSP/matcher CTest smoke tests and optional local test execution in platform build scripts.
- Updated GitHub Actions to current Node-24-compatible action majors and enabled smoke tests in both native build jobs.
- Increased the minimum editor size so the larger v0.3 control surface cannot overlap at the smallest supported size.

## 0.2.0
- Added real closed-loop sample-to-synth refinement.
- Added OfflineRenderer and SimilarityScorer.
- Expanded reference analysis with 32 log spectral bands, spectral flatness/bandwidth, pitch confidence, odd/even harmonic character, improved envelope analysis, stereo width and waveform preview.
- Added anti-aliased BLEP saw/square/pulse oscillator generation.
- Added sub oscillator, ring modulation, a 12-partial additive synthesis layer, variable pulse width and master tune.
- Added drive, chorus, delay, reverb, width and output-gain DSP.
- Added asynchronous Refine Match workflow with progress/cancellation plumbing.
- Added reference/resynth analyzer overlay.
- Added `.rmsynth` preset save/load.
- Added local-JUCE CMake option and platform build/package scripts.
- Corrected AU-compatible four-character manufacturer/plugin identifiers.

## 0.1.0
- Initial reference analysis, 2-oscillator synth, FM/noise/SVF/ADSR/LFO, Quick Match and retro UI.
