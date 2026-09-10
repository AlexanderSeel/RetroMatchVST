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
- [~] Generated Match/Gold/AI state now passes recursive finite-parameter, bounded-topology, scalar-range and embedded-wavetable compatibility checks before application, alongside render safety telemetry; `.rmsynth` import/export now preflights embedded tables and runs recursive parameter/graph validation, while archive-level release-pack validation remains open.

### Global release gate

A phase that changes DSP is complete only when its focused regressions pass and the native Windows/macOS plug-in matrix is green. Do not hide a core matching regression behind new UI/content features.

---

# Phase A — Core match integrity and recovery **(P0)**

This is the current priority. The reference sample is the sonic contract; a numerically improved candidate must not win by becoming louder, harsher, unstable or lifecycle-inaccurate.

## A1. Deterministic reference corpus and observability

- [x] Build a small checked-in deterministic corpus covering kick, snare/percussion, short pluck, bass, piano/keys, mallet/bell, sustained lead, pad, staccato/string-like material, noisy texture and evolving atmosphere.
- [x] Store expected coarse identity facts per fixture: pitch confidence/range, lifecycle class, transient character, envelope duration, spectral balance, stereo character and whether nonlinear colour is expected.
- [x] Add candidate telemetry suitable for tests/debugging: peak, RMS, crest factor, DC, clipped/non-finite sample counts and a deterministic four-times oversampled peak estimate.
- [x] Add optional offline/debug stage snapshots for layer output, combine, pre-FX, post-FX, global bus and final output. No callback allocations.
- [x] Keep similarity level-normalized where the metric requires it; playback gain remains a separate musical/safety concern.

## A2. Lifecycle and evidence constraints

- [x] Detect self-terminating references and enforce zero sustain through main voice, FM operators, embedded layers/GOLD/AI companions and amplitude MSEG loop state.
- [x] Render held notes beyond the audible reference when evaluating one-shots and penalize non-silent generated tails.
- [x] Prevent clean references from gaining wavefold/saturation/nonlinear FX solely to improve aggregate similarity.
- [x] Apply nonlinear evidence constraints consistently to normal matches, one-shots, shared matcher paths, GOLD companions and post-sum/global processing.
- [x] FX-probe dynamics compares reference and candidate in the same peak/RMS crest-factor domain rather than mixing transient/sustain metadata into the target.
- [x] `SoundMatcher::initialFit()` lifecycle policy is applied after seed generation. Initial-fit `candidateFeatures`/`similarity` are intentionally unmeasured; `confidence` is only a seed-analysis heuristic until a rendered evaluation/refinement exists.

## A3. Gain staging, distortion and technical safety

- [x] Audit/document unity-gain expectations from oscillator -> layer -> layer combine -> per-instance FX -> global bus -> output.
- [x] Prove that adding companion layers or parallel branches cannot accidentally multiply level.
- [x] Add a clean-path invariant: a clean sine/reference stays finite, unclipped and free of unintended nonlinear processing through a complete generated rack.
- [x] Add bounded headroom/normalization where legitimate generated multilayer sums need it; do not use a limiter to conceal upstream gain bugs.
- [x] Penalize hard clipping, non-finite output, suspicious crest collapse and excessive alias-like high-frequency energy in candidate selection; crest collapse, clipping and non-finite output are covered, while the existing spectral-safety guard covers upper-band excess.
- [x] Add focused regressions for serial/parallel FX level, multilayer sums, GOLD racks, AI variants and Compare-adjusted racks.
- [x] Prove materially equivalent offline/live topology, gain and FX behavior for the same patch state.

## A4. Perceptual identity analysis

- [x] Formalize identity dimensions: pitch/harmonicity, transient, amplitude lifecycle, spectral envelope/resonances, noise/tonal ratio, temporal spectral motion, stereo/width and tail/reverb character.
- [x] Use explicit lifecycle classes: one-shot, plucked/decaying, gated, sustained and evolving.
- [x] Improve harmonic-series/pitch validation so inharmonic percussion and mallets are not forced into an inappropriate pitched model.
- [x] Analyze attack, early body, middle body and tail rather than relying on one representative frame.
- [x] Preserve high-confidence reference dimensions as bounded constraints during refinement.
- [x] Rank synthesis explanations from evidence: subtractive/spectral, reference wavetable, FM/harmonic, layered hybrid, noise/percussion, texture/chop and FX/guitar-chain where supported.
- [x] Score identity and technical safety separately enough that unsafe distortion cannot win on aggregate similarity.

