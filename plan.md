# RetroMatch implementation plan

## MSEG + global bus + large Patch Map implementation block

- MSEG is treated as a primary resynthesis dimension: the matcher seeds MSEG from transient/spectral-motion analysis and mutates target, depth, loop and shape during refinement.
- Factory families with meaningful motion now intentionally author MSEG rather than relying only on LFOs.
- The FX page exposes a GLOBAL BUS rack using the same filter/effect catalog as instance racks. It processes once after all active synth instances are combined.
- Patch Map shows the global bus explicitly, exposes editable cables by normal left-click, increases cable hit targets and provides a BIG overlay view.
- Next matching milestone: advisor-driven method/depth ranking, heterogeneous method search, complete multilayer scoring and excitation-probe FX-chain reconstruction.

## Current milestone: playable semi-modular patch map

The next major UI/DSP milestone turns **SIGNAL LAB / PATCH MAP** from a read-only overview into a safe semi-modular sound-design surface. The goal is creative routing freedom without allowing illegal graphs, unstable feedback, hidden real-time allocations, or breaking existing `.rmsynth`/DAW automation compatibility.

### Status legend

- [x] implemented
- [~] started / partially implemented
- [ ] planned

## 0. Guardrails

- [x] Keep all existing APVTS parameter IDs and released automation order intact; new host parameters are append-only.
- [x] Keep expensive analysis, graph rebuilding and file work off the real-time audio callback.
- [x] Keep the existing synth engine as the source of truth for playback and offline matching.
- [ ] Any feedback-capable routing must have explicit gain limiting, delay/state ownership and cycle validation.
- [x] Old sessions without graph state load into the current canonical signal chain; Patch Map rebuilds the typed graph from restored DSP/APVTS state.
- [x] Currently supported graph edits are undoable and serializable before arbitrary audio routing is enabled.

## 1. Interactive canvas foundation

- [x] Wheel zoom around the mouse pointer.
- [x] Empty-space pan and double-click-to-edit navigation.
- [x] **Drag nodes** to freely reposition the patch map.
- [x] Optional **snap-to-grid**; hold Shift while dragging for temporary free movement.
- [x] Add **AUTO**, **FIT**, **zoom -**, **100%**, **zoom +** and **GRID** controls.
- [x] Draw a scalable workspace grid.
- [x] Replace ad-hoc straight wires with an explicit node/edge model and curved connections.
- [x] Separate the audio chain from modulation/clock relationships visually: MOD no longer pretends to be an audio insert.
- [x] Show connection ports so later cable editing has a clear interaction target.
- [x] Persist custom node positions, pan, zoom and grid preference in UI/session state.
- [x] LARGE PATCH MAP is genuinely resizable: content follows the window, the full toolbar stays visible, and the graph re-fits after overlay resize.
- [ ] Multi-select, box select, group move and optional node locking.
- [ ] Minimap for very large eight-instance patches.

## 2. Typed graph model and validation

Introduce an engine-neutral graph model shared by the UI, state serializer and routing compiler.

### Node model

Each node gets a stable ID, type and capabilities, for example:

- Instance / layer source
- OSC / wavetable source
- FM / harmonic generator
- Filter / amp stage
- FX rack
- Combine / mixer
- Modulation source/router
- Tempo clock
- Master output
- Future utility nodes: split, merge, gain, pan, stereo width, envelope follower

### Port types

- **Audio** — mono/stereo signal flow
- **Modulation** — normalized/bipolar control values
- **Clock** — tempo/sync events or resolved timing information
- **Control** — non-audio configuration links where needed

### Validation rules

- [x] Only compatible port types can connect.
- [x] Audio must eventually resolve to MASTER OUT.
- [x] No accidental zero-delay audio cycles.
- [x] Modulation cannot target parameters that are not modulation-safe.
- [x] A node cannot own invalid duplicate inputs unless its topology explicitly supports mixing.
- [x] Show a clear reason when a cable drop is rejected.
- [x] Topological ordering is deterministic and independent of visual node position.

## 3. Cable editing and routing UI

- [x] Drag from the MOD output jack to a compatible modulation input to create a DSP-backed connection.
- [x] Drag an existing modulation cable destination endpoint to reconnect it while preserving source/depth.
- [x] Click cable to select; Delete removes editable routes/layer combines; right-click or double-click opens detailed editing.
- [ ] Hover highlights the complete upstream/downstream path.
- [x] Distinct visual language for audio, modulation and clock cables.
- [~] Quick actions: FX-node right-click now provides a DSP-backed **SERIAL / PARALLEL** split primitive; generic Insert/Merge/Disconnect/Restore actions remain pending.
- [ ] Context menu on nodes: Edit, Bypass, Solo, Mute, Duplicate, Randomize, Lock position.
- [x] Undo/redo for supported graph operations: node layout, modulation cable create/edit/reconnect/delete and layer-combine edits.

