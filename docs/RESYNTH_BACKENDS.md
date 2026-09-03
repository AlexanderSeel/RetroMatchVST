# RetroMatch resynthesis backend evaluation

RetroMatch is intended to match a broad range of musical reference sounds, not only speech. The default production backend therefore remains the built-in hybrid synth + extracted reference wavetable + deterministic local similarity scorer.

## Decision summary

| Technology | Primary use | License/distribution implication | RetroMatch decision |
| --- | --- | --- | --- |
| Native RetroMatch / JUCE | General musical resynthesis and parameter matching | Existing project licensing | **Production default** |
| WORLD (`mmorise/World`) | High-quality speech/vocal analysis and resynthesis | Modified BSD / permissive | **Candidate optional vocal backend** |
| Loris | Spectral partial analysis, synthesis and morphing | GPL-2.0 | Research/reference only unless distribution strategy changes |
| Essentia | Broad audio feature extraction and analysis | AGPLv3 | Research/reference only unless distribution strategy changes |
| Rubber Band | High-quality time stretch and pitch shift | GPL-2.0 or commercial | Not a core resynthesis matcher; only consider with suitable licensing |
| aubio | Onset/pitch/tempo analysis | GPLv3 | Research/reference only unless distribution strategy changes |

## Why the native backend remains primary

The current matcher already has the properties RetroMatch needs for general instrument matching:

1. a parameterised hybrid voice that can combine VA oscillators, additive content, wavetable/reference-wavetable layers, supersaw/unison, wavefolding and 6-op FM;
2. deterministic offline rendering so candidate quality can be compared repeatably;
3. a multi-dimensional similarity score covering spectrum, temporal behaviour, timbre, brightness, envelope, harmonic structure, pitch and stereo image;
4. direct mapping from a chosen candidate to automatable JUCE/APVTS parameters.

A spectral library can improve individual analysis stages, but replacing the whole engine with a speech vocoder or a GPL/AGPL dependency would not automatically improve general musical matching.

## WORLD backend candidate

WORLD is the most interesting external backend found for a future optional module because it is permissively licensed and actively maintained. Its strengths are fundamental-frequency, spectral-envelope and aperiodicity analysis for speech and vocal resynthesis.

That also defines its boundary: it is not a drop-in solution for drums, distorted synths, pads, guitars, dense chords, complex transients or arbitrary polyphonic reference audio.

A future adapter should therefore be explicitly scoped as **Vocal / Speech Analysis (Experimental)** and should:

- be compile-time optional;
- run only off the audio thread;
- convert WORLD analysis output into RetroMatch features and/or synthesis seeds rather than bypassing the normal scoring pipeline;
- keep the native engine available as the final editable synth representation;
- carry WORLD's license notice in binary distributions when enabled.

The SETTINGS page currently exposes this backend as a research target only. It does not claim WORLD is compiled or active.

## AI-assisted resynthesis

AI assistance is deliberately a *seed generator*, not the authority that decides whether a sound matches.

Pipeline:

`reference audio -> local feature extraction -> AI parameter proposals -> local deterministic render -> local similarity score -> A/B/C user selection`

By default RetroMatch sends only extracted numeric/audio-feature information and the current synth seed to an AI provider. The loaded audio file is not uploaded by this implementation.

Supported configuration targets:

- OpenAI Responses API;
- Google Gemini Generate Content API;
- custom OpenAI-compatible/Azure-style endpoint;
- GitHub Copilot through an explicitly configured external bridge. GitHub Models itself is not treated as an available inference backend.

Secrets are never written to RetroMatch's settings file. The user can use an environment variable or an in-memory session key.

## Next backend experiments

1. Add a compile-time `RETROMATCH_ENABLE_WORLD` adapter only after creating vocal-focused fixtures and a measurable quality benchmark.
2. Expand native analysis with transient/noise-band descriptors before adding another large dependency.
3. Compare candidate quality on a representative test corpus: mono synth lead, bass, pad, pluck, percussion, vocal vowel, noisy texture and short polyphonic chord.
4. Keep every external proposal behind the same local render + similarity evaluation, so backend comparisons remain objective.
