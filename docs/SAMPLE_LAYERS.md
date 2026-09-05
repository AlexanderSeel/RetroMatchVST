# Sample selections, playback, distortion and layers

Load a reference recording. The reference workspace now has **START** and **END**
fields in seconds. Set both and click **APPLY** to use that region for synthesis
analysis and matching. The Melody page's synthesis region follows the same selection;
its MIDI analysis region remains independent.

Click **CREATE WAVETABLE FROM SELECTION** to turn the selected sample part into a
five-frame user wavetable. The selection needs audible material and at least two
cycles at the reference base pitch. Correct the base note first if necessary.
Extraction reads at most the first six seconds of the selection. It leaves the
previous table intact if extraction fails. On **WAVETABLE**, adjust **USER WT MIX**
and **WT POSITION** to blend and scan the result. Lower other oscillator mixes to
hear the table on its own. Tables are embedded in presets and sessions.

On **MELODY**, analyze the recording and click **PLAY MELODY** to play the extracted
MIDI notes through the current synth and enabled layers. **PLAY SAMPLE** auditions
the original recording instead. **STOP** releases playback. MIDI exported to a DAW
also plays all enabled layers when routed to this RetroMatch instance.

On **FX**, the **DISTORTION** section offers soft saturation, hard clipping and sine
folding. **DRIVE** sets the amount and **DIST MIX** blends wet and dry. Drive at zero
bypasses distortion. The existing oversampling setting also covers these modes.
Old patches retain the original soft saturation behavior.

On **LAYERS**, layer 1 is the live synth patch. Layers 2 and 3 store independent
copies, including their oscillators, envelopes, effects and wavetables:

1. Match or design a patch for one part of the reference.
2. Use **COPY CURRENT** on layer 2 to keep that patch.
3. Select another sample region and match/design a complementary patch; copy it to layer 3.
4. Blend with each layer's level, pan and tuning. Use ON to mute a stored layer,
   or set the current synth level to zero to hear only the stored layers.
5. **LOAD TO EDIT** copies a stored layer into the main synth. Edit there, then
   use **COPY CURRENT** on the original slot to save those edits back.

Layers receive the same notes, so they form one instrument. They can increase output
level and CPU use; balance levels as you add them. Matcher scores describe the main
patch, not an automatic optimization of the complete layered mix. Presets and DAW
sessions save the layer bank, and WAV preview exports include enabled layers.