## A5. Phase-A acceptance

- [x] Deterministic clean fixtures render finite with no hard clipping or unintended distortion.
- [x] Self-terminating references remain self-terminating under a held MIDI note.
- [x] Sustained references remain sustained on the deterministic corpus.
- [~] Quick/Refine/Gold strategy sweep rejects non-finite, clipped or unmeasured candidates, and ranking applies a bounded relative-loudness safety factor; corpus-wide harshness/level-bias confirmation still needs listening review.
- [x] Offline and live renders are materially equivalent for the same state.
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

- [x] Directional tests cover parameter semantics and rendered Brightness, Low End, Punch, Tail, Width, Motion and Fine Pitch where the analyzer exposes a robust metric.
- [x] Add an explicit reset-after-arbitrary-edits regression proving the immutable baseline is restored parameter-equivalently.
- [x] Add approximate level-invariance assertions for controls that should not act as loudness controls, especially Width/Brightness.
- [x] Add concise UI help/tooltips documenting each control's actual dimension/unit.

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

- [~] Editable audio fan-in reorder is now validated and serialized through the patch model; explicit pre/post processing-node ownership and UI actions remain open.
- [~] Centralize bounded Gain/Pan/Width operations in allocation-free routing utilities; generalized graph Split/Merge node execution remains open.
- [~] Added allocation-free normalized parallel branch mixing with deterministic compensation; bounded Parallel Core Gain and Parallel FX Gain controls now drive the supported split, while per-node branch controls remain open.
- [~] Preserve/compute latency across routed oversampled/nonlinear nodes; patch nodes now expose validated editing, persist bounded declared latency and DspRouting compilation computes deterministic maximum audio-path latency, while runtime delay application for arbitrary future graph processors remains open.
- [~] Add allocation-free atomic peak meters for the main bus and every rendered companion branch, plus Patch Map instance solo/audition; solo source state now persists through the validated graph and atomic routing plan, while richer audition presentation remains open.
- [~] Add validated model-level insert, merge, disconnect/restore edge and node-removal actions; generic node context exposes bounded declared-latency presets and undoable remove, while editable audio edges now expose utility insertion, compatible fan-in merge and disconnect-with-undo restoration.
- [x] Add persistent node-lock state and locked-position UI across Patch Map node types, plus modifier-click multi-select/group movement; minimap remains deferred unless eight-instance graphs prove it useful.
- [ ] Stress-test graph edits while audio is running.

## C3. Resynthesis integration

- [~] Treat only validated, bounded topology choices as discrete match dimensions; refinement now measures the untouched seed against the algorithm-profile hypothesis before selecting the profile as its parent, while later topology mutation and published DSP plans use bounded validation plus render safety gates.
- [x] Preserve user graph locks during Refine/AI runs and Patch Map auto-arrange/rebuilds; matching changes do not overwrite graph layout state.
- [~] Prefer reference-derived wavetable/topology choices only when measured identity improves; the initial refinement profile now competes against the untouched seed, while per-candidate profile/mutation selection remains part of the bounded search closure.
- [x] Centralize and clamp rack-depth choices to the supported 3/4/6/8-instance set and bound matcher population, iteration, topology-trial, sample-rate and render-duration requests.

### Patch Map acceptance

A user can build supported serial/parallel paths, edit valid modulation/layer routes, hear the change, undo it, save/reload it and reproduce the same sound. Invalid/unstable graphs never reach the audio thread.

---

# Phase D — GOLD full-rack resynthesis completion **(P1/P2)**

- [~] GOLD evaluates heterogeneous synthesis strategies and embeds multilayer racks; measured candidates that fail render telemetry are now rejected at application, while exact-rack score/result validation remains under review.
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

- [~] Add ZIP-compatible `.rmpack` with manifest: typed stable pack/patch IDs, version, author, description, tags/categories, minimum schema/plugin version and asset list; ZIP archive integration remains open.
- [ ] Reuse versioned `.rmsynth` patch data and include only required owned assets.
- [~] Validate paths/sizes; reusable pack-safety helpers now provide a typed manifest model, validated `manifest.json` read/write, bounded metadata/tag lists with unique patch IDs, SHA-256 format and asset-integrity verification, path traversal protection, asset limits and library-root containment, with dedicated round-trip/tamper/traversal regressions; ZIP archive integration remains open.
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

