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
