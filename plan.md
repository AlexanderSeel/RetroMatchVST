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
- [ ] Graph edits must be undoable and serializable before arbitrary routing is enabled.

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

- [ ] Drag from output port to compatible input port to create a connection.
- [ ] Drag an existing cable endpoint to reconnect it.
- [ ] Click cable to select; Delete/right-click removes it.
- [ ] Hover highlights the complete upstream/downstream path.
- [ ] Distinct visual language for audio, modulation and clock cables.
- [ ] Quick actions: **Insert after**, **Split parallel**, **Merge**, **Disconnect**, **Restore default route**.
- [ ] Context menu on nodes: Edit, Bypass, Solo, Mute, Duplicate, Randomize, Lock position.
- [ ] Undo/redo all graph operations.

## 4. DSP routing compiler

The visual graph must create a real sonic change, not just redraw lines.

### First safe routing targets

- [ ] Reorder supported pre/post FX sections.
- [ ] Parallel filter/FX branches with explicit split/merge gain compensation.
- [ ] Move selected utility processing before/after filter and FX where DSP ownership allows it.
- [ ] Expose layer combine order/operation through the graph.
- [ ] Route MOD/MSEG/LFO outputs to supported modulation destinations from the patch map.

### Engine work

- [ ] Compile the validated graph to a lightweight immutable processing plan outside the audio thread.
- [ ] Swap processing plans atomically at block boundaries.
- [ ] Preallocate node/process buffers in `prepareToPlay` or graph-plan preparation.
- [ ] Preserve latency accounting for oversampled/nonlinear nodes.
- [ ] Add dry/wet and gain normalization around parallel branches to avoid surprise level jumps.
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
- [ ] Do not serialize temporary meters/hover/selection state.
- [x] Add graph schema versioning independent of plug-in version.
- [x] Validate and repair malformed graph state instead of crashing; invalid nodes/edges are skipped during ValueTree restore.

## 8. Verification

- [x] Static contracts for graph model, validation and append-only automation.
- [x] Unit tests for allowed/forbidden connections.
- [x] Topological-sort and cycle-detection tests.
- [ ] DSP tests proving routing changes produce different but finite/non-silent output.
- [ ] Parallel split/merge loudness regression tests.
- [x] Session round-trip tests for typed graph + custom node layout/view state.
- [ ] Stress test continuous graph editing while audio is running.
- [ ] VST3/AU/Standalone builds and pluginval/auval coverage.

## Acceptance for the semi-modular milestone

A user can rearrange the patch visually, create/reconnect/remove only valid cables, build serial and parallel signal paths, route modulation from the graph, hear the changed topology immediately, undo the operation, save the patch, reload it and obtain the same routing and sound. Invalid or unstable graphs are rejected before they reach the audio thread.

## Implementation order

1. **Canvas interaction** — node drag, grid, zoom controls, fit/auto arrange. *(started on main)*
2. **Persistent graph data model** — stable IDs, typed ports, edge serialization, validation. *(implemented; typed schema v1 persisted in session/preset state)*
3. **Cable editor** — connection creation/reconnection/deletion + undo.
4. **DSP compiler v1** — safe serial/parallel audio routing and layer combine topology.
5. **Modulation cable routing** — expose supported destinations through the graph.
6. **Sound-design utilities/macros/scenes**.
7. **Resynthesis topology search**.

The implementation should favor a small number of musically useful, deterministic routing primitives over a fully unrestricted modular environment. RetroMatch should remain fast enough for live use and understandable enough that a matched patch can still be edited like an instrument rather than debugged like a graph program.


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
