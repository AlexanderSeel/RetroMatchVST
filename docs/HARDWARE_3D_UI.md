# RM-01 Hardware 3D UI Kit

RetroMatch now uses a reusable JUCE-native hardware rendering kit for its standard controls.

## Goal

The visual target is the approved RM-01 product-render direction: machined dark metal, walnut cheeks,
deep mounting sockets, raised convex encoders, segmented phosphor rings, recessed panels, tactile buttons
and physically separated layers. The editor architecture and APVTS parameter wiring are unchanged.

## Implementation

`Source/UI/Hardware3DKit.h` contains reusable drawing primitives:

- `drawRotaryKnob` — deep socket, machined bezel, phosphor ring, knurling, convex face and lit pointer.
- `drawRaisedButton` — socket, cast shadow, raised face, lower extrusion and active phosphor edge.
- `drawLinearTrack` — recessed track and raised metallic thumb.
- `drawRocker` — two-position hardware rocker for JUCE ToggleButton.
- `drawRecessedPanel` — reusable inset module/display shell.
- `drawSectionPlate` — raised section-header plate.
- `drawPhosphorDisplay` — glass/phosphor display shell for future analyzer surfaces.
- `drawMachinedScrew` and `drawWalnutCheek` — chassis parts reusable by editor components.

`Source/UI/RetroLookAndFeel.h` consumes the kit for rotary sliders, text buttons, tabs, combo boxes,
horizontal sliders, toggle buttons, progress bars and popup surfaces. Existing components therefore
pick up the hardware look without changing parameter IDs or attachments.

The large reference editor and compact reference overview now reuse the same phosphor display, recessed
panel and hardware control language. The main sound-design workflow is ordered as Presets, Layers, Synth,
FM, MSEG, Filter, Mod, FX, Wavetable, Signal, MIDI Map, Melody and Settings; AI diagnostics live inside
Settings instead of consuming a separate top-level tab. MSEG also exposes a dedicated target/depth path so
enabling it produces an immediately audible result while the modulation graph remains available for extra
routes.

## Palette

The current RetroMatch palette switch is preserved:

- MINT
- AMBER
- ICE
- VIOLET

The current primary/secondary/tertiary phosphor colours are passed into the hardware primitives,
so the physical geometry stays stable while the illuminated parts change colour.

## Asset pack

A matching high-definition PNG design pack is generated alongside this implementation for mockups,
documentation and future sprite-based rendering. The production editor deliberately uses the JUCE-native
kit so it remains sharp across the existing resizable editor range and does not add raster-memory overhead.