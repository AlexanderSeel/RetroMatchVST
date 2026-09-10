# RetroMatch gain-staging contract

This is the level contract for generated patches and offline renders. Musical
similarity is measured separately from playback gain; a candidate must remain
finite and headroom-safe before its similarity can be trusted.

## Signal order

1. Each voice renders its oscillators, envelopes and modulation into its local
   dry buffer.
2. The local pre-FX module rack and built-in effects run. `outputGainDb` is
   applied once at the end of this per-instance FX stage.
3. The main instance is scaled by `mainLayerGain`.
4. Companion instances are rendered independently, then scaled by their
   `layerGain` and stereo `layerPan`, and mixed using their bounded
   `layerAmount` and `layerOperation`.
5. `globalFxModules` runs exactly once after all main/companion layers have
   been combined. It is never copied into a companion instance.

## Invariants

- A single voice keeps its authored level; generated additive racks use
  `GeneratedRackGainPolicy` to keep coherent main-plus-companion contribution
  at or below the default `0.92` budget.
- Serial and parallel FX use the same dry source. Parallel branches are
  combined with bounded `0.5 + 0.5` compensation, preventing a dry/dry split
  from creating a +6 dB jump.
- Compare controls do not alter drive, wavefold, distortion or FX state.
- Offline safety telemetry rejects non-finite/empty renders and hard clipping;
  true-peak estimation is exposed separately for diagnostics.

The corresponding deterministic checks live in `PhaseASafetyTests` and
`RenderTelemetryTests`. Any new summing point or branch must add a focused
level/headroom regression before it is used by matching or factory content.
