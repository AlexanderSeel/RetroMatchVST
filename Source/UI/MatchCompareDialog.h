#pragma once
#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "RetroLookAndFeel.h"
#include "Hardware3DKit.h"

class MatchCompareDialog final : public juce::Component, private juce::Timer
{
public:
    explicit MatchCompareDialog (RetroMatchSynthAudioProcessor& p) : proc (p)
    {
        for (auto* b : { &candidateA, &candidateB, &candidateC, &synth, &reference, &mix, &stop }) addAndMakeVisible (*b);
        candidateA.setButtonText ("A"); candidateB.setButtonText ("B"); candidateC.setButtonText ("C");
        synth.setButtonText ("SYNTH"); reference.setButtonText ("REFERENCE"); mix.setButtonText ("MIX"); stop.setButtonText ("STOP");
        candidateA.onClick = [this] { select (0); }; candidateB.onClick = [this] { select (1); }; candidateC.onClick = [this] { select (2); };
        synth.onClick = [this] { audition (RetroMatchSynthAudioProcessor::ReferenceAuditionMode::synthOnly); };
        reference.onClick = [this] { audition (RetroMatchSynthAudioProcessor::ReferenceAuditionMode::referenceOnly); };
        mix.onClick = [this] { audition (RetroMatchSynthAudioProcessor::ReferenceAuditionMode::mixed); };
        stop.onClick = [this] { proc.allEditorNotesOff(); proc.setReferenceAuditionMode (RetroMatchSynthAudioProcessor::ReferenceAuditionMode::synthOnly); };
        startTimerHz (12);
    }
    ~MatchCompareDialog() override { proc.allEditorNotesOff(); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (18); r.removeFromTop (42);
        auto top = r.removeFromTop (34); candidateA.setBounds (top.removeFromLeft (56).reduced (2)); candidateB.setBounds (top.removeFromLeft (56).reduced (2)); candidateC.setBounds (top.removeFromLeft (56).reduced (2));
        top.removeFromLeft (16); synth.setBounds (top.removeFromLeft (100).reduced (2)); reference.setBounds (top.removeFromLeft (120).reduced (2)); mix.setBounds (top.removeFromLeft (80).reduced (2)); stop.setBounds (top.removeFromLeft (70).reduced (2));
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff050809));
        const auto led = findColour (RetroLookAndFeel::primaryLed);
        const auto gold = findColour (RetroLookAndFeel::secondaryLed);
        const RetroHardware3D::Palette palette { led, gold, findColour (RetroLookAndFeel::tertiaryLed) };
        auto frame = getLocalBounds().toFloat().reduced (8);
        RetroHardware3D::drawRecessedPanel (g, frame, palette, 10.0f);
        g.setColour (gold); g.setFont (juce::Font (juce::FontOptions (18.0f, juce::Font::bold)));
        g.drawText ("REFERENCE  ↔  RESYNTH VISUAL COMPARE", 24, 16, getWidth() - 48, 26, juce::Justification::centredLeft);

        if (! proc.currentFeatures)
        { g.setColour (led.withAlpha (0.7f)); g.setFont (16.0f); g.drawText ("Load and analyze a reference first.", getLocalBounds(), juce::Justification::centred); return; }
        const auto& ref = *proc.currentFeatures;
        const SoundFeatures* candidate = proc.currentCandidateFeatures ? &*proc.currentCandidateFeatures : nullptr;
        auto body = getLocalBounds().reduced (22); body.removeFromTop (82);
        auto waveArea = body.removeFromTop (body.getHeight() * 34 / 100).toFloat().reduced (3);
        auto spectrumArea = body.removeFromTop (body.getHeight() * 52 / 100).toFloat().reduced (3);
        auto metricsArea = body.toFloat().reduced (3);
        panel (g, waveArea, "WAVEFORM / ENVELOPE", led); panel (g, spectrumArea, "SPECTRAL FINGERPRINT", gold); panel (g, metricsArea, "SIMILARITY", led);
        drawWave (g, waveArea.reduced (12, 26), ref, led, 1.5f);
        if (candidate) drawWave (g, waveArea.reduced (12, 26), *candidate, gold, 1.15f);
        drawSpectrum (g, spectrumArea.reduced (12, 26), ref, candidate, led, gold);
        drawMetrics (g, metricsArea.reduced (12, 24), led, gold);
    }