## 4. DSP routing compiler

The visual graph must create a real sonic change, not just redraw lines.

### First safe routing targets

- [ ] Reorder supported pre/post FX sections.
- [~] Parallel FX branching is implemented per synth instance: PRE rack and built-in FX core split from the same dry input, merge at compensated 50/50 gain, then POST rack runs once. Arbitrary multi-node branches remain pending.
- [ ] Move selected utility processing before/after filter and FX where DSP ownership allows it.
- [x] Expose layer combine order/operation through the graph. Layer combine cables now persist an explicit sequence and expose **MOVE EARLIER / MOVE LATER**.
- [x] Route MOD/MSEG/LFO outputs to supported modulation destinations from the patch map.

### Engine work

- [x] Compile the validated graph to a lightweight immutable processing plan outside the audio thread. Compiler v1 turns persisted layer-combine sequences into a fixed seven-layer permutation.
- [x] Swap processing plans atomically at block boundaries. The seven-layer permutation is packed into one `uint32_t` atomic snapshot; the audio callback does not read `ValueTree` state or allocate.
- [~] Preallocate node/process buffers in `prepareToPlay` or graph-plan preparation. Layer engines plus the new per-engine parallel-FX scratch are preallocated; arbitrary multi-node graph buffers remain pending.
- [~] Preserve latency accounting for oversampled/nonlinear nodes. Layer-order routing keeps the existing fixed latency path unchanged; arbitrary routed latency compensation is still pending.
- [~] Add dry/wet and gain normalization around parallel branches. The first FX split uses deterministic 0.5/0.5 correlated-unity compensation; configurable branch gain/dry-wet remains pending.
- [ ] Add safety limiting for intentionally supported feedback structures.

## 5. Sound-design playground

- [ ] Add node browser / **ADD NODE** palette.
- [ ] Utility nodes: Gain, Pan, Width, Split, Merge, Envelope Follower, Macro, Meter.
- [ ] Branch solo and audition.
- [ ] Live mini meters on important audio nodes.
- [ ] Animated signal intensity on active cables without doing expensive analysis on the audio thread.
- [ ] Eight macros assignable from graph nodes/parameters.
- [ ] Scene A/B routing snapshots and morphing where topology is compatible.
- [ ] Randomize selected node, branch or modulation routes with musical constraints.
- [ ] Routing templates: Mono Lead, Wide Pad, FM Bell, Hybrid WT, Body+Air, Texture Stack.

## 6. Resynthesis integration

The matcher should be able to exploit the richer graph rather than only parameter values.

- [ ] Treat safe routing topology as a discrete optimization dimension in deeper resynthesis modes.
- [ ] Prefer reference-derived wavetables when they materially improve spectral/temporal similarity.
- [ ] Allow different strategies to bias topology: Reference WT, Spectral/Subtractive, FM/Harmonic, Layered Studio, Texture/Chop.
- [ ] Keep topology mutation bounded so candidate search cannot explode combinatorially.
- [ ] Visual Compare should highlight which branch contributes to spectral/timbre mismatches.
- [ ] Preserve user graph locks during Refine/AI runs.

## 7. Persistence and compatibility

- [x] Serialize graph version, nodes, edges, positions and UI viewport into `.rmsynth` and DAW state.
- [x] Migrate legacy sessions without graph state to the canonical graph rebuilt from their restored synth state.
- [x] Temporary meters/hover/selection and undo history are not serialized.
- [x] Add graph schema versioning independent of plug-in version. Schema v3 adds optional processor `routingMode`; v2 adds layer-combine sequence metadata; older sessions default to canonical serial routing.
- [x] Validate and repair malformed graph state instead of crashing; invalid nodes/edges are skipped during ValueTree restore.

## 8. Verification

- [x] Static contracts for graph model, validation and append-only automation.
- [x] Unit tests for allowed/forbidden connections.
- [x] Topological-sort and cycle-detection tests.
- [x] DSP tests proving routing changes produce different but finite/non-silent output. Compiler v1 compares canonical and reordered non-commutative layer-combine renders.
- [~] Parallel split/merge loudness regression tests: the first FX routing regression verifies finite/non-silent output, audible serial-vs-parallel change and a bounded level jump; broader fixture coverage remains pending.
- [x] Session round-trip tests for typed graph + custom node layout/view state, including v2 combine sequence persistence.
- [ ] Stress test continuous graph editing while audio is running.
- [ ] VST3/AU/Standalone builds and pluginval/auval coverage.

