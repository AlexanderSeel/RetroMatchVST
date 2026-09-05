# Roadmap

## v1.0 — COMPLETE SOURCE MILESTONE
Hybrid synth, reference analysis, closed-loop matcher, FM, wavetable/unison, reference-derived wavetable, modulation/effects, A/B/C alternatives, morphing, presets/state, preview export, tests and native build automation.

## Post-1.0 implemented
- 1x/2x/4x nonlinear oversampling quality modes with fixed latency compensation
- six-point per-voice MSEG with note-held loop and curved/timed segments
- append-only four-slot modulation graph with MSEG routing while preserving the original v1.0 MOD choice ranges
- dedicated MSEG/graph editor and post-1.0 state migration defaults
- user-imported arbitrary single-cycle and multi-frame wavetable sets mapped to an embedded five-frame x 2048 internal bank
- independent user-wavetable oscillator/mix layer that preserves the reference-derived matching wavetable
- dedicated wavetable import/preview editor with explicit common source-cycle sizes for ambiguous files

## Post-1.0 candidates
- convolution/IR effect matching
- neural perceptual embedding scorer as optional helper (not required for synthesis)
- GPU/parallel optimizer acceleration
- MPE/poly-aftertouch routing
- preset browser/tagging and patch database
- signed/notarized installers