private:
    RetroMatchSynthAudioProcessor& proc;
    juce::TextButton candidateA, candidateB, candidateC, synth, reference, mix, stop;

    void timerCallback() override { repaint(); }
    void select (int index) { if (proc.selectCandidate (index)) repaint(); }
    void audition (RetroMatchSynthAudioProcessor::ReferenceAuditionMode mode)
    {
        proc.allEditorNotesOff(); proc.setReferenceAuditionMode (mode);
        proc.noteOnFromEditor (proc.getReferenceBaseMidiNote(), 0.78f);
    }
    static void panel (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title, juce::Colour accent)
    {
        g.setColour (juce::Colour (0xff071012)); g.fillRoundedRectangle (r, 8);
        g.setColour (juce::Colour (0xff445457)); g.drawRoundedRectangle (r, 8, 1);
        g.setColour (accent); g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
        g.drawText (title, r.withHeight (22).reduced (8, 0), juce::Justification::centredLeft);
    }
    static void drawWave (juce::Graphics& g, juce::Rectangle<float> r, const SoundFeatures& f, juce::Colour colour, float width)
    {
        if (f.waveformPreview.empty()) return;
        juce::Path path;
        for (size_t i = 0; i < f.waveformPreview.size(); ++i)
        {
            const float x = r.getX() + (float) i / (float) juce::jmax<size_t> (1, f.waveformPreview.size() - 1) * r.getWidth();
            const float y = r.getCentreY() - f.waveformPreview[i] * r.getHeight() * 0.45f;
            if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
        }
        g.setColour (colour.withAlpha (0.15f)); g.strokePath (path, juce::PathStrokeType (width + 6));
        g.setColour (colour); g.strokePath (path, juce::PathStrokeType (width));
    }
    static void drawSpectrum (juce::Graphics& g, juce::Rectangle<float> r, const SoundFeatures& ref, const SoundFeatures* candidate, juce::Colour a, juce::Colour b)
    {
        const float w = r.getWidth() / SoundFeatures::spectralBandCount;
        for (int i = 0; i < SoundFeatures::spectralBandCount; ++i)
        {
            const float x = r.getX() + i * w;
            const float rh = juce::jlimit (0.0f, 1.0f, ref.spectralBands[(size_t) i]) * r.getHeight();
            g.setColour (a.withAlpha (0.48f)); g.fillRect (x, r.getBottom() - rh, juce::jmax (1.0f, w * 0.44f), rh);
            if (candidate)
            {
                const float ch = juce::jlimit (0.0f, 1.0f, candidate->spectralBands[(size_t) i]) * r.getHeight();
                g.setColour (b.withAlpha (0.62f)); g.fillRect (x + w * 0.48f, r.getBottom() - ch, juce::jmax (1.0f, w * 0.44f), ch);
            }
        }
    }
    void drawMetrics (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour led, juce::Colour gold)
    {
        const auto& s = proc.lastMatch.similarity;
        const std::array<std::pair<const char*, float>, 8> values {{
            { "TOTAL", s.total }, { "SPECTRUM", s.spectrum }, { "TIMBRE", s.timbre }, { "TEMPORAL", s.temporal },
            { "HARMONIC", s.harmonic }, { "ENVELOPE", s.envelope }, { "PITCH", s.pitch }, { "STEREO", s.stereo }
        }};
        const int columns = 4; const float cw = r.getWidth() / columns; const float rh = r.getHeight() / 2.0f;
        for (int i = 0; i < (int) values.size(); ++i)
        {
            auto cell = juce::Rectangle<float> (r.getX() + (i % columns) * cw, r.getY() + (i / columns) * rh, cw, rh).reduced (7, 5);
            g.setColour (juce::Colour (0xff142023)); g.fillRoundedRectangle (cell, 5);
            g.setColour (i == 0 ? gold : led); g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
            g.drawText (values[(size_t) i].first, cell.removeFromTop (18), juce::Justification::centredLeft);
            auto bar = cell.removeFromBottom (8); g.setColour (juce::Colour (0xff263235)); g.fillRoundedRectangle (bar, 3);
            g.setColour (i == 0 ? gold : led); g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, values[(size_t) i].second)), 3);
            g.drawText (juce::String (values[(size_t) i].second * 100.0f, 1) + "%", cell, juce::Justification::centredRight);
        }
    }
};