## Acceptance for the semi-modular milestone

A user can rearrange the patch visually, create/reconnect/remove only valid cables, build serial and parallel signal paths, route modulation from the graph, hear the changed topology immediately, undo the operation, save the patch, reload it and obtain the same routing and sound. Invalid or unstable graphs are rejected before they reach the audio thread.

## Implementation order

1. **Canvas interaction** — node drag, grid, zoom controls, fit/auto arrange. *(started on main)*
2. **Persistent graph data model** — stable IDs, typed ports, edge serialization, validation. *(implemented; typed schema v2 persisted in session/preset state)*
3. **Cable editor** — connection creation/reconnection/deletion + undo. *(v1 implemented for DSP-backed modulation and layer-combine cables; fixed audio topology remains protected)*
4. **DSP compiler v2** — safe serial/parallel audio routing and layer combine topology. *(layer ordering + atomic plan + per-instance compensated FX parallel routing implemented; arbitrary branch utilities remain next)*
5. **Modulation cable routing** — expose supported destinations through the graph. *(implemented for current modulation-safe destinations)*
6. **Sound-design utilities/macros/scenes**.
7. **Resynthesis topology search**.

The implementation should favor a small number of musically useful, deterministic routing primitives over a fully unrestricted modular environment. RetroMatch should remain fast enough for live use and understandable enough that a matched patch can still be edited like an instrument rather than debugged like a graph program.

## 9. Post-analysis Compare fine-tune

After Quick/Refine/Gold has produced a measured candidate, **Visual Compare** should become a controlled correction surface instead of only a read-only score screen. The raw analysed candidate remains immutable; every correction is an offset from that baseline so A/B and RESET are always trustworthy.

### Correction model

- [x] Add an engine-side, non-destructive fine-tune mapping that operates on `VoiceParameters` and clones embedded Gold layers instead of mutating the candidate bank.
- [x] Keep the zero position exactly neutral and clamp every correction to existing safe DSP ranges.
- [x] Add a DSP regression proving the Brightness control changes rendered/measured spectral centroid in the expected direction.
- [x] Keep **Baseline** and **Adjusted** states side-by-side in the processor; A/B/C now retain independent correction + measured-Adjusted state.
- [x] Never turn a static patch into a moving patch implicitly: Motion scales existing MSEG/LFO/mod routes only.
- [x] Store fine-tune offsets outside the released APVTS automation order; closing Compare restores baseline unless **KEEP / APPLY** explicitly commits the adjusted working patch.

### First knob set

| Control | Musical intent | Primary DSP mapping | Compare metric to watch |
| --- | --- | --- | --- |
| **BRIGHTNESS** | darker ↔ brighter | logarithmic filter cutoff shift | spectral centroid / high-energy residual |
| **LOW END** | lean ↔ heavier | bounded sub-oscillator balance | low-energy ratio / spectrum |
| **PUNCH** | softer ↔ harder onset | amp + FM attack time | attack / transient score |
| **TAIL** | shorter ↔ longer body | decay, sustain and release | temporal/envelope residual |
| **WIDTH** | mono/tight ↔ wide | stereo width, existing unison spread, rack pan spread | stereo residual |
| **MOTION** | steadier ↔ more animated | scale existing MSEG/LFO/mod depths | spectral-motion / temporal residual |
| **FINE PITCH** | residual tuning correction | ±50 cents around analysed candidate | pitch residual |

Do **not** add generic Drive/Reverb/Delay macros to this first row: those already have explicit editing surfaces and would make Compare a second synth editor. Add a correction only when it corresponds to a clearly visible/measurable mismatch.

### Compare UX

- [x] Put the seven correction knobs directly below/alongside the current Reference-vs-Resynth plots, all bipolar and center-detented except where a more specific unit display is useful.
- [x] Keep existing **REFERENCE / SYNTH / MIX** audition and add **BASELINE / ADJUSTED** so the ear can compare the correction without losing the reference A/B.
- [x] While dragging, apply the adjusted parameters to the live synth immediately; do not perform expensive offline analysis in the audio callback or on every mouse tick.
- [x] Debounce Adjusted after knob movement (~550 ms), render/score it on a dedicated background worker, reject stale results, and update measured score/feature traces only after a valid offline result. **MEASURE** remains as a manual immediate trigger.
- [x] Overlay three states where useful: Reference, Baseline and measured Adjusted; Baseline is a dim/ghost trace.
- [x] Show measured before→after deltas for Spectrum, Timbre, Temporal, Envelope, Harmonic, Stereo and Pitch in the similarity grid.
- [x] Never estimate or cosmetically inflate similarity. Until a re-render completes, label the adjusted score **PENDING MEASURE** and keep the baseline trace/score distinct.
- [x] **RESET** returns all knobs to zero and exactly restores the selected candidate baseline.
- [x] **KEEP / APPLY TO PATCH** commits the adjusted `VoiceParameters` as the editable patch while retaining the original candidate bank for comparison/history.
- [x] **AUTO NUDGE** derives one bounded correction step from directed feature residuals, performs a real background re-render, and automatically rejects the suggestion unless measured total similarity improves.

