# RetroMatch implementation plan

## Current milestone: illuminated hardware + melody workspace

- [x] Create an original scalable RetroMatch logo and application icon.
- [x] Add machined chassis depth, recessed glass displays and coherent glowing accents.
- [x] Add a per-instance light-color switch that survives session restoration.
- [x] Add real output oscilloscope, spectrum and stereo visualization without FFT work on the audio thread.
- [x] Add a graphical synthesis signal-flow view driven by patch settings.
- [x] Analyze a reference into timed notes in a cancellable background worker.
- [x] Offer predominant-melody and experimental layered-note transcription.
- [x] Display an interactive piano roll and replay the extracted notes through the current synth patch.
- [x] Export a standard MIDI file with tempo metadata and drag it to a compatible DAW.
- [x] Preserve extracted notes in session state; handle stop, replacement and editor closure safely.
- [x] Test synthetic melodies/chords, MIDI timing/export and playback lifecycle.
- [x] Build Windows VST3/Standalone, inspect the UI and record verification results.

## Scope and acceptance

All analysis is local. MIDI is a sequence of editable notes, paired with the
current editable synth patch for replay; a MIDI file does not embed the synth.
Tempo is user supplied for the MIDI time grid, not claimed to be automatically
detected. Preserve recording timing without automatic quantization. Transcription
of a mixed recording is an estimate: drums, overlapping harmonics and effects can
produce missed/extra notes. Expose confidence and let users inspect results before
export. Limit analysis to 60 seconds and 4096 notes to keep work bounded.

Keep prior bass-matching fixes and existing parameter IDs intact. Build into a new
distribution folder so earlier binaries remain available.