- [x] Host-sync/internal clock, 1-64 steps, musical divisions including dotted/triplet, swing and 1-4 octave range; DAW play/stop, restart-at-transport-start and host BPM now reach the audio-thread sequencer transport.
- [x] Modes: Up, Down, Up/Down, Down/Up, Played Order, Chord, Random, Walk and Pattern.
- [x] Latch/hold and explicit restart/reset/free-run behavior.
- [x] Fixed/preallocated pattern state; no locks/allocations in the audio callback.

## F2. Step and modulation data

- [x] Per-step pitch/degree, octave, velocity/accent, gate, rest, tie, probability, ratchet, bounded micro-timing, optional glide and macro values.
- [~] Two per-step macro lanes and independent modulation probability are bounded, editable and persisted; destination binding and Hold/Linear/Smooth/bounded-random interpolation remain open.
- [~] Per-step note probability and independent modulation probability are implemented and persisted; polymetric lane rate multipliers remain open.

## F3. UI/persistence/content

- [~] Sequencer UI exposes the active pattern and editing controls; a large paged/zoomable editor with dedicated tie/probability/ratchet lanes remains open.
- [x] Copy/paste/duplicate/rotate/reverse/invert/constrained-randomize and bounded humanize transforms are implemented.
- [~] Curated reusable pattern library independent of synth presets now provides eight bounded starting patterns and a sequencer UI loader; user-authored pattern files and larger browser remain open.
- [~] Active sequence state is persisted with the plug-in state; reusable pattern files and `.rmpack` inclusion remain open.
- [x] MIDI output remains deferred while the internal sequencer transport is stabilized.

---

# Phase G — LET THE MAGIC HAPPEN **(P3, only after Match gate)**

Magic deliberately evolves a sample/patch while preserving a user-selected amount of identity. It must never masquerade as a better Match score.

## G1. Workflow

1. Load a sample or patch and establish an immutable origin.
2. Choose a semantic **Direction**, **Intensity** and optional locked dimensions.
3. Generate several safe, audibly distinct variants.
4. Audition what changed; keep, evolve again, return to origin or branch.

## G2. Initial semantic directions

- [x] Cinematic, Atmospheric, Organic, Orchestral, Staccato, Percussive
- [x] Techno, Trance, Digital/FM, Warm/Analog
- [x] Dark/Bright, Wide/Intimate, Rhythmic, Fragile, Aggressive, Glitch/Experimental

Each direction must map to an explicit bounded set of synthesis/routing dimensions. Descriptive labels are not permission for arbitrary randomization.

## G3. Identity preservation and safety

- [~] Identity/intensity control now scales directed mutation depth and remains bounded to 0..1; the editor exposes subtle/medium/strong/maximum choices, and the processor exposes bounded normalized distance from the immutable Magic origin, while rendered branch-history reporting remains open.
- [~] Directed Magic variations now reuse the configured pitch/oscillator/FM/envelope/filter/modulation/FX locks after mutation; lifecycle and Patch Map routing locks remain separate follow-up work.
- [~] Processor captures a Magic origin on the first directed variation, exposes explicit capture/restore actions, keeps and persists a bounded 16-entry branch history with backward-walking restore APIs, and persists that origin through plug-in/preset state.
- [~] Directed variations are finite/range-clamped, preserve the loaded reference lifecycle, and the processor renders/evaluates them before applying, while KEEP/APPLY and no-reference safety rendering remain open.
- [~] Directed Magic labels now include bounded changed-dimension reporting and clear Match state; processor exposes origin-relative dimensions/distance and the editor now provides origin/branch restore controls with status-line diff reporting, while a dedicated audition/diff panel remains open.

---

# Phase H — Release hardening and delivery **(continuous; final gate)**

- [ ] Native Windows VST3 + Standalone smoke/validation.
- [ ] Native macOS AU + VST3 + Standalone smoke/validation; keep JUCE-required ad-hoc VST3 signing separate from Xcode-managed signing policy.
- [ ] pluginval/auval coverage where practical.
- [ ] Session/preset migration fixtures for released schemas.
- [ ] Stress/soak tests for graph edits, candidate background work, preset browsing and sequencer transitions.
- [~] Live output and OfflineRenderer now have final non-finite firewalls; live invalid-sample counts are exposed atomically, while automated zero-nonfinite assertions and final clipping/headroom verification remain open.
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
