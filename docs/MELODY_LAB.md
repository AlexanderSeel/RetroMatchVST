# Melody Lab and illuminated UI

The **LED** button cycles Mint, Amber, Ice and Violet. Its setting is per
plug-in instance and survives DAW session restoration. The original vector RM
signal mark lives in `Assets/retromatch-mark.svg`; `scripts/create-logo.py` rebuilds
the SVG and the application icon from shared geometry.

**SIGNAL** displays the actual post-mix left/right output, a logarithmic spectrum
and a mid/side stereo trace. The lower diagram shows current synthesis mix levels
and effect settings. Audio moves through a bounded single-producer/single-consumer
FIFO; FFT work runs on the editor timer. When the UI falls behind, visual samples
are dropped without holding up audio.

## Sample → patch + notes → DAW

1. Load a reference recording. Use Quick/Refine to develop the synth patch.
2. Open **MELODY**, choose **MELODY** for one predominant line or **LAYERED /
   EXPERIMENTAL** for up to four simultaneous pitches, then press **ANALYZE**.
3. Inspect the piano roll. Click a note and use **NOTE -**, **NOTE +** or **DELETE
   NOTE** to correct its pitch or remove it.
4. **PLAY** replays the extracted notes through the current synth patch; **STOP**
   releases audition notes. Playback uses the original recording's timing.
5. Set the BPM used for the piano-roll grid and MIDI tempo metadata. Changing BPM
   preserves note times in seconds. Export uses 960 ticks per quarter note.
6. Use **EXPORT MIDI**, or press and drag **DRAG MIDI TO DAW** onto a MIDI track in
   a host that accepts external file drops. Put RetroMatch on that track to hear
   the same patch. Export remains available where host drag/drop is unavailable.
7. **SAVE PATCH** and DAW state include the extracted notes with the synth settings.
   A `.mid` file itself carries notes and tempo; distribute the patch with it when
   sharing the sound as well as the performance.

Drag exports are ordinary `.mid` files retained under the system temporary
directory's `RetroMatch-MIDI` folder so a DAW can finish importing after the drag.

## Analysis boundaries

Analysis is local, cancellable and limited to 60 seconds / 4096 notes. It uses a
4096-point Hann spectrum at 22.05 kHz with an approximately 10 ms hop, harmonic
salience, harmonic suppression for layered extraction, short-window silence
gating and note tracking. The analyzed pitch range is MIDI 28–96. Notes shorter
than about 65 ms are discarded. The FFT window limits separation of very fast
ornaments even though the event grid is finer.

Layered extraction is an estimate, especially with drums, dense chords, unison,
effects or shared harmonics. Confidence describes concentration of energy near
the estimated harmonics; it is not a calibrated probability of a correct note.
The BPM control supplies the tempo; automatic beat detection, expressive pitch
bends and source separation are future work. Review/correct MIDI in the DAW for
demanding arrangements.

## Verification tools

`RetroMatchTests` covers known melodies and chords, silence/cancellation, clip
state, MIDI round-trip timing and note pairing, playback start/stop/end timing,
and the visual FIFO, alongside the existing DSP/matcher regressions.

Configure with `RETROMATCH_BUILD_UI_TESTS=ON` to build `RetroMatchEditorPreview`.
Run it with an output directory to generate screenshots of the actual editor at
normal and minimum sizes, and check session restoration / playback on editor
closure. This tool creates an offscreen editor and needs desktop graphics/font
support; it need not display a window.
