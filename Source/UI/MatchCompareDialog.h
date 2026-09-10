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

        for (auto* b : { &candidateA, &candidateB, &candidateC, &synth, &reference, &mix, &stop, &baseline, &adjusted, &resetTune, &measureTune, &autoNudge, &keepTune })
            addAndMakeVisible (*b);

        candidateA.setButtonText ("A"); candidateB.setButtonText ("B"); candidateC.setButtonText ("C");
        synth.setButtonText ("SYNTH"); reference.setButtonText ("REFERENCE"); mix.setButtonText ("MIX"); stop.setButtonText ("STOP");
        baseline.setButtonText ("BASELINE"); adjusted.setButtonText ("ADJUSTED"); resetTune.setButtonText ("RESET");
        measureTune.setButtonText ("MEASURE"); autoNudge.setButtonText ("AUTO NUDGE"); keepTune.setButtonText ("KEEP / APPLY");
        baseline.setClickingTogglesState (true); adjusted.setClickingTogglesState (true);
        baseline.setRadioGroupId (0x524d46); adjusted.setRadioGroupId (0x524d46);

        baseline.setTooltip ("Audition the immutable measured candidate before post-analysis correction.");
        adjusted.setTooltip ("Audition the current post-analysis correction. This state is temporary until KEEP / APPLY.");
        resetTune.setTooltip ("Return every post-analysis correction to neutral and restore the measured baseline.");
        measureTune.setTooltip ("Render and measure the adjusted candidate so waveform, spectrum and score reflect the actual audio.");
        autoNudge.setTooltip ("Derive a small correction from the measured residual, render it, and keep it only when the score improves.");
        keepTune.setTooltip ("Commit the current correction to the selected candidate. The original measured candidate remains the Compare baseline.");

        static constexpr const char* tuneNames[] { "BRIGHT", "LOW END", "PUNCH", "TAIL", "WIDTH", "MOTION", "FINE PITCH" };
        static constexpr const char* tuneTips[] {
            "Spectral brightness only. Moves the existing filter cutoff by up to +/-1.25 octaves; it never adds drive, FM or wavefold. Double-click = neutral.",
            "Low-frequency balance. Changes the main voice's existing sub level by up to +/-0.12; companion GOLD layers do not gain extra sub sources. Double-click = neutral.",
            "Onset punch, not loudness. Scales amp and FM attack time from x2.83 (softer) to x0.35 (harder). Double-click = neutral.",
            "Decay/release time only. Scales tail time from x0.45 to x2.22 and never raises sustain, preserving one-shot behaviour. Double-click = neutral.",
            "Stereo image width only. Scales width from x0.73 to x1.37 without changing unison detune. Double-click = neutral.",
            "Scales modulation that already exists from x0.55 to x1.80. It never invents LFO, MSEG or matrix routing on a static patch. Double-click = neutral.",
            "Whole-instrument residual tuning correction, exactly +/-50 cents. Double-click = neutral."
        };
        for (size_t i = 0; i < fineTune.size(); ++i)
        {
            addAndMakeVisible (fineTune[i]);
            addAndMakeVisible (fineTuneLabels[i]);
            fineTune[i].setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            fineTune[i].setTextBoxStyle (juce::Slider::TextBoxBelow, false, 66, 14);
            fineTune[i].setRange (-1.0, 1.0, 0.01);
            fineTune[i].setDoubleClickReturnValue (true, 0.0);
            fineTune[i].setValue (0.0, juce::dontSendNotification);
            fineTune[i].setTooltip (tuneTips[i]);
            fineTune[i].textFromValueFunction = [i] (double value)
            {
                const auto signedText = [] (double v, int decimals)
                {
                    return juce::String (v >= 0.0 ? "+" : "") + juce::String (v, decimals);
                };
                switch (i)
                {
                    case 0: return signedText (value * 1.25, 2) + " oct";
                    case 1: return signedText (value * 0.12, 2);
                    case 2: return "A x" + juce::String (std::pow (2.0, -value * 1.50), 2);
                    case 3: return "T x" + juce::String (std::pow (2.0, value * 1.15), 2);
                    case 4: return "W x" + juce::String (std::pow (2.0, value * 0.45), 2);
                    case 5: return "M x" + juce::String (std::pow (2.0, value * 0.85), 2);
                    case 6: return signedText (value * 50.0, 0) + " ct";
                    default: return juce::String (value, 2);
                }
            };
            fineTune[i].updateText();
            fineTuneLabels[i].setText (tuneNames[i], juce::dontSendNotification);
            fineTuneLabels[i].setJustificationType (juce::Justification::centred);
            fineTuneLabels[i].setColour (juce::Label::textColourId, juce::Colour (0xffb8c8c3));
            fineTuneLabels[i].setFont (juce::Font (juce::FontOptions (8.5f, juce::Font::bold)));
            fineTune[i].onValueChange = [this] { applyFineTune(); };
        }

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
        baseline.onClick = [this]
        {
            baselineAudition = true;
            proc.showCompareFineTuneBaseline (true);
            syncButtonState();
            repaint();
        };
        adjusted.onClick = [this]
        {
            baselineAudition = false;
            proc.showCompareFineTuneBaseline (false);
            syncButtonState();
            repaint();
        };
        resetTune.onClick = [this]
        {
            syncingFineTune = true;
            for (auto& slider : fineTune) slider.setValue (0.0, juce::dontSendNotification);
            syncingFineTune = false;
            baselineAudition = false;
            proc.resetCompareFineTune();
            syncButtonState();
            repaint();
        };
        measureTune.onClick = [this]
        {
            baselineAudition = false;
            startFineTuneMeasurement();
            syncButtonState();
            repaint();
        };
        autoNudge.onClick = [this] { startAutoNudge(); };
        keepTune.onClick = [this]
        {
            baselineAudition = false;
            proc.keepCompareFineTune();
            syncButtonState();
            repaint();
        };

        syncFineTuneControlsFromProcessor();
        syncButtonState();
        startTimerHz (12);
    }

    ~MatchCompareDialog() override
    {
        closing = true;
        compareMeasurePool.removeAllJobs (true, 10000);
        proc.allEditorNotesOff();
        // BASELINE/ADJUSTED are audition states. Only KEEP is allowed to survive closing Compare.
        proc.showCompareFineTuneBaseline (! proc.isCompareFineTuneApplied());
        setLookAndFeel (nullptr);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (18);
        r.removeFromTop (48);

        auto top = r.removeFromTop (42);
        candidateA.setBounds (top.removeFromLeft (58).reduced (2));
        candidateB.setBounds (top.removeFromLeft (58).reduced (2));
        candidateC.setBounds (top.removeFromLeft (58).reduced (2));
        top.removeFromLeft (18);
        synth.setBounds (top.removeFromLeft (104).reduced (2));
        reference.setBounds (top.removeFromLeft (126).reduced (2));
        mix.setBounds (top.removeFromLeft (84).reduced (2));
        stop.setBounds (top.removeFromLeft (76).reduced (2));

        auto tune = r.removeFromTop (132).reduced (10, 7);
        // Keep the POST-ANALYSIS title/status in a dedicated header band.
        tune.removeFromTop (28);
        auto tuneButtons = tune.removeFromRight (330);
        auto tuneRow1 = tuneButtons.removeFromTop (36);
        baseline.setBounds (tuneRow1.removeFromLeft (96).reduced (2));
        adjusted.setBounds (tuneRow1.removeFromLeft (96).reduced (2));
        resetTune.setBounds (tuneRow1.removeFromLeft (92).reduced (2));
        tuneButtons.removeFromTop (4);
        auto tuneRow2 = tuneButtons.removeFromTop (36);
        measureTune.setBounds (tuneRow2.removeFromLeft (86).reduced (2));
        autoNudge.setBounds (tuneRow2.removeFromLeft (104).reduced (2));
        keepTune.setBounds (tuneRow2.removeFromLeft (104).reduced (2));
        const int cell = juce::jmax (54, tune.getWidth() / (int) fineTune.size());
        for (size_t i = 0; i < fineTune.size(); ++i)
        {
            auto c = tune.removeFromLeft (i + 1 == fineTune.size() ? tune.getWidth() : cell);
            fineTuneLabels[i].setBounds (c.removeFromTop (18));
            fineTune[i].setBounds (c.reduced (3, 1));
        }
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
        const juce::String rackTag = proc.lastMatch.fullRackScore ? "    /    GOLD FULL-RACK SCORE" : juce::String {};
        g.drawText ("METHOD  " + methods[methodIndex] + "    /    DEPTH  " + depths[depthIndex] + rackTag,
                    24, 38, getWidth() - 360, 16, juce::Justification::centredLeft, true);

        auto legend = juce::Rectangle<float> ((float) getWidth() - 390.0f, 18.0f, 360.0f, 22.0f);
        drawLegend (g, legend.removeFromLeft (118.0f), led, "REFERENCE");
        drawLegend (g, legend.removeFromLeft (118.0f), gold.withAlpha (0.58f), "BASELINE");
        drawLegend (g, legend, cyan, "ADJUSTED");

        if (! proc.currentFeatures)
        {
            g.setColour (juce::Colour (0xffd8e2de));
            g.setFont (juce::Font (juce::FontOptions (16.0f, juce::Font::bold)));
            g.drawText ("Load and analyze a reference first.", getLocalBounds(), juce::Justification::centred);
            return;
        }

        const auto& ref = *proc.currentFeatures;
        const auto* baselineResult = proc.getSelectedCandidateBaseline();
        const auto* measuredAdjusted = proc.getCompareFineTuneMeasuredResult();
        const SoundFeatures* baselineFeatures = baselineResult && baselineResult->candidateFeatures.duration > 0.0f
                                              ? &baselineResult->candidateFeatures : nullptr;
        const SoundFeatures* adjustedFeatures = measuredAdjusted && measuredAdjusted->candidateFeatures.duration > 0.0f
                                              ? &measuredAdjusted->candidateFeatures : nullptr;
        auto body = getLocalBounds().reduced (22);
        auto fineTunePanel = body.withTrimmedTop (86).withHeight (132).toFloat().reduced (1.0f);
        panel (g, fineTunePanel, "POST-ANALYSIS CORRECTION", cyan);
        juce::String correctionStatus;
        if (proc.isCompareFineTunePending()) correctionStatus = "ADJUSTED AUDIO · SCORE/TRACE PENDING MEASURE";
        else if (measuredAdjusted && baselineResult)
            correctionStatus = "ADJUSTED MEASURED  " + juce::String (measuredAdjusted->similarity.total * 100.0f, 1) + "%   /   DELTA "
                             + juce::String ((measuredAdjusted->similarity.total - baselineResult->similarity.total) * 100.0f, 1) + " pt";
        else correctionStatus = "MEASURED BASELINE · ZERO / UNMEASURED CORRECTION";
        if (proc.isCompareFineTuneApplied()) correctionStatus += "   ·   APPLIED";
        if (juce::Time::getMillisecondCounterHiRes() < autoNudgeMessageUntilMs && autoNudgeMessage.isNotEmpty())
            correctionStatus += "   ·   " + autoNudgeMessage;
        g.setColour (proc.isCompareFineTunePending() ? gold : (measuredAdjusted ? cyan : juce::Colour (0xff82928d)));
        g.setFont (juce::Font (juce::FontOptions (9.0f, juce::Font::bold)));
        g.drawText (correctionStatus,
                    fineTunePanel.withTrimmedLeft (250.0f).withTrimmedRight (10.0f).withHeight (24.0f),
                    juce::Justification::centredRight, true);
        body.removeFromTop (224);

        // Similarity is a decision surface, not a footer. Give its percentage,
        // delta and progress bar independent vertical rows.
        auto waveArea = body.removeFromTop (body.getHeight() * 30 / 100).toFloat().reduced (3.0f);
        auto spectrumArea = body.removeFromTop (body.getHeight() * 56 / 100).toFloat().reduced (3.0f);
        auto metricsArea = body.toFloat().reduced (3.0f);

        panel (g, waveArea, "WAVEFORM / ENVELOPE", led);
        panel (g, spectrumArea, "SPECTRAL FINGERPRINT", gold);
        panel (g, metricsArea, "SIMILARITY", cyan);

        auto wavePlot = waveArea.reduced (12.0f, 28.0f);
        drawPlotGrid (g, wavePlot);
        drawWave (g, wavePlot, ref, led, 2.0f);
        if (baselineFeatures) drawWave (g, wavePlot, *baselineFeatures, gold.withAlpha (0.52f), 1.35f);
        if (adjustedFeatures) drawWave (g, wavePlot, *adjustedFeatures, cyan, 1.9f);

        auto spectrumPlot = spectrumArea.reduced (12.0f, 28.0f);
        drawPlotGrid (g, spectrumPlot);
        drawSpectrum (g, spectrumPlot, ref, baselineFeatures, adjustedFeatures, led, gold, cyan);
        drawMetrics (g, metricsArea.reduced (12.0f, 25.0f), led, gold);
    }