### Verification for Compare fine-tune

- [x] Zero-correction output matches the selected candidate baseline.
- [ ] Each correction moves its intended rendered feature in the expected direction on a deterministic fixture set.
- [x] Gold/full-rack corrections preserve layer topology and immutable baseline state.
- [ ] Reset restores the baseline after arbitrary knob moves.
- [x] Candidate A/B/C switching maintains independent correction/measured state; no cross-candidate leakage.
- [x] Re-measure work is invoked from Compare and never runs inside `processBlock`.
- [x] Session/preset policy is explicit: temporary compare state is not serialized; only **KEEP / APPLY** leaves the adjusted live patch in normal APVTS/session state.

## Gold Match full-rack resynthesis

- Add a dedicated GOLD search mode after the fast Quick/Refine paths.
- Sweep all supported synthesis explanations, with the Resynthesis Advisor recommendation evaluated first.
- Deep-refine the strongest heterogeneous methods rather than producing three variants of one topology.
- For each finalist, build and render the complete 3 / 4 / 6 / 8-instance rack and choose depth by measured reference similarity.
- Store the exact embedded rack in the winning candidate so selecting A/B/C reproduces what was measured.
- Keep FX white-noise/impulse probing secondary to the musical reference score; never inflate the displayed similarity to meet a target.
- Show method, depth and FULL RACK status in candidate cards and Compare.

### Rack-level evolution

- Gold does not stop after choosing a synthesis method and layer count.
- Evolve the completed rack with bounded full renders: main/layer gain, pan, tune, role timbre and MSEG movement.
- Include a whole-instrument post-sum filter/FX rack in the offline Gold model and mirror the winning rack into the live global FX controls.
- Use white-noise transfer analysis at higher spectral resolution plus a multi-tone nonlinear probe for guitar/pedal/amp-chain matching.
- Keep excitation probes diagnostic/secondary; the musical full-rack similarity remains the displayed truth.

---

# 10. Priority recovery milestone — analysis, match quality, Compare semantics and distortion

**This is now the highest-priority milestone. New sound-design features must not hide a regression in the core promise of RetroMatch.** The loaded sample is the user's sonic intent; analysis and resynthesis must first reproduce that intent cleanly before the engine deliberately transforms it.

## 10.1 Reproduce and measure the regression before retuning algorithms

- [ ] Build a deterministic reference fixture corpus covering: kick, snare, percussion, short pluck, bass, piano, marimba/bell, sustained lead, pad, strings/staccato, noisy texture and evolving atmosphere.
- [ ] Store expected pitch/lifecycle/transient/spectrum/envelope characteristics for the fixtures so future matcher changes have a stable baseline.
- [ ] Add render telemetry for every candidate stage: peak, true/oversampled peak where practical, RMS, short-term loudness, crest factor, DC, clipped-sample count and non-finite sample count.
- [ ] Add optional debug snapshots at engine stages: oscillator/layer output, per-layer combine, pre-FX, post-FX, global bus and final output. This must be diagnostic-only and never allocate in the real-time callback.
- [ ] Compare current `main` against the last known subjectively acceptable matching revision using the same fixtures and MIDI notes.
- [ ] Do not compensate a poor timbral match by simply making a candidate louder. Similarity scoring must use level-normalized analysis where appropriate while playback preserves safe musical gain.

## 10.2 Eliminate the "everything sounds over-distorted" failure mode

- [ ] Audit gain staging from each oscillator through layered GOLD racks, serial/parallel FX, global bus and final output. Explicitly document the expected unity-gain point of every combine operation.
- [ ] Verify that adding layers, companion voices or parallel branches cannot multiply amplitude accidentally.
- [ ] Verify nonlinear stages are not enabled or driven merely to increase spectral similarity. Drive/distortion must only appear when the reference actually supports it.
- [ ] Add a clean-path invariant: with drive/wavefold/saturation/clip FX disabled, a clean sine/reference fixture must remain clean and unclipped through the complete rack.
- [ ] Add per-stage headroom and bounded normalization where a generated multilayer rack can legitimately sum hot.
- [ ] Add an output safety ceiling/soft protection for generated candidates, but do **not** use the limiter to mask an upstream gain bug.
- [ ] Penalize hard clipping, excessive high-frequency alias-like energy and unexpectedly reduced crest factor in matcher scoring.
- [ ] Ensure the offline renderer and live engine use the same gain/FX topology so a candidate cannot score clean offline and distort after being applied live.
- [ ] Add regression tests for serial versus parallel FX loudness, multilayer sums, GOLD racks, AI suggestions and Compare-adjusted racks.

