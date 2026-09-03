---
name: synth-ui-designer
description: Design and review professional JUCE synthesizer interfaces with hardware-inspired clarity, responsive layout, parameter ergonomics, and host-safe lifecycle behavior.
version: 1.0.0
authors:
  - Alexander Seel
---

# Synth UI Designer

Use this skill whenever designing, reviewing, or refactoring a synthesizer or audio plug-in UI.

## Core principles

1. **Workflow before density**: organize controls around the musician's task, not the implementation classes. Reference/match actions stay visible; deep synthesis controls live in focused tabs.
2. **One visual hierarchy**: product/header, persistent workflow area, functional tabs, section headers, controls, values. Do not place every parameter on one canvas.
3. **Hardware-readable controls**: rotary controls must be truly circular at every aspect ratio, have an obvious position indicator, a readable parameter name, and a compact human-formatted value.
4. **Functional grouping**: prefer Synth, FM, Filter/Amp, Mod, FX, and Match pages. Keep related selectors next to the parameters they affect.
5. **Responsive JUCE layout**: layout must derive from current component bounds. Never depend on one fixed editor size. Use responsive grids/reflow and sensible min/max editor dimensions.
6. **No raw engineering values in the UI**: avoid values such as `11999.9990` or `0.3500000`. Use appropriate precision and units (`12.0 kHz`, `350 ms`, `18.0 ct`, `0.35`).
7. **Progressive disclosure**: advanced FM/operator/modulation controls should not compete visually with the basic signal path.
8. **Persistent feedback**: analysis status, match progress, reference state, and errors must remain visible without requiring a tab switch.
9. **Host-safe lifecycle**: constructors must finish creating controls before any operation that can trigger `resized()`, repaint callbacks, async callbacks, or parameter attachment callbacks. Detach custom LookAndFeel objects before destruction.
10. **Accessibility and interaction**: hit targets should be comfortable, text should remain legible at minimum size, selected/toggled state must not rely only on subtle colour changes, and mouse-wheel/drag behavior must be predictable.

## Recommended RetroMatch architecture

- Header: product identity plus patch save/load.
- Persistent left workflow panel: reference analyzer, load, quick match, refine, export, progress, status.
- Right tabbed editor:
  - **SYNTH**: oscillators, pitch, wavetable, unison, wavefold, harmonic shaping.
  - **FM**: PM/FM core, six operator overview, selected-operator detail.
  - **FILTER + AMP**: filter mode/cutoff/resonance, ADSR, drive/output.
  - **MOD**: LFO and modulation matrix.
  - **FX**: chorus, delay, reverb, stereo/output processing.
  - **MATCH**: A/B/C candidates, morphing, parameter locks.

## Acceptance checklist

- Editor opens and closes repeatedly in a VST3 host without crashing.
- Minimum editor size is usable with no overlap or clipped controls.
- Increasing width/height produces useful reflow rather than empty space or stretched controls.
- Rotary knobs remain circular.
- No parameter label collides with its value box.
- No raw excessive-precision values are visible.
- Every parameter appears on exactly one logical page unless intentionally duplicated as a global control.
- Reference/match workflow remains reachable regardless of selected synthesis tab.
- Tab names and section names communicate intent in musician terminology.
- Changes do not rename APVTS parameter IDs without an explicit preset-migration plan.