private:
    RetroMatchSynthAudioProcessor& proc;
    RetroLookAndFeel laf;
    juce::TextButton candidateA, candidateB, candidateC, synth, reference, mix, stop;
    juce::TextButton baseline, adjusted, resetTune, measureTune, autoNudge, keepTune;
    std::array<juce::Slider, 7> fineTune;
    std::array<juce::Label, 7> fineTuneLabels;
    bool syncingFineTune = false;
    bool baselineAudition = false;
    bool compareMeasureRunning = false;
    bool autoMeasureArmed = false;
    bool autoNudgeTrial = false;
    int autoNudgeCandidate = -1;
    float autoNudgeBaselineScore = 0.0f;
    juce::String autoNudgeMessage;
    double autoNudgeMessageUntilMs = 0.0;
    bool closing = false;
    double lastFineTuneChangeMs = 0.0;
    juce::ThreadPool compareMeasurePool { 1 };

    class FineTuneMeasureJob final : public juce::ThreadPoolJob
    {
    public:
        FineTuneMeasureJob (juce::Component::SafePointer<MatchCompareDialog> ownerIn,
                            RetroMatchSynthAudioProcessor::CompareFineTuneMeasureRequest requestIn)
            : juce::ThreadPoolJob ("Compare fine-tune offline measure"), owner (ownerIn), request (std::move (requestIn)) {}

        JobStatus runJob() override
        {
            if (shouldExit()) return jobHasFinished;
            auto measured = SoundMatcher::evaluateFit (request.reference, request.params, request.settings);
            if (shouldExit()) return jobHasFinished;
            const auto candidateIndex = request.candidateIndex;
            const auto values = request.values;
            juce::MessageManager::callAsync ([safe = owner, candidateIndex, values, measured = std::move (measured)] () mutable
            {
                if (safe != nullptr) safe->completeFineTuneMeasurement (candidateIndex, values, std::move (measured));
            });
            return jobHasFinished;
        }

    private:
        juce::Component::SafePointer<MatchCompareDialog> owner;
        RetroMatchSynthAudioProcessor::CompareFineTuneMeasureRequest request;
    };

    void timerCallback() override
    {
        laf.setPalette (proc.lightPalette.load());
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (autoMeasureArmed && ! compareMeasureRunning && proc.isCompareFineTunePending()
            && now - lastFineTuneChangeMs >= 550.0)
            startFineTuneMeasurement();
        syncButtonState();
        repaint();
    }

    void syncButtonState()
    {
        candidateA.setToggleState (proc.selectedCandidate == 0, juce::dontSendNotification);
        candidateB.setToggleState (proc.selectedCandidate == 1, juce::dontSendNotification);
        candidateC.setToggleState (proc.selectedCandidate == 2, juce::dontSendNotification);
        candidateA.setEnabled (! compareMeasureRunning); candidateB.setEnabled (! compareMeasureRunning); candidateC.setEnabled (! compareMeasureRunning);
        baseline.setToggleState (baselineAudition, juce::dontSendNotification);
        adjusted.setToggleState (! baselineAudition, juce::dontSendNotification);
        measureTune.setButtonText (compareMeasureRunning ? "MEASURING..." : "MEASURE");
        measureTune.setEnabled (proc.isCompareFineTunePending() && ! compareMeasureRunning);
        autoNudge.setEnabled (proc.getCompareFineTuneValues().isNeutral() && proc.getSelectedCandidateBaseline() != nullptr && ! compareMeasureRunning);
        keepTune.setEnabled (! proc.getCompareFineTuneValues().isNeutral() && ! compareMeasureRunning);
    }

    void setFineTuneControls (CompareFineTune::Values v)
    {
        const std::array<float, 7> values {{ v.brightness, v.lowEnd, v.punch, v.tail, v.width, v.motion, v.finePitch }};
        syncingFineTune = true;
        for (size_t i = 0; i < fineTune.size(); ++i) fineTune[i].setValue (values[i], juce::dontSendNotification);
        syncingFineTune = false;
    }

    void startAutoNudge()
    {
        if (closing || compareMeasureRunning || ! proc.currentFeatures || ! proc.getCompareFineTuneValues().isNeutral()) return;
        const auto* baselineResult = proc.getSelectedCandidateBaseline();
        if (! baselineResult || baselineResult->candidateFeatures.duration <= 0.0f) return;

        const auto suggestion = CompareFineTune::suggestFromResidual (*proc.currentFeatures,
                                                                       baselineResult->candidateFeatures,
                                                                       baselineResult->params);
        if (suggestion.isNeutral())
        {
            autoNudgeMessage = "AUTO NUDGE: NO DIRECTED RESIDUAL";
            autoNudgeMessageUntilMs = juce::Time::getMillisecondCounterHiRes() + 2600.0;
            repaint();
            return;
        }

        autoNudgeTrial = true;
        autoNudgeCandidate = proc.selectedCandidate;
        autoNudgeBaselineScore = baselineResult->similarity.total;
        autoNudgeMessage = "AUTO NUDGE: VERIFYING";
        autoNudgeMessageUntilMs = juce::Time::getMillisecondCounterHiRes() + 6000.0;
        setFineTuneControls (suggestion);
        baselineAudition = false;
        proc.previewCompareFineTune (suggestion);
        autoMeasureArmed = false;
        lastFineTuneChangeMs = juce::Time::getMillisecondCounterHiRes();
        startFineTuneMeasurement();
        syncButtonState();
        repaint();
    }

    void startFineTuneMeasurement()
    {
        if (closing || compareMeasureRunning || ! proc.isCompareFineTunePending()) return;
        auto request = proc.makeCompareFineTuneMeasureRequest();
        if (! request || request->values.isNeutral()) return;
        compareMeasureRunning = true;
        autoMeasureArmed = false;
        auto* job = new FineTuneMeasureJob (juce::Component::SafePointer<MatchCompareDialog> (this), std::move (*request));
        compareMeasurePool.addJob (job, true);
    }

    void completeFineTuneMeasurement (int candidateIndex, CompareFineTune::Values values, MatchResult measured)
    {
        if (closing) return;
        const float measuredTotal = measured.similarity.total;
        const bool acceptedMeasurement = proc.acceptCompareFineTuneMeasurement (candidateIndex, values, std::move (measured));
        compareMeasureRunning = false;

        if (autoNudgeTrial && candidateIndex == autoNudgeCandidate)
        {
            autoNudgeTrial = false;
            const bool improved = acceptedMeasurement && measuredTotal > autoNudgeBaselineScore + 0.0005f;
            if (improved)
            {
                autoNudgeMessage = "AUTO NUDGE ACCEPTED  +" + juce::String ((measuredTotal - autoNudgeBaselineScore) * 100.0f, 1) + " pt";
            }
            else
            {
                setFineTuneControls ({});
                proc.resetCompareFineTune();
                autoNudgeMessage = "AUTO NUDGE REJECTED";
            }
            autoNudgeMessageUntilMs = juce::Time::getMillisecondCounterHiRes() + 3200.0;
        }

        if (proc.isCompareFineTunePending())
        {
            autoMeasureArmed = true;
            lastFineTuneChangeMs = juce::Time::getMillisecondCounterHiRes();
        }
        syncButtonState();
        repaint();
    }

    void syncFineTuneControlsFromProcessor()
    {
        setFineTuneControls (proc.getCompareFineTuneValues());
    }

    CompareFineTune::Values fineTuneValues() const
    {
        CompareFineTune::Values v;
        v.brightness = (float) fineTune[0].getValue();
        v.lowEnd = (float) fineTune[1].getValue();
        v.punch = (float) fineTune[2].getValue();
        v.tail = (float) fineTune[3].getValue();
        v.width = (float) fineTune[4].getValue();
        v.motion = (float) fineTune[5].getValue();
        v.finePitch = (float) fineTune[6].getValue();
        return v;
    }

    void applyFineTune()
    {
        if (syncingFineTune) return;
        baselineAudition = false;
        const auto values = fineTuneValues();
        autoNudgeTrial = false;
        autoNudgeMessage.clear();
        proc.previewCompareFineTune (values);
        autoMeasureArmed = ! values.isNeutral();
        lastFineTuneChangeMs = juce::Time::getMillisecondCounterHiRes();
        syncButtonState();
        repaint();
    }

    void select (int index)
    {
        if (proc.selectCandidate (index))
        {
            syncFineTuneControlsFromProcessor();
            baselineAudition = false;
            autoMeasureArmed = proc.isCompareFineTunePending();
            lastFineTuneChangeMs = juce::Time::getMillisecondCounterHiRes();
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
                              const SoundFeatures* baselineFeatures, const SoundFeatures* adjustedFeatures,
                              juce::Colour referenceColour, juce::Colour baselineColour, juce::Colour adjustedColour)
    {
        const float w = r.getWidth() / SoundFeatures::spectralBandCount;
        for (int i = 0; i < SoundFeatures::spectralBandCount; ++i)
        {
            const float x = r.getX() + i * w;
            const float rh = juce::jlimit (0.0f, 1.0f, ref.spectralBands[(size_t) i]) * r.getHeight();
            g.setColour (referenceColour.withAlpha (0.68f));
            g.fillRect (x, r.getBottom() - rh, juce::jmax (1.0f, w * 0.25f), rh);

            if (baselineFeatures)
            {
                const float bh = juce::jlimit (0.0f, 1.0f, baselineFeatures->spectralBands[(size_t) i]) * r.getHeight();
                g.setColour (baselineColour.withAlpha (adjustedFeatures ? 0.46f : 0.82f));
                g.fillRect (x + w * 0.34f, r.getBottom() - bh, juce::jmax (1.0f, w * 0.25f), bh);
            }
            if (adjustedFeatures)
            {
                const float ah = juce::jlimit (0.0f, 1.0f, adjustedFeatures->spectralBands[(size_t) i]) * r.getHeight();
                g.setColour (adjustedColour.withAlpha (0.90f));
                g.fillRect (x + w * 0.68f, r.getBottom() - ah, juce::jmax (1.0f, w * 0.25f), ah);
            }
        }
    }

    void drawMetrics (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour led, juce::Colour gold)
    {
        const auto* baselineResult = proc.getSelectedCandidateBaseline();
        const auto* adjustedResult = proc.getCompareFineTuneMeasuredResult();
        const auto& base = baselineResult ? baselineResult->similarity : proc.lastMatch.similarity;
        const auto& shown = adjustedResult ? adjustedResult->similarity : base;
        struct Metric { const char* name; float before; float after; };
        const std::array<Metric, 8> values {{
            { "TOTAL", base.total, shown.total }, { "SPECTRUM", base.spectrum, shown.spectrum },
            { "TIMBRE", base.timbre, shown.timbre }, { "TEMPORAL", base.temporal, shown.temporal },
            { "HARMONIC", base.harmonic, shown.harmonic }, { "ENVELOPE", base.envelope, shown.envelope },
            { "STEREO", base.stereo, shown.stereo }, { "PITCH", base.pitch, shown.pitch }
        }};

        const int columns = 4;
        const float cw = r.getWidth() / columns;
        const float rh = r.getHeight() / 2.0f;
        for (int i = 0; i < (int) values.size(); ++i)
        {
            auto cell = juce::Rectangle<float> (r.getX() + (i % columns) * cw,
                                                r.getY() + (i / columns) * rh,
                                                cw, rh).reduced (5.0f, 4.0f);
            const auto accent = i == 0 ? gold : led;
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff253236), cell.getTopLeft(),
                                                     juce::Colour (0xff172326), cell.getBottomLeft(), false));
            g.fillRoundedRectangle (cell, 5.0f);
            g.setColour (juce::Colour (0xff53666a));
            g.drawRoundedRectangle (cell, 5.0f, 1.0f);

            auto content = cell.reduced (10.0f, 7.0f);
            auto titleArea = content.removeFromTop (16.0f);
            g.setColour (juce::Colour (0xffd4dfdb));
            g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
            g.drawText (values[(size_t) i].name, titleArea, juce::Justification::centredLeft);

            auto bar = content.removeFromBottom (9.0f);
            content.removeFromBottom (5.0f);
            auto valueArea = content;
            g.setColour (accent);
            g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
            g.drawText (juce::String (values[(size_t) i].after * 100.0f, 1) + "%", valueArea,
                        juce::Justification::centredRight);

            if (adjustedResult)
            {
                const float delta = (values[(size_t) i].after - values[(size_t) i].before) * 100.0f;
                g.setColour (delta >= 0.0f ? juce::Colour (0xff8bd7b8) : juce::Colour (0xffd69a91));
                g.setFont (juce::Font (juce::FontOptions (8.5f, juce::Font::bold)));
                g.drawText ((delta >= 0.0f ? "+" : "") + juce::String (delta, 1) + " pt",
                            valueArea, juce::Justification::centredLeft);
            }

            g.setColour (juce::Colour (0xff0b1214));
            g.fillRoundedRectangle (bar, 3.0f);
            g.setColour (accent);
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, values[(size_t) i].after)), 3.0f);
        }
    }

};
