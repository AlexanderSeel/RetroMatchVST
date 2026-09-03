# Architecture — v1.0

## Processing domains

RetroMatch keeps realtime synthesis separate from expensive reference analysis and optimization.

### Realtime audio thread
`PluginProcessor → SynthEngine → HybridVoice → global FX`

The callback performs parameter reads, MIDI handling and DSP only. File I/O, FFT feature extraction and parameter search are outside the audio callback.

### Offline matching thread
`Reference → SampleAnalyzer → seed → OfflineRenderer → SampleAnalyzer → SimilarityScorer → population optimizer → patch`

`OfflineRenderer` uses the same `SynthEngine` as the live plug-in, so optimization is performed against the actual synthesis implementation.

## Feature vector

Global descriptors:
- fundamental frequency and confidence;
- RMS/peak/transient and ADSR-like envelope estimates;
- zero-crossing rate, harmonicity and **inharmonicity**;
- centroid, 85% rolloff, bandwidth and flatness;
- low/high energy and odd/even harmonic balance;
- 32 logarithmic spectral bands;
- stereo width;
- 256-point waveform preview.

Temporal/timbre descriptors:
- 8 temporal frames × 16 logarithmic spectrum bands;
- normalized RMS per temporal frame;
- spectral-motion measurement;
- 12 DCT/cepstral timbre coefficients.

`SampleAnalyzer::analyzeBuffer` accepts mono or multichannel audio. Spectral/pitch analysis uses a mono mix, while stereo width is measured from the original first two channels. This same path is now used for both reference and generated candidate audio.

## Similarity scoring

The weighted score combines:
- global spectrum — 25%;
- temporal spectrum/envelope energy — 18%;
- cepstral timbre — 12%;
- brightness/bandwidth/flatness — 14%;
- ADSR/transient envelope — 14%;
- harmonic/inharmonic character — 10%;
- pitch — 4%;
- stereo width — 3%.

Raw loudness is intentionally not a dominant term.

## Optimizer

The derivative-free elite population:
1. retains the feature-derived seed;
2. evaluates discrete oscillator/filter/FM topology trials;
3. ranks a bounded elite set by perceptual score;
4. biases parent selection toward better elites;
5. applies coarse-to-fine continuous mutation;
6. occasionally crosses over parameter groups;
7. increases exploration after stagnation;
8. enforces Match Locks after topology changes, mutation and crossover;
9. always keeps the seed/best candidate so refinement cannot report a regression.

In v1.0 the searchable topology includes factory and reference-derived wavetable mix/position/warp, supersaw/unison, wavefolding and FM operator fixed/ratio modes plus their time-varying envelopes. The matcher can retain A/B/C alternatives with different synthesis biases and interpolate between their parameter states.

## Synth engine in v1.0

Per voice:
- OSC1/OSC2 sine, BLEP saw, BLEP square, triangle and variable pulse;
- sub oscillator, white noise and ring modulation;
- 12-partial additive layer;
- simple phase-modulation path;
- five-frame morphing wavetable bank with phase warp;
- 7-voice supersaw/unison with stereo spread;
- wavefolder;
- six-operator FM with six generic algorithms and feedback;
- per FM operator: ratio or fixed Hz, level, ADSR, key scaling and velocity sensitivity;
- multimode state-variable filter;
- amp ADSR, sine LFO and 4 modulation slots.

Mod sources:
- LFO 1;
- velocity;
- key tracking;
- per-note random;
- amp envelope.

Mod destinations:
- pitch;
- cutoff;
- amplitude;
- pulse width;
- phase-modulation amount;
- six-op FM mix;
- wavetable position;
- wavefold amount.

Global FX:
- nonlinear drive;
- stereo chorus;
- fractional feedback delay;
- algorithmic reverb;
- stereo width;
- output gain.

## v1.0 state and next architecture direction

Reference-derived wavetable/cycle extraction and multiple retained A/B/C candidate solutions are implemented in v1.0. Highest-value post-1.0 extensions are more explicit synthesis-engine classification, onset/DTW alignment, larger modulation/additive engines, oversampling quality modes, MPE and optional learned perceptual embeddings. Granular and resonator/physical models should remain separate engines that can be selected/layered rather than forcing every target into one topology.
