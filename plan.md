# RetroMatch implementation plan

> **Product rule:** **Match is truth; Magic is transformation.** Matching must reproduce the loaded reference as faithfully and safely as possible. Creative transformation is explicit and must never be hidden inside Match or Compare.

This plan is ordered by implementation dependency and release risk. Duplicate requirements from the previous plan have been merged into one owner phase so each item has a single acceptance gate.

## Status legend

- [x] implemented and present on `main`
- [~] partially implemented / requires closure
- [ ] planned

---

## 0. Non-negotiable engineering guardrails

- [x] Keep released APVTS parameter IDs/order compatible; new host parameters are append-only.
- [x] Keep file I/O, analysis, graph compilation and candidate rendering off the real-time audio callback.
- [x] Use the same synth engine/state model for live playback and offline matching wherever possible.
- [x] Version and validate serialized graph/patch state; malformed optional state must degrade safely.
- [x] Preserve immutable match baselines for A/B, Compare and candidate history.
- [ ] Any future feedback-capable routing must own delay/state explicitly, validate cycles and bound gain.
- [ ] Generated content must pass finite-audio, clipping/headroom and compatibility checks before it is treated as release-ready.

### Global release gate

A phase that changes DSP is complete only when its focused regressions pass and the native Windows/macOS plug-in matrix is green. Do not hide a core matching regression behind new UI/content features.

---

# Phase A — Core match integrity and recovery **(P0)**

This is the current priority. The reference sample is the sonic contract; a numerically improved candidate must not win by becoming louder, harsher, unstable or lifecycle-inaccurate.

## A1. Deterministic reference corpus and observability

- [ ] Build a small checked-in deterministic corpus covering kick, snare/percussion, short pluck, bass, piano/keys, mallet/bell, sustained lead, pad, staccato/string-like material, noisy texture and evolving atmosphere.
- [ ] Store expected coarse identity facts per fixture: pitch confidence/range, lifecycle class, transient character, envelope duration, spectral balance, stereo character and whether nonlinear colour is expected.
- [ ] Add candidate telemetry suitable for tests/debugging: peak, RMS, crest factor, DC, clipped/non-finite sample counts and, where useful, oversampled/true peak.
- [ ] Add optional offline/debug stage snapshots for layer output, combine, pre-FX, post-FX, global bus and final output. No callback allocations.
- [ ] Keep similarity level-normalized where the metric requires it; playback gain remains a separate musical/safety concern.

## A2. Lifecycle and evidence constraints

- [x] Detect self-terminating references and enforce zero sustain through main voice, FM operators, embedded layers/GOLD/AI companions and amplitude MSEG loop state.
- [x] Render held notes beyond the audible reference when evaluating one-shots and penalize non-silent generated tails.
- [x] Prevent clean references from gaining wavefold/saturation/nonlinear FX solely to improve aggregate similarity.
- [x] Apply nonlinear evidence constraints consistently to normal matches, one-shots, shared matcher paths, GOLD companions and post-sum/global processing.
- [x] FX-probe dynamics compares reference and candidate in the same peak/RMS crest-factor domain rather than mixing transient/sustain metadata into the target.
- [x] `SoundMatcher::initialFit()` lifecycle policy is applied after seed generation. Initial-fit `candidateFeatures`/`similarity` are intentionally unmeasured; `confidence` is only a seed-analysis heuristic until a rendered evaluation/refinement exists.

## A3. Gain staging, distortion and technical safety

- [ ] Audit/document unity-gain expectations from oscillator -> layer -> layer combine -> per-instance FX -> global bus -> output.
- [ ] Prove that adding companion layers or parallel branches cannot accidentally multiply level.
- [ ] Add a clean-path invariant: a clean sine/reference stays finite, unclipped and free of unintended nonlinear processing through a complete generated rack.
- [ ] Add bounded headroom/normalization where legitimate generated multilayer sums need it; do not use a limiter to conceal upstream gain bugs.
- [ ] Penalize hard clipping, non-finite output, suspicious crest collapse and excessive alias-like high-frequency energy in candidate selection.
- [ ] Add focused regressions for serial/parallel FX level, multilayer sums, GOLD racks, AI variants and Compare-adjusted racks.
- [ ] Prove materially equivalent offline/live topology, gain and FX behavior for the same patch state.

## A4. Perceptual identity analysis