## 10.3 Rework analysis around perceptual identity instead of isolated statistics

- [ ] Split analysis into explicit identity dimensions: pitch/harmonicity, transient, amplitude lifecycle, spectral envelope, resonant peaks/formants, noise/tonal ratio, temporal spectral motion, stereo/width and tail/reverb character.
- [ ] Detect one-shot, plucked/decaying, gated, sustained and evolving lifecycle classes before envelope synthesis decisions are made.
- [ ] Improve pitch confidence and harmonic-series validation so inharmonic/mallet/percussion material is not forced into an inappropriate pitched model.
- [ ] Analyse multiple temporal windows rather than one representative sustain frame: attack, early body, middle body and tail.
- [ ] Preserve strong reference features as constraints during refinement instead of allowing later optimization passes to destroy them for a small aggregate score gain.
- [ ] Let the Resynthesis Advisor rank synthesis explanations from those features: subtractive, reference wavetable, FM/harmonic, layered hybrid, noise/percussion, texture/chop and combinations.
- [ ] Score both **identity** and **technical safety**. A numerically closer but obviously distorted/unstable result must lose to a slightly lower-scoring clean candidate.

## 10.4 Make Compare controls do exactly what their names say

The current Compare correction layer must be treated as a semantic contract. A knob called **Brightness** cannot unexpectedly behave like Drive; **Tail** cannot recreate sustain on a one-shot; **Width** cannot become a loudness boost.

- [ ] Create a deterministic directional test for **every** Compare control, not only Brightness.
- [ ] **BRIGHTNESS:** primarily spectral tilt/filter cutoff. Must raise/lower measured centroid and high-band ratio without materially increasing harmonic distortion or overall loudness.
- [ ] **LOW END:** primarily low-band balance/sub contribution. Must change low-band energy without turning the complete sound louder or muddying the entire spectrum.
- [ ] **PUNCH:** primarily attack/transient shape. Must alter attack slope/crest/onset energy; do not map it to generic drive.
- [ ] **TAIL:** decay/release/body length. For self-terminating references it must remain self-terminating and never force non-zero sustain or looping amplitude MSEG.
- [ ] **WIDTH:** stereo spread/pan decorrelation only where stereo structure exists. Maintain approximately constant perceived level through compensation.
- [ ] **MOTION:** scale existing modulation/MSEG movement. It must not invent modulation when the baseline is static unless the UI explicitly switches to a creative mode.
- [ ] **FINE PITCH:** pitch only, with no timbral or gain side effects.
- [ ] Replace any correction mapping that touches unrelated nonlinear/FX parameters solely because it improves the aggregate score.
- [ ] Surface a concise tooltip/detail line for each Compare knob showing the actual controlled dimensions and units.
- [ ] Keep all Compare controls center-neutral and make RESET bit-equivalent/parameter-equivalent to the immutable candidate baseline.
- [ ] Re-render Adjusted in the background and show measured deltas only. Never predict an improvement from the knob position.
- [ ] AUTO NUDGE may move multiple controls only when each move follows its semantic residual and the resulting measured candidate is both safer and more similar.

## 10.5 Recovery acceptance gate

Do not move the sequencer/Magic work to default-on production behavior until this gate passes:

- [ ] Clean fixture renders contain no hard clipping/non-finite samples and do not acquire unintended distortion.
- [ ] One-shot references naturally become silent under a held MIDI note unless the source clearly contains a sustained tail.
- [ ] Sustained references remain sustained.
- [ ] Every Compare knob passes its directional feature test and RESET exactly restores baseline.
- [ ] Offline candidate and live applied candidate produce materially equivalent level/timbre for the same state.
- [ ] A human listening pass across the fixture corpus confirms Quick/Refine/Gold are not systematically harsher or more distorted than the references.

# 11. Preset library and full patch-pack import/export

RetroMatch should ship as a sound-design instrument even when no sample is loaded. Presets must demonstrate the range of the engine and provide starting points for the Magic/evolution workflow.

## 11.1 Pack format

