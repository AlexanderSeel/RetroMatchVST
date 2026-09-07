# Instance workflow

Layers opens first; Presets is beside it. Select **Edit instance** in the rack or use the persistent instance selector above the tabs. The instance number and its color remain visible while editing oscillators, FM, filters, modulation, wavetables and effects. Changes are saved to that instance automatically when switching or closing the editor. Saving a patch while editing another instance preserves the main synth and the edited layer.

**Add current synth instance** duplicates the selected sound. Each additional instance has level, pan, tuning, combination mode and amount controls. Instances combine in rack order, starting with the main synth:

| Mode | Result at full amount |
| --- | --- |
| Add | Accumulated signal plus this instance |
| Mix | Crossfade from the accumulated signal to this instance |
| Subtract | Accumulated signal minus this instance |
| Multiply | Product of the two signals (ring modulation) |
| Divide | Bounded, regularised division: `tanh(a*b / (b*b + 0.01))` |

Amount zero bypasses the operation; intermediate values blend between the accumulated signal and the result. Level and pan scale the incoming instance before combination. Disabling an instance bypasses it entirely. Existing patches retain additive mixing.

The factory library contains 110 presets across ten types, with 50 new patches using two to four instances. Search by name or type, filter by category, and double-click or press Load Selected. Factory entries are grouped by type and sorted by name; saved user patches are available under User.

The typing keyboard works across the focused editor, including while using its controls. `A W S E D F T G Y H U J K O L P ;` play consecutive semitones. Choose the starting C using **Typing octave** beside the piano. Text fields retain normal typing, shortcut modifiers suppress notes, and changing octave or losing editor focus releases held notes. The octave choice is stored with the patch/session. A host can still reserve keys before forwarding them to a plug-in.

The reference waveform shows the full file. Drag either blue handle to set start or end; releasing applies the region. Numeric start/end fields remain available for precise edits. Handles cannot cross, and analysis runs on release rather than during every mouse movement.