- [ ] Formalize identity dimensions: pitch/harmonicity, transient, amplitude lifecycle, spectral envelope/resonances, noise/tonal ratio, temporal spectral motion, stereo/width and tail/reverb character.
- [ ] Use explicit lifecycle classes: one-shot, plucked/decaying, gated, sustained and evolving.
- [ ] Improve harmonic-series/pitch validation so inharmonic percussion and mallets are not forced into an inappropriate pitched model.
- [ ] Analyze attack, early body, middle body and tail rather than relying on one representative frame.
- [ ] Preserve high-confidence reference dimensions as bounded constraints during refinement.
- [ ] Rank synthesis explanations from evidence: subtractive/spectral, reference wavetable, FM/harmonic, layered hybrid, noise/percussion, texture/chop and FX/guitar-chain where supported.
- [ ] Score identity and technical safety separately enough that unsafe distortion cannot win on aggregate similarity.

## A5. Phase-A acceptance

- [ ] Deterministic clean fixtures render finite with no hard clipping or unintended distortion.
- [x] Self-terminating references remain self-terminating under a held MIDI note.
- [ ] Sustained references remain sustained on the deterministic corpus.
- [ ] Quick/Refine/Gold do not systematically increase harshness or level to raise score.
- [ ] Offline and live renders are materially equivalent for the same state.
- [ ] Windows VST3/Standalone + DSP regressions pass.
- [ ] macOS AU/VST3/Standalone + validation pass.
- [ ] Listening pass on the fixture corpus confirms the automated gate.

---

# Phase B — Compare fine-tune closure **(P1 after A is stable)**

Compare is a semantic residual-correction surface, not a second synthesizer. Every knob is bipolar, center-neutral and operates from an immutable measured baseline.

## B1. Implemented correction contract

- [x] **Brightness**: logarithmic filter/spectral tilt only; no hidden drive/wavefold.
- [x] **Low End**: bounded main-voice sub balance; do not multiply sub sources across GOLD companions.
- [x] **Punch**: amp/FM attack speed, not generic loudness/drive.
- [x] **Tail**: decay/release time; never reintroduce sustain into one-shots.
- [x] **Width**: stereo width/rack pan spread; do not alter unison detune timbre.
- [x] **Motion**: scale existing modulation topology only; static patches stay static.
- [x] **Fine Pitch**: whole-instrument residual tuning, currently +/-50 cents.
- [x] Full-rack corrections clone embedded layers and keep candidate baselines immutable.
- [x] Compare macros do not alter nonlinear colour/FX state.
- [x] AUTO NUDGE derives bounded semantic residual directions and accepts a result only after measured improvement.

## B2. Implemented UX/measurement path

- [x] Reference/Synth/Mix audition plus Baseline/Adjusted comparison.
- [x] Debounced background re-render/measurement; no expensive work in `processBlock`.
- [x] Reference/Baseline/Adjusted traces and measured before->after similarity deltas.
- [x] Pending state remains visibly unmeasured; displayed similarity is never cosmetically inflated.
- [x] Candidate A/B/C keeps independent correction/measured state.
- [x] KEEP/APPLY commits the adjusted patch; temporary Compare state is otherwise not serialized.

## B3. Remaining verification

- [~] Directional tests exist for parameter semantics and rendered Brightness; extend deterministic rendered-feature coverage to Low End, Punch, Tail, Width, Motion and Fine Pitch where the analyzer exposes a robust metric.
- [ ] Add an explicit reset-after-arbitrary-edits regression proving the immutable baseline is restored parameter-equivalently.
- [ ] Add approximate level-invariance assertions for controls that should not act as loudness controls, especially Width/Brightness.
- [ ] Add concise UI help/tooltips documenting each control's actual dimension/unit.

### Compare acceptance

All seven controls move only their intended perceptual dimension within tolerance, RESET restores the candidate baseline, background measurement remains truthful, and one-shot/nonlinear invariants survive arbitrary Compare edits.

---

# Phase C — Semi-modular Patch Map completion **(P1/P2)**

The existing Patch Map foundation is usable; finish a small set of deterministic routing primitives instead of turning RetroMatch into an unrestricted modular host.

## C1. Foundation already landed

