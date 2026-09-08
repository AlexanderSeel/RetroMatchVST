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
        laf.setPalette (proc.lightPalette.load());
        setLookAndFeel (&laf);
        setOpaque (true);

        for (auto* b : { &candidateA, &candidateB, &candidateC, &synth, &reference, &mix, &stop })
            addAndMakeVisible (*b);

        candidateA.setButtonText ("A"); candidateB.setButtonText ("B"); candidateC.setButtonText ("C");
        synth.setButtonText ("SYNTH"); reference.setButtonText ("REFERENCE"); mix.setButtonText ("MIX"); stop.setButtonText ("STOP");

        for (auto* b : { &candidateA, &candidateB, &candidateC })
        {
            b->setClickingTogglesState (true);
            b->setRadioGroupId (0x524d43);
        }

        candidateA.onClick = [this] { select (0); };
        candidateB.onClick = [this] { select (1); };
        candidateC.onClick = [this] { select (2); };
        synth.onClick = [this] { audition (RetroMatchSynthAudioProcessor::ReferenceAuditionMode::synthOnly); };
        reference.onClick = [this] { audition (RetroMatchSynthAudioProcessor::ReferenceAuditionMode::referenceOnly); };
        mix.onClick = [this] { audition (RetroMatchSynthAudioProcessor::ReferenceAuditionMode::mixed); };
        stop.onClick = [this]
        {
            proc.allEditorNotesOff();
            proc.setReferenceAuditionMode (RetroMatchSynthAudioProcessor::ReferenceAuditionMode::synthOnly);
            repaint();
        };

        syncButtonState();
        startTimerHz (12);
    }

    ~MatchCompareDialog() override
    {
        proc.allEditorNotesOff();
        setLookAndFeel (nullptr);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (18);
        r.removeFromTop (48);

        auto top = r.removeFromTop (38);
        candidateA.setBounds (top.removeFromLeft (58).reduced (2));
        candidateB.setBounds (top.removeFromLeft (58).reduced (2));
        candidateC.setBounds (top.removeFromLeft (58).reduced (2));
        top.removeFromLeft (18);
        synth.setBounds (top.removeFromLeft (104).reduced (2));
        reference.setBounds (top.removeFromLeft (126).reduced (2));
        mix.setBounds (top.removeFromLeft (84).reduced (2));
        stop.setBounds (top.removeFromLeft (76).reduced (2));
    }

    void paint (juce::Graphics& g) override
    {
        laf.setPalette (proc.lightPalette.load());
        const auto led = laf.findColour (RetroLookAndFeel::primaryLed);
        const auto gold = laf.findColour (RetroLookAndFeel::secondaryLed);
        const auto cyan = laf.findColour (RetroLookAndFeel::tertiaryLed);
        const RetroHardware3D::Palette palette { led, gold, cyan };

        // Brighter than the deeply recessed editor pages by design: comparison
        // traces, labels and metrics must remain legible on low-brightness displays.
        g.fillAll (juce::Colour (0xff111719));
        auto outer = getLocalBounds().toFloat().reduced (6.0f);
        RetroHardware3D::drawRecessedPanel (g, outer, palette, 10.0f);

        auto face = outer.reduced (8.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff273033), face.getTopLeft(),
                                                 juce::Colour (0xff171f22), face.getBottomLeft(), false));
        g.fillRoundedRectangle (face, 7.0f);
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.drawRoundedRectangle (face.reduced (1.0f), 7.0f, 1.0f);

        g.setColour (gold);
        g.setFont (juce::Font (juce::FontOptions (18.0f, juce::Font::bold)));
        g.drawText ("REFERENCE  VS  RESYNTH VISUAL COMPARE", 24, 14, getWidth() - 48, 24,
                    juce::Justification::centredLeft);

        const juce::StringArray methods { "Balanced Hybrid", "Reference Wavetable", "Spectral Subtractive",
                                           "FM / Harmonic", "Layered Studio", "Texture / Chop", "FX / Guitar Chain" };
        const juce::StringArray depths { "Classic / 1-3", "Studio / 4", "Deep / 6", "Maximum / 8" };
        const int methodIndex = juce::jlimit (0, methods.size() - 1, (int) std::lround (proc.apvts.getRawParameterValue ("resynthStrategy")->load()));
        const int depthIndex = juce::jlimit (0, depths.size() - 1, (int) std::lround (proc.apvts.getRawParameterValue ("resynthComplexity")->load()));
        g.setColour (juce::Colour (0xffc9d6d1));
        g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
        g.drawText ("METHOD  " + methods[methodIndex] + "    /    DEPTH  " + depths[depthIndex],
                    24, 38, getWidth() - 360, 16, juce::Justification::centredLeft, true);

        auto legend = juce::Rectangle<float> ((float) getWidth() - 300.0f, 18.0f, 270.0f, 22.0f);
        drawLegend (g, legend.removeFromLeft (125.0f), led, "REFERENCE");
        drawLegend (g, legend, gold, "RESYNTH");

        if (! proc.currentFeatures)
        {
            g.setColour (juce::Colour (0xffd8e2de));
            g.setFont (juce::Font (juce::FontOptions (16.0f, juce::Font::bold)));
            g.drawText ("Load and analyze a reference first.", getLocalBounds(), juce::Justification::centred);
            return;
        }

        const auto& ref = *proc.currentFeatures;
        const SoundFeatures* candidate = proc.currentCandidateFeatures ? &*proc.currentCandidateFeatures : nullptr;
        auto body = getLocalBounds().reduced (22);
        body.removeFromTop (88);

        auto waveArea = body.removeFromTop (body.getHeight() * 34 / 100).toFloat().reduced (3.0f);
        auto spectrumArea = body.removeFromTop (body.getHeight() * 52 / 100).toFloat().reduced (3.0f);
        auto metricsArea = body.toFloat().reduced (3.0f);

        panel (g, waveArea, "WAVEFORM / ENVELOPE", led);
        panel (g, spectrumArea, "SPECTRAL FINGERPRINT", gold);
        panel (g, metricsArea, "SIMILARITY", cyan);

        auto wavePlot = waveArea.reduced (12.0f, 28.0f);
        drawPlotGrid (g, wavePlot);
        drawWave (g, wavePlot, ref, led, 2.0f);
        if (candidate) drawWave (g, wavePlot, *candidate, gold, 1.7f);

        auto spectrumPlot = spectrumArea.reduced (12.0f, 28.0f);
        drawPlotGrid (g, spectrumPlot);
        drawSpectrum (g, spectrumPlot, ref, candidate, led, gold);
        drawMetrics (g, metricsArea.reduced (12.0f, 25.0f), led, gold);
    }