- [ ] Add a versioned `.rmpack` container (ZIP-compatible internally) with a manifest plus patches and optional owned assets.
- [ ] Manifest fields: pack ID, name, version, author, description, tags/categories, minimum plug-in/schema version and patch list.
- [ ] Store patches as normal versioned `.rmsynth` data wherever possible; include reference-derived wavetables or other required owned assets only when the patch depends on them.
- [ ] Use stable patch IDs so re-importing an updated pack can distinguish update, duplicate and conflict.
- [ ] Validate all paths and sizes during import; never allow a pack to write outside the preset library.
- [ ] Gracefully report missing/unsupported assets and continue importing valid patches when possible.

## 11.2 Library operations

- [ ] **IMPORT PACK** — load a complete `.rmpack` in one action.
- [ ] **EXPORT PACK** — export selected presets/categories as one pack with editable metadata.
- [ ] **EXPORT ALL PATCHES** — one command to back up the complete user preset library.
- [ ] **IMPORT PATCHES/FOLDER** — bulk import existing `.rmsynth` files without manually selecting each one.
- [ ] Conflict policy: Ask / Keep Existing / Replace / Import as Copy; remember the choice only for the current operation unless the user explicitly saves it.
- [ ] Add progress/report UI with imported, updated, skipped and failed counts.
- [ ] Keep factory presets read-only; saving an edited factory patch creates a user copy.
- [ ] Add library search by name, category, tags, author, favorite and sound character.

## 11.3 Large factory library target

Target **300+ genuinely distinct factory presets**, not parameter-randomized filler. Build them in reviewed families and include normal playable patches plus sequenced/evolving versions.

Suggested first distribution:

| Family | Target | Examples |
| --- | ---: | --- |
| Piano / Keys | 30+ | soft piano, glass piano, broken tape keys, EP hybrids |
| Mallets / Bells | 30+ | marimba, kalimba, vibraphone, metallic bell, FM mallets |
| Strings / Orchestral-inspired | 45+ | warm ensemble, solo-like layers, staccato, spiccato-inspired, hybrid brass/string stacks |
| Pads / Textures | 50+ | warm, frozen, granular-like, airy, dark, evolving, huge cinematic beds |
| Leads / Plucks | 45+ | techno, trance, analog, FM, digital, short plucks, acid-like and unusual leads |
| Bass | 30+ | sub, reese, FM, plucked, cinematic low-end, techno bass |
| Percussion / Drums | 35+ | kick-like, snare-like, toms, metallic hits, synthetic percussion, impacts |
| Cinematic Atmospheres | 40+ | tension, mystery, space, organic, dystopian, hopeful, transition beds |
| Sequences / Arps | 40+ | rhythmic pulses, melodic arps, generative beds, ostinatos, evolving soundscapes |

- [ ] Every factory preset receives category, subcategory, tags, recommended octave/range and optional macro labels.
- [ ] Loudness-normalize the library to a bounded musical range so browsing presets does not jump between whisper-quiet and clipped.
- [ ] Run an offline safety render across multiple notes/velocities for every factory preset before shipping it.
- [ ] Add a preset-content test that rejects non-finite output, severe clipping and missing pack assets.

# 12. New SEQUENCER / ARP tab — phrase and soundscape builder

Add a dedicated top-level **SEQUENCER** tab. The goal is closer to a workstation phrase/arp designer than a minimal synth arpeggiator: fast presets for immediate results, but deep enough to build evolving rhythmic soundscapes.

## 12.1 Core playback engine

- [ ] Host-sync and internal-clock modes; tempo follows the existing shared tempo system.
- [ ] Pattern length 1–64 steps with common musical divisions, dotted/triplet options and per-pattern swing.
- [ ] Modes: Up, Down, Up/Down, Down/Up, Played Order, Chord, Random, Walk and Pattern.
- [ ] 1–4 octave range with configurable octave ordering.
- [ ] Latch/hold, restart mode, note-reset/transport-reset/free-run behavior.
- [ ] Real-time engine must use fixed/preallocated pattern state and avoid allocation/locks in the audio callback.

## 12.2 Step data

Each step can contain:

- [ ] pitch/degree or chord-relative note
- [ ] octave offset
- [ ] velocity/accent
- [ ] gate length
- [ ] rest
- [ ] tie/legato
- [ ] probability
- [ ] ratchet/retrigger count
- [ ] micro-timing/nudge within a bounded safe range
- [ ] glide/slide where the active voice mode supports it
- [ ] optional per-step macro/modulation values

## 12.3 Soundscape lanes

Beyond the note lane, add optional automation lanes so a pattern can animate the synth rather than merely trigger it.