- [x] Zoom-at-pointer, pan, draggable nodes, optional grid/snap, fit/auto/zoom controls and scalable large view.
- [x] Explicit typed node/edge model with Audio/Modulation/Clock/Control semantics.
- [x] Port compatibility, deterministic topological ordering, cycle rejection and clear invalid-drop feedback.
- [x] DSP-backed modulation cable create/reconnect/delete and editable layer-combine ordering.
- [x] Undo/redo and graph/session serialization with schema migration/repair.
- [x] Immutable layer-combine processing plan swapped atomically at block boundaries.
- [~] Per-instance serial/parallel FX split is DSP-backed with preallocated scratch and bounded 50/50 compensation.
- [x] BIG Patch Map/window behavior follows resize and preserves navigation state.

## C2. Finish routing v2

- [ ] Reorder explicitly supported pre/post processing nodes where engine ownership is unambiguous.
- [ ] Generalize safe Split/Merge/Gain/Pan/Width utilities using preallocated processing buffers.
- [ ] Add configurable branch dry/wet/gain with deterministic compensation.
- [ ] Preserve/compute latency across routed oversampled/nonlinear nodes.
- [ ] Add branch solo/audition and lightweight meters without callback analysis/allocation.
- [ ] Add generic Insert/Merge/Disconnect/Restore actions and node context actions.
- [ ] Add multi-select/group move/node lock; minimap only if eight-instance graphs prove it useful.
- [ ] Stress-test graph edits while audio is running.

## C3. Resynthesis integration

- [ ] Treat only validated, bounded topology choices as discrete match dimensions.
- [ ] Preserve user graph locks during Refine/AI runs.
- [ ] Prefer reference-derived wavetable/topology choices only when measured identity improves.
- [ ] Keep topology search bounded to avoid combinatorial candidate explosion.

### Patch Map acceptance

A user can build supported serial/parallel paths, edit valid modulation/layer routes, hear the change, undo it, save/reload it and reproduce the same sound. Invalid/unstable graphs never reach the audio thread.

---

# Phase D — GOLD full-rack resynthesis completion **(P1/P2)**

- [~] GOLD evaluates heterogeneous synthesis strategies and embeds multilayer racks; continue validating that the score belongs to the exact full rack the user receives.
- [~] Evolve bounded rack dimensions: main/layer gain, pan, tune, role timbre and long-form MSEG motion.
- [~] Keep global/post-sum processing represented in the measured candidate and mirrored into live controls.
- [~] Use excitation probes as diagnostic/secondary evidence only; musical full-rack similarity remains the displayed truth.
- [ ] Add deterministic depth-selection tests for 3/4/6/8-instance candidates.
- [ ] Add full-rack offline/live equivalence and headroom regressions before increasing search depth.

### GOLD acceptance

Candidate A/B/C reproduces the exact rack that was scored, including layers/global processing, with truthful similarity, safe headroom and deterministic lifecycle behavior.

---

# Phase E — Preset library and pack workflow **(P2)**

## E1. Versioned pack format

- [ ] Add ZIP-compatible `.rmpack` with manifest: stable pack/patch IDs, version, author, description, tags/categories, minimum schema/plugin version and asset list.
- [ ] Reuse versioned `.rmsynth` patch data and include only required owned assets.
- [ ] Validate paths/sizes; never allow archive traversal outside the preset library.
- [ ] Handle re-import/update conflicts explicitly: Ask / Keep / Replace / Import as Copy.

## E2. Library operations

- [ ] Import/export a complete pack, export all patches, and bulk-import `.rmsynth` folders.
- [ ] Report imported/updated/skipped/failed counts.
- [ ] Keep factory presets read-only; edits save as user copies.
- [ ] Search/filter by name, category, tags, author, favorite and sound character.

## E3. Factory content target

- [ ] Target **300+ reviewed, genuinely distinct** presets across keys, mallets/bells, strings, pads/textures, leads/plucks, bass, percussion, cinematic atmospheres and sequences/arps.
- [ ] Store category/subcategory/tags, recommended octave/range and useful macro labels.
- [ ] Loudness-normalize browsing to a bounded musical range.
- [ ] Safety-render every factory patch across representative notes/velocities; reject non-finite/severely clipped output and missing assets.

---

# Phase F — Sequencer / arpeggiator **(P3)**

## F1. Real-time core