private:
    RetroMatchSynthAudioProcessor& proc;
    RetroLookAndFeel laf;
    juce::TextButton candidateA, candidateB, candidateC, synth, reference, mix, stop;

    void timerCallback() override
    {
        laf.setPalette (proc.lightPalette.load());
        syncButtonState();
        repaint();
    }

    void syncButtonState()
    {
        candidateA.setToggleState (proc.selectedCandidate == 0, juce::dontSendNotification);
        candidateB.setToggleState (proc.selectedCandidate == 1, juce::dontSendNotification);
        candidateC.setToggleState (proc.selectedCandidate == 2, juce::dontSendNotification);
    }

    void select (int index)
    {
        if (proc.selectCandidate (index))
        {
            syncButtonState();
            repaint();
        }
    }

    void audition (RetroMatchSynthAudioProcessor::ReferenceAuditionMode mode)
    {
        proc.allEditorNotesOff();
        proc.setReferenceAuditionMode (mode);
        proc.noteOnFromEditor (proc.getReferenceBaseMidiNote(), 0.78f);
        repaint();
    }

    static void drawLegend (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour colour, const juce::String& text)
    {
        const auto dot = r.removeFromLeft (15.0f).withSizeKeepingCentre (8.0f, 8.0f);
        g.setColour (colour.withAlpha (0.20f));
        g.fillEllipse (dot.expanded (4.0f));
        g.setColour (colour);
        g.fillEllipse (dot);
        g.setColour (juce::Colour (0xffe2e9e6));
        g.setFont (juce::Font (juce::FontOptions (9.0f, juce::Font::bold)));
        g.drawText (text, r, juce::Justification::centredLeft);
    }

    static void panel (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title, juce::Colour accent)
    {
        g.setColour (juce::Colour (0xaa000000));
        g.fillRoundedRectangle (r.translated (0.0f, 2.0f), 8.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff172326), r.getTopLeft(),
                                                 juce::Colour (0xff0d1719), r.getBottomLeft(), false));
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (juce::Colour (0xff647679));
        g.drawRoundedRectangle (r, 8.0f, 1.2f);
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.drawLine (r.getX() + 7.0f, r.getY() + 1.0f, r.getRight() - 7.0f, r.getY() + 1.0f, 1.0f);
        g.setColour (accent);
        g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
        g.drawText (title, r.withHeight (24.0f).reduced (9.0f, 0.0f), juce::Justification::centredLeft);
    }

    static void drawPlotGrid (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setColour (juce::Colour (0xff0a1113));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (juce::Colour (0xff31464a).withAlpha (0.62f));
        for (int i = 1; i < 8; ++i)
        {
            const float x = r.getX() + r.getWidth() * (float) i / 8.0f;
            g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
        }
        for (int i = 1; i < 4; ++i)
        {
            const float y = r.getY() + r.getHeight() * (float) i / 4.0f;
            g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
        }
        g.setColour (juce::Colour (0xff839497).withAlpha (0.40f));
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
    }

    static void drawWave (juce::Graphics& g, juce::Rectangle<float> r, const SoundFeatures& f,
                          juce::Colour colour, float width)
    {
        if (f.waveformPreview.empty()) return;
        juce::Path path;
        for (size_t i = 0; i < f.waveformPreview.size(); ++i)
        {
            const float x = r.getX() + (float) i / (float) juce::jmax<size_t> (1, f.waveformPreview.size() - 1) * r.getWidth();
            const float y = r.getCentreY() - f.waveformPreview[i] * r.getHeight() * 0.43f;
            if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
        }

        g.setColour (juce::Colours::black.withAlpha (0.75f));
        g.strokePath (path, juce::PathStrokeType (width + 4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (colour.withAlpha (0.30f));
        g.strokePath (path, juce::PathStrokeType (width + 7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (colour);
        g.strokePath (path, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    static void drawSpectrum (juce::Graphics& g, juce::Rectangle<float> r, const SoundFeatures& ref,
                              const SoundFeatures* candidate, juce::Colour a, juce::Colour b)
    {
        const float w = r.getWidth() / SoundFeatures::spectralBandCount;
        for (int i = 0; i < SoundFeatures::spectralBandCount; ++i)
        {
            const float x = r.getX() + i * w;
            const float rh = juce::jlimit (0.0f, 1.0f, ref.spectralBands[(size_t) i]) * r.getHeight();
            g.setColour (a.withAlpha (0.72f));
            g.fillRect (x, r.getBottom() - rh, juce::jmax (1.0f, w * 0.42f), rh);

            if (candidate)
            {
                const float ch = juce::jlimit (0.0f, 1.0f, candidate->spectralBands[(size_t) i]) * r.getHeight();
                g.setColour (b.withAlpha (0.84f));
                g.fillRect (x + w * 0.47f, r.getBottom() - ch, juce::jmax (1.0f, w * 0.42f), ch);
            }
        }
    }

    void drawMetrics (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour led, juce::Colour gold)
    {
        const auto& s = proc.lastMatch.similarity;
        const bool hasFxProbe = proc.lastMatch.effectProbeSimilarity >= 0.0f;
        const std::array<std::pair<const char*, float>, 8> values {{
            { "TOTAL", s.total }, { "SPECTRUM", s.spectrum }, { "TIMBRE", s.timbre }, { "TEMPORAL", s.temporal },
            { "HARMONIC", s.harmonic }, { "ENVELOPE", s.envelope }, { "STEREO", s.stereo },
            { hasFxProbe ? "FX PROBE" : "PITCH", hasFxProbe ? proc.lastMatch.effectProbeSimilarity : s.pitch }
        }};

        const int columns = 4;
        const float cw = r.getWidth() / columns;
        const float rh = r.getHeight() / 2.0f;
        for (int i = 0; i < (int) values.size(); ++i)
        {
            auto cell = juce::Rectangle<float> (r.getX() + (i % columns) * cw,
                                                r.getY() + (i / columns) * rh,
                                                cw, rh).reduced (7.0f, 5.0f);
            const auto accent = i == 0 ? gold : led;
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff253236), cell.getTopLeft(),
                                                     juce::Colour (0xff172326), cell.getBottomLeft(), false));
            g.fillRoundedRectangle (cell, 5.0f);
            g.setColour (juce::Colour (0xff53666a));
            g.drawRoundedRectangle (cell, 5.0f, 1.0f);

            auto content = cell.reduced (10.0f, 6.0f);
            auto titleArea = content.removeFromTop (18.0f);
            g.setColour (juce::Colour (0xffd4dfdb));
            g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
            g.drawText (values[(size_t) i].first, titleArea, juce::Justification::centredLeft);

            auto bar = content.removeFromBottom (8.0f);
            auto valueArea = content;
            g.setColour (accent);
            g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
            g.drawText (juce::String (values[(size_t) i].second * 100.0f, 1) + "%", valueArea,
                        juce::Justification::centredRight);

            g.setColour (juce::Colour (0xff0b1214));
            g.fillRoundedRectangle (bar, 3.0f);
            g.setColour (accent);
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, values[(size_t) i].second)), 3.0f);
        }
    }
};