- [ ] Two or more assignable modulation lanes targeting modulation-safe destinations/macros.
- [ ] Curve shapes/step interpolation: Hold, Linear, Smooth and Randomized-within-range.
- [ ] Per-step probability for modulation events independent of note probability.
- [ ] Scene/macro lane for slowly moving between timbral states over a longer pattern.
- [ ] Pattern rate multiplier per modulation lane for polymetric movement without creating unsafe audio-thread complexity.

## 12.4 Sequencer UI

- [ ] Large central 16/32/64-step editor with horizontal paging/zoom rather than tiny controls.
- [ ] Step bars show velocity by height and gate by width; explicit icons/markers for tie, probability and ratchets.
- [ ] Piano/scale overlay for melodic Pattern mode.
- [ ] Lane selector underneath: Velocity, Gate, Probability, Ratchet, Pitch/Octave, Macro 1…8 / Mod Lane 1…N.
- [ ] Pattern browser on the left/right with categories such as Basic Arp, Techno, Trance, Ostinato, Cinematic Pulse, Organic, Broken Rhythm, Generative and Ambient.
- [ ] Copy/paste/duplicate/rotate/reverse/invert/randomize selected steps.
- [ ] Humanize controls for timing/velocity with bounded ranges and one-click reset.
- [ ] Drag pattern length and loop region directly in the ruler.

## 12.5 Pattern library and patch persistence

- [ ] Ship a large curated pattern library independent of synth presets so any patch can audition many phrases quickly.
- [ ] Save the active sequence inside the patch so a sequenced factory preset recalls exactly.
- [ ] Also allow exporting/importing reusable pattern files and include them in `.rmpack` when referenced.
- [ ] Sequence state needs its own schema version so future step lanes can be added without breaking old patches.
- [ ] MIDI output can be considered later; first milestone drives RetroMatch internally to avoid host-specific complexity.

# 13. "LET THE MAGIC HAPPEN" — guided sound evolution

This becomes RetroMatch's creative signature. **The loaded sample is the seed/mind; analysis captures its identity, and the synth provides the space in which that identity can evolve.** It must be different from Match: Match tries to reproduce; Magic deliberately transforms while preserving a user-selected amount of identity.

## 13.1 Workflow

1. Load a sample or choose an existing patch.
2. RetroMatch analyses the sonic identity and establishes an immutable origin state.
3. Choose a **Direction** and **Intensity**.
4. Optionally lock dimensions that must not move.
5. Press **LET THE MAGIC HAPPEN**.
6. Generate several safe, meaningfully different variations (A/B/C or more), audition them instantly and show what changed.
7. Keep one, evolve it again, return to the origin, or branch into another direction.

## 13.2 Direction selector

Start with curated semantic directions whose mappings are explicit and testable:

- [ ] **Cinematic** — depth, controlled width, long-form motion, orchestral/hybrid layering where appropriate
- [ ] **Atmospheric** — air, slow movement, diffuse tail, restrained transient
- [ ] **Organic** — softer spectra, imperfect/slow modulation, natural attack variation
- [ ] **Orchestral** — ensemble-like layering, articulation-aware envelopes, restrained synthetic FX
- [ ] **Staccato** — shorter articulation, stronger onset, controlled tail
- [ ] **Percussive** — transient/noise/body emphasis, self-terminating envelope discipline
- [ ] **Techno** — focused low end, pulse/groove modulation, assertive but bounded nonlinear color
- [ ] **Trance** — harmonic brightness, rhythmic modulation/arp compatibility, width with level compensation
- [ ] **Digital / FM** — harmonic sidebands, metallic/glassy character, precise movement
- [ ] **Warm / Analog** — gentle spectral softening, subtle drift, saturation only within strict headroom
- [ ] **Dark** / **Bright** — spectral direction without automatic loudness/drive changes
- [ ] **Wide / Intimate** — spatial direction with compensated level
- [ ] **Rhythmic** — introduce or intensify tempo-related modulation and optionally suggest a sequence
- [ ] **Fragile** — reduce density, shorten/soften selected layers, preserve expressive detail
- [ ] **Aggressive** — controlled transient/harmonic density; must remain under the anti-clipping gate
- [ ] **Glitch / Experimental** — bounded unusual modulation/topology/sequence mutations with always-available undo
- [ ] **Surprise Me** — choose among safe direction combinations while respecting all locks.

## 13.3 Identity preservation and locks

- [ ] **IDENTITY** control: Original ↔ Inspired ↔ Transform. This determines how strongly reference features constrain evolution.
- [ ] Locks for Pitch, Envelope, Transient, Spectrum, Stereo, Motion, FX, Layers and Sequence.
- [ ] One-shot lifecycle is a hard lock by default for clearly self-terminating references unless the user explicitly chooses a direction that converts it into a pad/texture.
- [ ] Preserve strong recognisable resonances/formants when identity is high.
- [ ] The engine may add layers/routing only under bounded gain/headroom rules.

