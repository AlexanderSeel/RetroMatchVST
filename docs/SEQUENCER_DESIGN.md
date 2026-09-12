# Sequencer and Melody design

RetroMatch treats the two time-based tools as different instruments:

- **Sequencer / Arp** is a note-gated sound-design modulator. A played note or chord starts the phrase, and the phrase drives pitch, gate, velocity and mapped timbre lanes. It is not a self-running MIDI player by default.
- **Melody Lab** is an arrangement/transcription editor. It owns a finite note clip that can be corrected, imported, exported or dragged to a DAW.

Useful sequencer patterns include:

- a quarter-note bass ostinato with a slower pad chord trigger;
- filter, wavetable and amplitude motion on a held pad or evolving atmosphere;
- ratcheted plucks and probability variations for generative rhythmic texture;
- pitch ramps plus rising filter/wavetable lanes for risers and transitions;
- polymetric macro lanes for cinematic pulses, drones and granular-feeling movement;
- chord-triggered arpeggios where the performer supplies the harmony and the patch supplies the rhythm.

These uses follow established instrument workflows: arpeggiators transform a held note or chord into a rhythmical pattern, chord-trigger modes preserve the input harmony, and tempo-synced rate/gate/swing controls make the phrase musical. Parameter sequencing is especially valuable when it is visible as automation lanes rather than hidden behind unrelated knobs. See the [Ableton Live MIDI Effect reference](https://www.ableton.com/en/manual/live-midi-effect-reference/) and its [Arpeggiate transformation reference](https://www.ableton.com/en/live-manual/12/midi-tools/) for the reference interaction model.

## Product decisions

1. The step editor uses a graphical grid: pitch is shown as a point lane, velocity/gate as bars, and both macro lanes as independent envelopes. Dragging directly edits the lane under the pointer.
2. Sequencer scope is a first-class state concept: Global, Main Instance and Layer Instance. The current transport is global; per-instance DSP transport and parameter application remain an explicit Phase F closure item.
3. Factory examples should demonstrate note-gated bass, chord-pad, atmosphere, riser and generative motion. They should not start producing sound on an empty keyboard unless a user explicitly selects free-run.
4. Melody editing is intentionally separate: double-click adds a note, dragging moves pitch/time, DELETE removes it, MIDI import replaces the clip, and export/drag-out writes a standard MIDI file.
