---
name: synth-ui-designer
description: Design and review professional JUCE synthesizer interfaces with hardware-inspired clarity, responsive layout, parameter ergonomics, visual matching workflows, and host-safe lifecycle behavior.
version: 1.1.0
authors:
  - Alexander Seel
---

# Synth UI Designer

Use this skill whenever designing, reviewing, or refactoring a synthesizer, resynthesizer, sampler, or audio plug-in UI.

## Core principles

1. **Workflow before density**: organize controls around the musician's task, not implementation classes. Reference/match actions stay visible; deep synthesis controls live in focused tabs.
2. **Keep cause and result together**: if a user loads a reference to create a matched sound, the reference visualization, matching actions, progress, generated variants and variant selection belong in one persistent workspace. Never hide the result on a distant tab.
3. **Variants over one-shot answers**: perceptual sound matching is subjective. Quick Match, Refine Match and AI-assisted matching should produce meaningfully different A/B/C candidates rather than silently replacing the current patch with a single answer.
4. **Make the algorithm legible**: show a compact visual pipeline such as Analyze -> Seed -> Render -> Score -> Variants. During matching, indicate the active stage and progress. Show reference/resynth overlays and per-candidate similarity dimensions where useful.
5. **One visual hierarchy**: product/header, persistent workflow area, functional tabs, section headers, controls, values. Do not place every parameter on one canvas.
6. **Hardware-readable controls**: rotary controls must be truly circular at every aspect ratio, have an obvious position indicator, a readable parameter name, and a compact human-formatted value.
7. **Functional grouping**: prefer Synth, FM, Filter/Amp, Mod and FX editing pages. Keep matching persistent rather than making it just another synthesis tab. Provider/backend configuration belongs in Settings.
8. **Responsive JUCE layout**: layout must derive from current component bounds. Never depend on one fixed editor size. Use responsive grids/reflow and sensible min/max editor dimensions.
9. **No raw engineering values in the UI**: avoid values such as `11999.9990` or `0.3500000`. Use appropriate precision and units (`12.0 kHz`, `350 ms`, `18.0 ct`, `0.35`).
10. **Progressive disclosure**: advanced FM/operator/modulation/provider controls should not compete visually with the basic signal path.
11. **Persistent feedback**: analysis status, match progress, selected variant, reference state, and errors must remain visible without requiring a tab switch.
12. **Audition in context**: instruments should expose a compact virtual keyboard or equivalent manual trigger when it materially reduces host round-trips. It must be hideable and must release all notes when hidden or destroyed.
13. **Hardware state cues**: important actions and toggle states should use clear LEDs, meters, badges or graphical state indicators in addition to text and border colour. Decorative lighting must never obscure state.
14. **Host-safe lifecycle**: constructors must finish creating controls before any operation that can trigger `resized()`, repaint callbacks, async callbacks, or parameter attachment callbacks. Detach custom LookAndFeel objects and stop background work before destruction.
15. **No network activity on the audio thread**: AI/provider requests, model calls, downloads and expensive matching work must run outside `processBlock`. Apply results back on the message thread or through an explicit thread-safe handoff.
16. **AI proposes, local DSP verifies**: AI-assisted sound design should generate parameter seeds or high-level suggestions. The plug-in must render and score candidates locally before presenting them as matches.
17. **Protect secrets and user audio**: API keys should come from environment/session-only inputs unless secure credential storage is explicitly implemented. Do not upload reference audio when numerical feature summaries are sufficient; disclose exactly what leaves the machine.

## Recommended RetroMatch architecture

- **Header**: product identity, patch save/load, export, keyboard visibility.
- **Persistent reference + resynth workspace**:
  - drop/load reference;
  - waveform + spectrum with resynth overlay;
  - Analyze -> Seed -> Render -> Score -> Variants process indicator;
  - Quick x3, Refine x3, AI x3;
  - A/B/C candidate cards with overall and dimensional scores;
  - candidate morph;
  - progress and status.
- **Right tabbed editor**:
  - **SYNTH**: oscillators, pitch, wavetable, unison, wavefold, harmonic shaping.
  - **FM**: PM/FM core, six-operator overview, selected-operator detail.
  - **FILTER + AMP**: filter mode/cutoff/resonance, ADSR, drive/output.
  - **MOD**: LFO and modulation matrix.
  - **FX**: chorus, delay, reverb, stereo/output processing.
  - **SETTINGS**: AI provider/model/endpoint configuration, resynthesis backend information, privacy/evaluation details.
- **Optional bottom keyboard**: hideable manual audition surface that sends notes into the same synth engine used by host MIDI.

## Candidate design guidance

- A/B/C must intentionally explore different synthesis families, not three near-identical random mutations.
- A useful default split is:
  - **A Natural/Spectral**: prioritise reference wavetable, additive/harmonic structure and conservative topology.
  - **B FM/Harmonic**: prioritise FM/PM topology and harmonic/inharmonic relationships.
  - **C Wavetable/Texture**: prioritise wavetable, supersaw/unison, motion and wavefolding.
- Auto-select the objectively best local score, but never hide the other two choices.
- Candidate cards should expose enough of the score breakdown to explain why variants differ.

## Acceptance checklist

- Editor opens and closes repeatedly in a VST3 host without crashing.
- Minimum editor size is usable with no overlap or clipped primary controls.
- Increasing width/height produces useful reflow rather than empty space or stretched controls.
- Rotary knobs remain circular.
- No parameter label collides with its value box.
- No raw excessive-precision values are visible.
- Reference, matching controls, progress and A/B/C choices are visible together.
- Quick and Refine each generate three distinct candidates.
- AI-assisted matching never performs network work on the audio thread and every AI candidate is locally rendered/scored.
- Reference and candidate waveform/spectrum information can be compared visually.
- The virtual keyboard can be hidden and cannot leave stuck notes.
- Important toggle/action state has a clear graphical/LED cue.
- Every parameter appears on exactly one logical editing page unless intentionally duplicated as a global control.
- Tab names and section names communicate intent in musician terminology.
- Changes do not rename APVTS parameter IDs without an explicit preset-migration plan.
