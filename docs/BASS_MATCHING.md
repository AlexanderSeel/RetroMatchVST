# Bass and reference matching changes

This change addresses unwanted harmonic layers on clean tones and analysis/render
timing errors exposed by the two supplied reference recordings.

- Pitch estimation uses the first strong autocorrelation peak and fractional lag
  interpolation, with a lower detection limit of 25 Hz.
- A fundamental-dominated reference starts with a single sine and a low-resonance
  filter. It does not acquire a sub-octave, supersaw, additive layer, FM or effects
  merely because it is periodic or has mostly odd harmonics.
- Refinement explicitly tests isolated oscillator and reference-wavetable
  topologies. Alternate search steps change only the envelope and filter, allowing
  clean candidates to improve without activating unused layers.
- The natural UI variant preserves the seed. Quick Match reduces candidate count
  rather than shortening the reference's time axis to 1.5 seconds.
- File analysis skips pre-roll below 1% of the recording peak, retaining 10 ms
  before the crossing. Reference wavetable extraction also skips pre-roll and
  uses fractional cycle boundaries and a wider zero-crossing search.
- The default render limit now matches the analyzer's 12-second input limit.
  Matching long recordings costs more CPU time. File analysis still reads at most
  the first 12 seconds; it does not reconstruct an entire arbitrary-length recording.

## Regression coverage

The smoke suite checks automatic pitch and rendered sine purity at 27.5, 32.7032,
41.2034, 55, 82.4069 and 523.2511 Hz at both 44.1 and 48 kHz. A generated WAV checks
one second of pre-roll and requires more than 99.9% fundamental energy in each
extracted sine frame. A 5.25-second render checks the previous four-second cap.
Existing synthesis, oversampling, state and matcher non-regression tests remain.

To benchmark local reference files after building with `RETROMATCH_BUILD_TESTS=ON`:

```powershell
& '.\build-windows\RetroMatchTests_artefacts\Release\RetroMatchTests.exe' 'C:\path\reference.wav'
```

The optional file arguments print detected pitch, evaluated seed similarity and
default Refine similarity, then run the smoke suite. Reference recordings are not
copied into the repository. These feature scores measure the engine's objective,
not a percentage of perceptual identity; listening in a host remains necessary.

## Measured results, 2026-09-05

MSVC Release / JUCE 9.0.1, original source `28a419a` versus this working tree,
using the same two local WAV files and the default 132 iterations / 24 topology
trials. The scoring formula is unchanged. Analysis timing and default render
duration differ as described above, so these are end-to-end pipeline results,
not scores against an identical cached feature vector.

| Reference | Original seed | Updated seed | Original refined | Updated refined |
|---|---:|---:|---:|---:|
| `526252__mogigrumbles__cs-80-guitar-2-40-e2-vel-127.wav` | 69.87% | 75.09% | 78.18% | 87.26% |
| `610254__chaoticnoize__13-c5.wav` | 76.76% | 75.88% | 78.70% | 86.71% |

The seed columns are single evaluated seeds, not the UI's three-variant Quick
search. Updated detected fundamentals were 41.8214 and 1047.39 Hz respectively;
filenames were not used to override measured pitch.

The full smoke executable and static source checks passed. The existing synthetic
matcher fixture scored 79.17% at seed and 88.39% after its short refinement run.
No DAW listening/host-validation result is claimed by these automated checks.

The Windows VST3 and Standalone Release targets built successfully; CTest passed
1/1 tests. Exported binaries are in `dist/bass-matching-fix/`, separate from the
pre-existing modified files in `dist/docker-windows-release/`.