## 13.4 Evolution engine

- [ ] Reuse measured reference features and the Resynthesis Advisor rather than randomizing the entire parameter space.
- [ ] Express every Direction as weighted target ranges over meaningful musical dimensions, not raw arbitrary parameter mutations.
- [ ] Generate a small diverse candidate bank; reject duplicates/near-duplicates before presenting results.
- [ ] Render every candidate offline for safety and feature measurement before it becomes selectable.
- [ ] Hard-reject non-finite/clipped/unstable candidates and strongly penalize unintended loudness jumps.
- [ ] Keep a deterministic random seed in history so a liked evolution can be reproduced.
- [ ] Allow **EVOLVE AGAIN** to use the selected child as the new origin while keeping a breadcrumb/history back to the loaded sample.
- [ ] Respect Patch Map locks, Compare semantic constraints and user-locked parameters.
- [ ] For Rhythmic/Techno/Trance/Cinematic directions, optionally generate or select a compatible Sequencer pattern as part of the candidate.

## 13.5 Magic UI

- [ ] Prominent **LET THE MAGIC HAPPEN** button on the analysis/result workflow and a dedicated expanded evolution panel.
- [ ] Direction selector with tags/cards rather than a huge raw combo box.
- [ ] Intensity and Identity controls plus visible lock chips.
- [ ] Variation cards show Direction, identity distance, layer count, sequence status and safety/level status; do not expose a fake similarity percentage when the goal is transformation.
- [ ] Instant origin/variation A/B audition.
- [ ] **KEEP**, **EVOLVE AGAIN**, **BACK TO ORIGIN** and **SEND TO COMPARE** actions.
- [ ] Show a concise change summary such as “brighter harmonic layer + slower MSEG + wider post layer + 16-step pulse”; no opaque “AI changed it” language.

# 14. Step-by-step delivery order for the new direction

The following order is deliberate. It prevents creative features from making the current matching regression harder to diagnose.

### Phase A — stop the quality regression

1. [ ] Freeze matcher/Compare feature expansion temporarily.
2. [ ] Build the deterministic reference fixture corpus and render telemetry.
3. [ ] Trace and fix gain staging / unintended distortion from oscillator to final output.
4. [ ] Align offline and live render topology/gain.
5. [ ] Re-baseline analysis and similarity scoring against clean fixtures.
6. [ ] Rework all seven Compare mappings and add directional tests.
7. [ ] Listening/safety acceptance pass; only then remove the recovery gate.

### Phase B — library foundation

8. [ ] Versioned `.rmpack` format and manifest.
9. [ ] Bulk patch import/export + **EXPORT ALL PATCHES**.
10. [ ] Preset browser metadata/search/favorites/category improvements.
11. [ ] Produce the first 100 reviewed factory presets across all major categories.
12. [ ] Add automated preset safety rendering.

### Phase C — sequencer

13. [ ] Real-time 1–64 step sequencer/arp core and host sync.
14. [ ] Dedicated Sequencer tab with note/velocity/gate/probability/ratchet editing.
15. [ ] Modulation/soundscape lanes and macro targeting.
16. [ ] Pattern browser, pattern import/export and preset integration.
17. [ ] Build a curated pattern library and sequenced factory presets.

### Phase D — Magic evolution

18. [ ] Direction model + Identity/Intensity + locks.
19. [ ] Safe offline candidate evolution using analysed identity.
20. [ ] Magic variation/history UI and audition workflow.
21. [ ] Sequencer-aware evolution for rhythmic directions.
22. [ ] Add remaining factory presets until the reviewed library exceeds 300 patches.

### Phase E — polish and release gate

23. [ ] Factory preset/pack migration tests on Windows and macOS.
24. [ ] Full VST3/AU/Standalone native CI, pluginval/auval and session round-trip coverage.
25. [ ] CPU/real-time stress tests with 8 layers + FX + modulation + Sequencer.
26. [ ] Final loudness/headroom pass across all factory presets and Magic directions.
27. [ ] UX pass: no tiny controls, no clipped labels, keyboard-accessible preset/sequence browsing and clear undo/history behavior.

## Product principle

**Match is truth; Magic is transformation.** RetroMatch should first understand and reproduce the loaded sample without unwanted distortion. Compare must make predictable named corrections. Once that foundation is trustworthy, the preset library, sequencer and Magic workflow turn the sample from a static reference into a starting point for an instrument, a phrase and a complete evolving soundscape.
