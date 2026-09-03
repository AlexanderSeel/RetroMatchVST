# RetroMatch Synth UI Redesign

## Goal

Replace the original single-canvas editor with a musician-oriented, responsive interface while preserving all existing DSP behavior and APVTS parameter IDs.

## Layout

- **Header**: product identity, Save Patch, Load Patch.
- **Persistent reference workflow**: reference analyzer, Load Sample, Quick Match, Refine Match, Export WAV, progress and status.
- **Tabbed synthesis editor**:
  - Synth
  - FM
  - Filter + Amp
  - Mod
  - FX
  - Match

## Responsive behavior

The editor targets a default size around 1400x860 with a usable minimum around 1080x720. The persistent reference panel receives a bounded fraction of available width; the tab editor takes the remaining space. Parameter controls reflow into a dynamic number of columns based on available width.

## Visual system

- dark low-glare background
- warm amber/gold primary interaction accent
- teal analyzer/reference accent
- circular hardware-inspired rotary controls
- consistent label -> control -> formatted value hierarchy
- section headers instead of heavy panel borders around every control

## Parameter organization

### Synth
Oscillator mix, sub/noise/ring/additive, tuning, pulse width, wavetable/reference wavetable, supersaw/unison, wavefold and harmonic shaping.

### FM
Legacy PM controls, six-operator mix/feedback/algorithm, operator ratio/level overview and selected-operator detail.

### Filter + Amp
Filter mode, cutoff/resonance, ADSR, drive and output gain.

### Mod
LFO parameters and four modulation-matrix slots.

### FX
Chorus, delay, reverb and stereo width.

### Match
A/B/C candidate generation/selection, morphing and Match Locks.

## Compatibility constraints

- Do not rename APVTS parameter IDs without preset migration.
- Do not change DSP defaults as part of UI work.
- Keep asynchronous matching work off the message thread.
- Finish constructing dynamic controls before any `setSize()` call that can invoke `resized()`.
- Detach custom LookAndFeel before editor member destruction.