- [ ] Host-sync/internal clock, 1-64 steps, musical divisions including dotted/triplet, swing and 1-4 octave range.
- [ ] Modes: Up, Down, Up/Down, Down/Up, Played Order, Chord, Random, Walk and Pattern.
- [ ] Latch/hold and explicit restart/reset/free-run behavior.
- [ ] Fixed/preallocated pattern state; no locks/allocations in the audio callback.

## F2. Step and modulation data

- [ ] Per-step pitch/degree, octave, velocity/accent, gate, rest, tie, probability, ratchet, bounded micro-timing, optional glide and macro values.
- [ ] At least two modulation lanes targeting modulation-safe destinations/macros with Hold/Linear/Smooth/bounded-random interpolation.
- [ ] Independent modulation probability and optional polymetric lane rate multipliers.

## F3. UI/persistence/content

- [ ] Large paged/zoomable step editor with dedicated lanes and clear tie/probability/ratchet feedback.
- [ ] Copy/paste/duplicate/rotate/reverse/invert/constrained-randomize and bounded humanize.
- [ ] Curated reusable pattern library independent of synth presets.
- [ ] Save active sequence in patches; version sequence schema independently; support reusable pattern files and `.rmpack` inclusion.
- [ ] Defer MIDI output until internal sequencing is stable across hosts.

---

# Phase G — LET THE MAGIC HAPPEN **(P3, only after Match gate)**

Magic deliberately evolves a sample/patch while preserving a user-selected amount of identity. It must never masquerade as a better Match score.

## G1. Workflow

1. Load a sample or patch and establish an immutable origin.
2. Choose a semantic **Direction**, **Intensity** and optional locked dimensions.
3. Generate several safe, audibly distinct variants.
4. Audition what changed; keep, evolve again, return to origin or branch.

## G2. Initial semantic directions

- [ ] Cinematic, Atmospheric, Organic, Orchestral, Staccato, Percussive
- [ ] Techno, Trance, Digital/FM, Warm/Analog
- [ ] Dark/Bright, Wide/Intimate, Rhythmic, Fragile, Aggressive, Glitch/Experimental

Each direction must map to an explicit bounded set of synthesis/routing dimensions. Descriptive labels are not permission for arbitrary randomization.

## G3. Identity preservation and safety

- [ ] Identity/intensity control determines how far mutation may move from origin.
- [ ] Locks for pitch, lifecycle/envelope, spectral character, stereo, modulation, FX and routing where applicable.
- [ ] Retain origin + branch history so evolution is reversible.
- [ ] Reuse Phase-A safety scoring; Aggressive/Glitch still cannot produce non-finite or uncontrolled clipped output.
- [ ] Show changed dimensions and do not report Match similarity as though creative transformation were recovery accuracy.

---

# Phase H — Release hardening and delivery **(continuous; final gate)**

- [ ] Native Windows VST3 + Standalone smoke/validation.
- [ ] Native macOS AU + VST3 + Standalone smoke/validation; keep JUCE-required ad-hoc VST3 signing separate from Xcode-managed signing policy.
- [ ] pluginval/auval coverage where practical.
- [ ] Session/preset migration fixtures for released schemas.
- [ ] Stress/soak tests for graph edits, candidate background work, preset browsing and sequencer transitions.
- [ ] Zero non-finite output in automated safety renders; bounded clipping/headroom policy documented.
- [ ] Final listening pass on reference fixtures and representative factory content.

---

# Implementation order from current `main`

1. **Close Phase A:** deterministic fixture corpus -> telemetry -> gain/headroom regressions -> perceptual identity constraints -> native Windows/macOS green.
2. **Close Phase B:** remaining Compare rendered-direction/reset/level-invariance tests and concise semantics help.
3. **Close GOLD integrity:** exact full-rack scoring, depth/headroom and offline/live equivalence.
4. **Finish Patch Map routing v2:** only safe deterministic utilities, latency and stress coverage.
5. **Preset/pack infrastructure**, then build and safety-render the factory library.
6. **Sequencer/arp** on stable engine/persistence foundations.
7. **Magic** as explicit creative evolution using the already-proven identity/safety infrastructure.
8. **Release hardening** stays continuous and becomes the final shipping gate.

## Definition of done for new work

Every new DSP feature needs: a clear semantic contract, bounded parameters, real-time-safe ownership, persistence/migration policy where stateful, at least one focused regression, and native CI validation. If a requirement belongs to an earlier phase, fix it there instead of duplicating it in a later feature section.
