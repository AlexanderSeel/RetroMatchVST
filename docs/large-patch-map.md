# Large Patch Map

`SIGNAL LAB > BIG` opens the patch map in a separate resizable window. The large view uses the same DSP-backed graph and editing rules as the embedded Patch Map; it is not a second routing state.

The large window always keeps its complete toolbar visible. `UNDO`/`REDO` restore supported graph edits, `AUTO` restores the canonical layout, `FIT` fits the current graph to the visible viewport, `-` / `100%` / `+` control zoom, and `GRID` toggles snap-to-grid. Resizing the large window automatically re-fits the graph so nodes and controls do not remain outside the visible content area.

The overlay follows the plug-in editor scale on HiDPI displays. Its dedicated dialog window exits the modal state through the title-bar close button and remains auto-owned by JUCE, so closing BIG cleanly tears down the large-map content instead of leaving modal/window lifetime ambiguous.

Audio, modulation, and clock cables retain the same validation and editing behavior as the embedded view; unsupported fixed audio topology cannot be disconnected until the DSP routing compiler owns that topology.
