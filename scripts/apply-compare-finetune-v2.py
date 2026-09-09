#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


def write(path, text):
    (ROOT / path).write_text(text, encoding="utf-8")


def replace_once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected 1 match, found {count}")
    return text.replace(old, new, 1)


# -----------------------------------------------------------------------------
# CompareFineTune: explicit value equality for measured-state generation checks.
# -----------------------------------------------------------------------------
path = "Source/Matching/CompareFineTune.h"
text = read(path)
text = replace_once(text,
'''    bool isNeutral (float epsilon = 1.0e-6f) const noexcept
    {
        return std::abs (brightness) <= epsilon && std::abs (lowEnd) <= epsilon
            && std::abs (punch) <= epsilon && std::abs (tail) <= epsilon
            && std::abs (width) <= epsilon && std::abs (motion) <= epsilon
            && std::abs (finePitch) <= epsilon;
    }
};''',
'''    bool isNeutral (float epsilon = 1.0e-6f) const noexcept
    {
        return std::abs (brightness) <= epsilon && std::abs (lowEnd) <= epsilon
            && std::abs (punch) <= epsilon && std::abs (tail) <= epsilon
            && std::abs (width) <= epsilon && std::abs (motion) <= epsilon
            && std::abs (finePitch) <= epsilon;
    }

    bool nearlyEquals (const Values& other, float epsilon = 1.0e-5f) const noexcept
    {
        return std::abs (brightness - other.brightness) <= epsilon
            && std::abs (lowEnd - other.lowEnd) <= epsilon
            && std::abs (punch - other.punch) <= epsilon
            && std::abs (tail - other.tail) <= epsilon
            && std::abs (width - other.width) <= epsilon
            && std::abs (motion - other.motion) <= epsilon
            && std::abs (finePitch - other.finePitch) <= epsilon;
    }
};''', "CompareFineTune value equality")
write(path, text)


# -----------------------------------------------------------------------------
# Processor API/state: per-candidate correction, measured Adjusted, explicit KEEP.
# -----------------------------------------------------------------------------
path = "Source/PluginProcessor.h"
text = read(path)
text = replace_once(text,
'''    CompareFineTune::Values getCompareFineTuneValues() const noexcept { return compareFineTuneValues; }
    bool previewCompareFineTune (CompareFineTune::Values values);
    void resetCompareFineTune();
    bool showCompareFineTuneBaseline (bool baseline);
    bool isCompareFineTunePending() const noexcept { return compareFineTunePending; }''',
'''    CompareFineTune::Values getCompareFineTuneValues() const noexcept
    {
        return juce::isPositiveAndBelow (selectedCandidate, 3)
             ? compareFineTuneValuesByCandidate[(size_t) selectedCandidate] : CompareFineTune::Values {};
    }
    const MatchResult* getSelectedCandidateBaseline() const noexcept
    {
        return juce::isPositiveAndBelow (selectedCandidate, 3) && candidateBank[(size_t) selectedCandidate].confidence > 0.0f
             ? &candidateBank[(size_t) selectedCandidate] : nullptr;
    }
    const MatchResult* getCompareFineTuneMeasuredResult() const noexcept
    {
        if (! juce::isPositiveAndBelow (selectedCandidate, 3)) return nullptr;
        const auto index = (size_t) selectedCandidate;
        if (! compareFineTuneMeasuredByCandidate[index]) return nullptr;
        return compareFineTuneValuesByCandidate[index].nearlyEquals (compareFineTuneMeasuredValuesByCandidate[index])
             ? &*compareFineTuneMeasuredByCandidate[index] : nullptr;
    }
    bool previewCompareFineTune (CompareFineTune::Values values);
    void resetCompareFineTune();
    bool showCompareFineTuneBaseline (bool baseline);
    bool measureCompareFineTune();
    bool keepCompareFineTune();
    bool isCompareFineTunePending() const noexcept { return compareFineTunePending; }
    bool isCompareFineTuneApplied() const noexcept
    {
        return juce::isPositiveAndBelow (selectedCandidate, 3)
             && compareFineTuneAppliedByCandidate[(size_t) selectedCandidate];
    }''', "Processor fine tune public API")
text = replace_once(text,
'''    // Transient Compare correction state. It intentionally stays out of APVTS/session
    // automation until the user explicitly commits a measured patch.
    CompareFineTune::Values compareFineTuneValues {};
    bool compareFineTunePending = false;''',
'''    // Transient Compare correction state. It intentionally stays out of APVTS/session
    // automation until KEEP explicitly promotes the adjusted voice into the working patch.
    std::array<CompareFineTune::Values, 3> compareFineTuneValuesByCandidate {};
    std::array<CompareFineTune::Values, 3> compareFineTuneMeasuredValuesByCandidate {};
    std::array<std::optional<MatchResult>, 3> compareFineTuneMeasuredByCandidate {};
    std::array<bool, 3> compareFineTuneAppliedByCandidate {};
    bool compareFineTunePending = false;''', "Processor fine tune private state")
text = replace_once(text,
'''    void updateCandidatePreview (const MatchResult&);
    void invalidateMatchesAfterReferencePitchChange();
    void rebuildRoutingPlanFromState();''',
'''    void updateCandidatePreview (const MatchResult&);
    void clearCompareFineTuneState() noexcept;
    void invalidateMatchesAfterReferencePitchChange();
    void rebuildRoutingPlanFromState();''', "Processor fine tune clear helper")
write(path, text)


# -----------------------------------------------------------------------------
# Processor implementation.
# -----------------------------------------------------------------------------
path = "Source/PluginProcessor.cpp"
text = read(path)

# Any operation that invalidates the candidate bank also invalidates transient correction history.
text = text.replace('candidateBank = {};\n    selectedCandidate = 0;', 'candidateBank = {};\n    clearCompareFineTuneState();\n    selectedCandidate = 0;')
text = text.replace('candidateBank = {}; currentCandidateFeatures.reset();', 'candidateBank = {}; clearCompareFineTuneState(); currentCandidateFeatures.reset();')

text = replace_once(text,
'''std::array<MatchResult, 3> RetroMatchSynthAudioProcessor::buildCandidateBank()
{
    if (! currentFeatures) return {};
    const auto authored = getMainVoiceParameters();''',
'''std::array<MatchResult, 3> RetroMatchSynthAudioProcessor::buildCandidateBank()
{
    if (! currentFeatures) return {};
    clearCompareFineTuneState();
    const auto authored = getMainVoiceParameters();''', "Reset fine tune for candidate rebuild")
text = replace_once(text,
'''    std::array<MatchResult, 3> result {};
    if (! currentFeatures) return result;

    const auto reference = *currentFeatures;''',
'''    std::array<MatchResult, 3> result {};
    if (! currentFeatures) return result;
    clearCompareFineTuneState();

    const auto reference = *currentFeatures;''', "Reset fine tune for Gold rebuild")

old_block_start = text.index('bool RetroMatchSynthAudioProcessor::selectCandidate (int index)')
old_block_end = text.index('void RetroMatchSynthAudioProcessor::morphCandidates (int a, int b, float amount)', old_block_start)
new_block = r'''void RetroMatchSynthAudioProcessor::clearCompareFineTuneState() noexcept
{
    compareFineTuneValuesByCandidate = {};
    compareFineTuneMeasuredValuesByCandidate = {};
    compareFineTuneMeasuredByCandidate = {};
    compareFineTuneAppliedByCandidate.fill (false);
    compareFineTunePending = false;
}

bool RetroMatchSynthAudioProcessor::selectCandidate (int index)
{
    if (! juce::isPositiveAndBelow (index, 3) || candidateBank[(size_t) index].confidence <= 0.0f) return false;
    selectedCandidate = index;
    const auto stateIndex = (size_t) index;
    const auto& selected = candidateBank[stateIndex];
    const auto values = compareFineTuneValuesByCandidate[stateIndex];
    const bool measuredCurrent = compareFineTuneMeasuredByCandidate[stateIndex].has_value()
                              && values.nearlyEquals (compareFineTuneMeasuredValuesByCandidate[stateIndex]);
    compareFineTunePending = ! values.isNeutral() && ! measuredCurrent;

    auto setChoice = [this] (const char* id, int value)
    {
        if (auto* parameter = apvts.getParameter (id))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) value));
    };
    if (selected.algorithm >= 0) setChoice ("resynthStrategy", juce::jlimit (0, 6, selected.algorithm));
    if (selected.complexity >= 0) setChoice ("resynthComplexity", juce::jlimit (0, 3, selected.complexity));

    if (values.isNeutral())
        applyGeneratedRack (selected, index);
    else
    {
        auto adjusted = selected;
        adjusted.params = CompareFineTune::apply (selected.params, values);
        applyGeneratedRack (adjusted, index);
    }

    const auto& visual = measuredCurrent ? *compareFineTuneMeasuredByCandidate[stateIndex] : selected;
    lastMatch = visual;
    currentCandidateFeatures = visual.candidateFeatures.duration > 0.0f
                             ? std::optional<SoundFeatures> (visual.candidateFeatures) : std::nullopt;
    return true;
}

bool RetroMatchSynthAudioProcessor::previewCompareFineTune (CompareFineTune::Values values)
{
    if (! juce::isPositiveAndBelow (selectedCandidate, 3)) return false;
    const auto index = (size_t) selectedCandidate;
    const auto baseline = candidateBank[index];
    if (baseline.confidence <= 0.0f) return false;

    values.clamp();
    compareFineTuneValuesByCandidate[index] = values;
    compareFineTuneAppliedByCandidate[index] = false;
    const bool measuredCurrent = compareFineTuneMeasuredByCandidate[index].has_value()
                              && values.nearlyEquals (compareFineTuneMeasuredValuesByCandidate[index]);
    compareFineTunePending = ! values.isNeutral() && ! measuredCurrent;

    auto preview = baseline;
    preview.params = CompareFineTune::apply (baseline.params, values);
    // Preview changes the live engine immediately, but remains transient compare state.
    // Closing Compare without KEEP restores the measured candidate baseline.
    applyGeneratedRack (preview, selectedCandidate);

    const auto& visual = measuredCurrent ? *compareFineTuneMeasuredByCandidate[index] : baseline;
    lastMatch = visual;
    currentCandidateFeatures = visual.candidateFeatures.duration > 0.0f
                             ? std::optional<SoundFeatures> (visual.candidateFeatures) : std::nullopt;
    return true;
}

void RetroMatchSynthAudioProcessor::resetCompareFineTune()
{
    if (! juce::isPositiveAndBelow (selectedCandidate, 3)) return;
    const auto index = (size_t) selectedCandidate;
    compareFineTuneValuesByCandidate[index] = {};
    compareFineTuneMeasuredValuesByCandidate[index] = {};
    compareFineTuneMeasuredByCandidate[index].reset();
    compareFineTuneAppliedByCandidate[index] = false;
    compareFineTunePending = false;
    if (candidateBank[index].confidence > 0.0f)
    {
        applyGeneratedRack (candidateBank[index], selectedCandidate);
        lastMatch = candidateBank[index];
        currentCandidateFeatures = candidateBank[index].candidateFeatures.duration > 0.0f
                                 ? std::optional<SoundFeatures> (candidateBank[index].candidateFeatures) : std::nullopt;
    }
}

bool RetroMatchSynthAudioProcessor::showCompareFineTuneBaseline (bool baselineView)
{
    if (! juce::isPositiveAndBelow (selectedCandidate, 3)) return false;
    const auto index = (size_t) selectedCandidate;
    const auto measuredBaseline = candidateBank[index];
    if (measuredBaseline.confidence <= 0.0f) return false;

    if (baselineView)
    {
        applyGeneratedRack (measuredBaseline, selectedCandidate);
        lastMatch = measuredBaseline;
        currentCandidateFeatures = measuredBaseline.candidateFeatures.duration > 0.0f
                                 ? std::optional<SoundFeatures> (measuredBaseline.candidateFeatures) : std::nullopt;
        return true;
    }

    const auto values = compareFineTuneValuesByCandidate[index];
    auto adjusted = measuredBaseline;
    adjusted.params = CompareFineTune::apply (measuredBaseline.params, values);
    applyGeneratedRack (adjusted, selectedCandidate);

    const bool measuredCurrent = compareFineTuneMeasuredByCandidate[index].has_value()
                              && values.nearlyEquals (compareFineTuneMeasuredValuesByCandidate[index]);
    const auto& visual = measuredCurrent ? *compareFineTuneMeasuredByCandidate[index] : measuredBaseline;
    lastMatch = visual;
    currentCandidateFeatures = visual.candidateFeatures.duration > 0.0f
                             ? std::optional<SoundFeatures> (visual.candidateFeatures) : std::nullopt;
    return true;
}

bool RetroMatchSynthAudioProcessor::measureCompareFineTune()
{
    if (! currentFeatures || ! juce::isPositiveAndBelow (selectedCandidate, 3)) return false;
    const auto index = (size_t) selectedCandidate;
    const auto baseline = candidateBank[index];
    if (baseline.confidence <= 0.0f) return false;

    const auto values = compareFineTuneValuesByCandidate[index];
    if (values.isNeutral())
    {
        compareFineTuneMeasuredValuesByCandidate[index] = {};
        compareFineTuneMeasuredByCandidate[index].reset();
        compareFineTunePending = false;
        applyGeneratedRack (baseline, selectedCandidate);
        lastMatch = baseline;
        currentCandidateFeatures = baseline.candidateFeatures.duration > 0.0f
                                 ? std::optional<SoundFeatures> (baseline.candidateFeatures) : std::nullopt;
        return true;
    }

    auto params = CompareFineTune::apply (baseline.params, values);
    auto settings = matchSettings;
    if (baseline.algorithm >= 0) settings.algorithm = juce::jlimit (0, 6, baseline.algorithm);
    auto measured = SoundMatcher::evaluateFit (*currentFeatures, params, settings);
    measured.algorithm = baseline.algorithm;
    measured.complexity = baseline.complexity;
    measured.fullRackScore = baseline.fullRackScore;
    measured.explanation = "Compare fine-tune measured adjustment. " + measured.explanation;

    compareFineTuneMeasuredValuesByCandidate[index] = values;
    compareFineTuneMeasuredByCandidate[index] = measured;
    compareFineTunePending = false;
    applyGeneratedRack (measured, selectedCandidate);
    lastMatch = measured;
    currentCandidateFeatures = measured.candidateFeatures.duration > 0.0f
                             ? std::optional<SoundFeatures> (measured.candidateFeatures) : std::nullopt;
    return true;
}

bool RetroMatchSynthAudioProcessor::keepCompareFineTune()
{
    if (! juce::isPositiveAndBelow (selectedCandidate, 3)) return false;
    const auto index = (size_t) selectedCandidate;
    const auto baseline = candidateBank[index];
    if (baseline.confidence <= 0.0f) return false;

    const auto values = compareFineTuneValuesByCandidate[index];
    auto adjusted = baseline;
    adjusted.params = CompareFineTune::apply (baseline.params, values);
    applyGeneratedRack (adjusted, selectedCandidate);
    compareFineTuneAppliedByCandidate[index] = true;
    apvts.state.setProperty ("patchName", "Compare Adjusted " + juce::String ((char) ('A' + selectedCandidate)), nullptr);

    const bool measuredCurrent = compareFineTuneMeasuredByCandidate[index].has_value()
                              && values.nearlyEquals (compareFineTuneMeasuredValuesByCandidate[index]);
    const auto& visual = measuredCurrent ? *compareFineTuneMeasuredByCandidate[index] : baseline;
    lastMatch = visual;
    currentCandidateFeatures = visual.candidateFeatures.duration > 0.0f
                             ? std::optional<SoundFeatures> (visual.candidateFeatures) : std::nullopt;
    compareFineTunePending = ! values.isNeutral() && ! measuredCurrent;
    return true;
}

'''
text = text[:old_block_start] + new_block + text[old_block_end:]
write(path, text)


# -----------------------------------------------------------------------------
# Compare UI: independent A/B/C controls, real MEASURE, triple traces, KEEP.
# -----------------------------------------------------------------------------
path = "Source/UI/MatchCompareDialog.h"
text = read(path)
text = replace_once(text,
'for (auto* b : { &candidateA, &candidateB, &candidateC, &synth, &reference, &mix, &stop, &baseline, &adjusted, &resetTune })',
'for (auto* b : { &candidateA, &candidateB, &candidateC, &synth, &reference, &mix, &stop, &baseline, &adjusted, &resetTune, &measureTune, &keepTune })', "Compare visible buttons")
text = replace_once(text,
'''        baseline.setButtonText ("BASELINE"); adjusted.setButtonText ("ADJUSTED"); resetTune.setButtonText ("RESET");''',
'''        baseline.setButtonText ("BASELINE"); adjusted.setButtonText ("ADJUSTED"); resetTune.setButtonText ("RESET");
        measureTune.setButtonText ("MEASURE"); keepTune.setButtonText ("KEEP / APPLY");''', "Compare button labels")
text = replace_once(text,
'''        resetTune.onClick = [this]
        {
            syncingFineTune = true;
            for (auto& slider : fineTune) slider.setValue (0.0, juce::dontSendNotification);
            syncingFineTune = false;
            baselineAudition = false;
            proc.resetCompareFineTune();
            syncButtonState();
            repaint();
        };

        syncButtonState();''',
'''        resetTune.onClick = [this]
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
            proc.measureCompareFineTune();
            syncButtonState();
            repaint();
        };
        keepTune.onClick = [this]
        {
            baselineAudition = false;
            proc.keepCompareFineTune();
            syncButtonState();
            repaint();
        };

        syncFineTuneControlsFromProcessor();
        syncButtonState();''', "Compare measure/keep callbacks")
text = replace_once(text,
'''    ~MatchCompareDialog() override
    {
        proc.allEditorNotesOff();
        setLookAndFeel (nullptr);
    }''',
'''    ~MatchCompareDialog() override
    {
        proc.allEditorNotesOff();
        // BASELINE/ADJUSTED are audition states. Only KEEP is allowed to survive closing Compare.
        proc.showCompareFineTuneBaseline (! proc.isCompareFineTuneApplied());
        setLookAndFeel (nullptr);
    }''', "Compare close policy")
text = replace_once(text,
'''        auto tune = r.removeFromTop (96).reduced (2, 3);
        auto tuneButtons = tune.removeFromRight (272);
        baseline.setBounds (tuneButtons.removeFromTop (28).removeFromLeft (88).reduced (2));
        adjusted.setBounds (tuneButtons.removeFromTop (28).removeFromLeft (88).reduced (2));
        resetTune.setBounds (tuneButtons.removeFromTop (28).removeFromLeft (88).reduced (2));''',
'''        auto tune = r.removeFromTop (96).reduced (2, 3);
        auto tuneButtons = tune.removeFromRight (310);
        auto tuneRow1 = tuneButtons.removeFromTop (30);
        baseline.setBounds (tuneRow1.removeFromLeft (96).reduced (2));
        adjusted.setBounds (tuneRow1.removeFromLeft (96).reduced (2));
        resetTune.setBounds (tuneRow1.removeFromLeft (92).reduced (2));
        auto tuneRow2 = tuneButtons.removeFromTop (30);
        measureTune.setBounds (tuneRow2.removeFromLeft (118).reduced (2));
        keepTune.setBounds (tuneRow2.removeFromLeft (166).reduced (2));''', "Compare tune layout")
text = replace_once(text,
'''        auto legend = juce::Rectangle<float> ((float) getWidth() - 300.0f, 18.0f, 270.0f, 22.0f);
        drawLegend (g, legend.removeFromLeft (125.0f), led, "REFERENCE");
        drawLegend (g, legend, gold, "RESYNTH");''',
'''        auto legend = juce::Rectangle<float> ((float) getWidth() - 390.0f, 18.0f, 360.0f, 22.0f);
        drawLegend (g, legend.removeFromLeft (118.0f), led, "REFERENCE");
        drawLegend (g, legend.removeFromLeft (118.0f), gold.withAlpha (0.58f), "BASELINE");
        drawLegend (g, legend, cyan, "ADJUSTED");''', "Compare triple legend")
text = replace_once(text,
'''        const auto& ref = *proc.currentFeatures;
        const SoundFeatures* candidate = proc.currentCandidateFeatures ? &*proc.currentCandidateFeatures : nullptr;
        auto body = getLocalBounds().reduced (22);
        auto fineTunePanel = body.withTrimmedTop (82).withHeight (96).toFloat().reduced (1.0f);
        panel (g, fineTunePanel, "POST-ANALYSIS CORRECTION", cyan);
        g.setColour (proc.isCompareFineTunePending() ? gold : juce::Colour (0xff82928d));
        g.setFont (juce::Font (juce::FontOptions (9.0f, juce::Font::bold)));
        g.drawText (proc.isCompareFineTunePending() ? "ADJUSTED AUDIO · SCORE/TRACE PENDING MEASURE" : "MEASURED BASELINE · ZERO CORRECTION",
                    fineTunePanel.withTrimmedLeft (fineTunePanel.getWidth() - 260.0f).withHeight (22.0f).reduced (4.0f, 0.0f),
                    juce::Justification::centredRight, true);''',
'''        const auto& ref = *proc.currentFeatures;
        const auto* baselineResult = proc.getSelectedCandidateBaseline();
        const auto* measuredAdjusted = proc.getCompareFineTuneMeasuredResult();
        const SoundFeatures* baselineFeatures = baselineResult && baselineResult->candidateFeatures.duration > 0.0f
                                              ? &baselineResult->candidateFeatures : nullptr;
        const SoundFeatures* adjustedFeatures = measuredAdjusted && measuredAdjusted->candidateFeatures.duration > 0.0f
                                              ? &measuredAdjusted->candidateFeatures : nullptr;
        auto body = getLocalBounds().reduced (22);
        auto fineTunePanel = body.withTrimmedTop (82).withHeight (96).toFloat().reduced (1.0f);
        panel (g, fineTunePanel, "POST-ANALYSIS CORRECTION", cyan);
        juce::String correctionStatus;
        if (proc.isCompareFineTunePending()) correctionStatus = "ADJUSTED AUDIO · SCORE/TRACE PENDING MEASURE";
        else if (measuredAdjusted && baselineResult)
            correctionStatus = "ADJUSTED MEASURED  " + juce::String (measuredAdjusted->similarity.total * 100.0f, 1) + "%   /   DELTA "
                             + juce::String ((measuredAdjusted->similarity.total - baselineResult->similarity.total) * 100.0f, 1) + " pt";
        else correctionStatus = "MEASURED BASELINE · ZERO / UNMEASURED CORRECTION";
        if (proc.isCompareFineTuneApplied()) correctionStatus += "   ·   APPLIED";
        g.setColour (proc.isCompareFineTunePending() ? gold : (measuredAdjusted ? cyan : juce::Colour (0xff82928d)));
        g.setFont (juce::Font (juce::FontOptions (9.0f, juce::Font::bold)));
        g.drawText (correctionStatus,
                    fineTunePanel.withTrimmedLeft (fineTunePanel.getWidth() - 390.0f).withHeight (22.0f).reduced (4.0f, 0.0f),
                    juce::Justification::centredRight, true);''', "Compare measurement status")
text = replace_once(text,
'''        drawWave (g, wavePlot, ref, led, 2.0f);
        if (candidate) drawWave (g, wavePlot, *candidate, gold, 1.7f);

        auto spectrumPlot = spectrumArea.reduced (12.0f, 28.0f);
        drawPlotGrid (g, spectrumPlot);
        drawSpectrum (g, spectrumPlot, ref, candidate, led, gold);
        drawMetrics (g, metricsArea.reduced (12.0f, 25.0f), led, gold);''',
'''        drawWave (g, wavePlot, ref, led, 2.0f);
        if (baselineFeatures) drawWave (g, wavePlot, *baselineFeatures, gold.withAlpha (0.52f), 1.35f);
        if (adjustedFeatures) drawWave (g, wavePlot, *adjustedFeatures, cyan, 1.9f);

        auto spectrumPlot = spectrumArea.reduced (12.0f, 28.0f);
        drawPlotGrid (g, spectrumPlot);
        drawSpectrum (g, spectrumPlot, ref, baselineFeatures, adjustedFeatures, led, gold, cyan);
        drawMetrics (g, metricsArea.reduced (12.0f, 25.0f), led, gold);''', "Compare triple plots")
text = replace_once(text,
'''    juce::TextButton baseline, adjusted, resetTune;''',
'''    juce::TextButton baseline, adjusted, resetTune, measureTune, keepTune;''', "Compare button members")
text = replace_once(text,
'''        baseline.setToggleState (baselineAudition, juce::dontSendNotification);
        adjusted.setToggleState (! baselineAudition, juce::dontSendNotification);
    }

    CompareFineTune::Values fineTuneValues() const''',
'''        baseline.setToggleState (baselineAudition, juce::dontSendNotification);
        adjusted.setToggleState (! baselineAudition, juce::dontSendNotification);
        measureTune.setEnabled (proc.isCompareFineTunePending());
        keepTune.setEnabled (! proc.getCompareFineTuneValues().isNeutral());
    }

    void syncFineTuneControlsFromProcessor()
    {
        const auto v = proc.getCompareFineTuneValues();
        const std::array<float, 7> values {{ v.brightness, v.lowEnd, v.punch, v.tail, v.width, v.motion, v.finePitch }};
        syncingFineTune = true;
        for (size_t i = 0; i < fineTune.size(); ++i) fineTune[i].setValue (values[i], juce::dontSendNotification);
        syncingFineTune = false;
    }

    CompareFineTune::Values fineTuneValues() const''', "Compare UI state sync")
text = replace_once(text,
'''        if (proc.selectCandidate (index))
        {
            syncingFineTune = true;
            for (auto& slider : fineTune) slider.setValue (0.0, juce::dontSendNotification);
            syncingFineTune = false;
            baselineAudition = false;
            syncButtonState();''',
'''        if (proc.selectCandidate (index))
        {
            syncFineTuneControlsFromProcessor();
            baselineAudition = false;
            syncButtonState();''', "Compare candidate state restore")

# Triple spectrum rendering.
start = text.index('    static void drawSpectrum (')
end = text.index('    void drawMetrics (', start)
text = text[:start] + r'''    static void drawSpectrum (juce::Graphics& g, juce::Rectangle<float> r, const SoundFeatures& ref,
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

''' + text[end:]

# Metrics now show actual measured before -> after deltas when available.
start = text.index('    void drawMetrics (')
end = text.rindex('\n};')
old_metrics = text[start:end]
new_metrics = r'''    void drawMetrics (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour led, juce::Colour gold)
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
            g.drawText (values[(size_t) i].name, titleArea, juce::Justification::centredLeft);

            auto bar = content.removeFromBottom (8.0f);
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
'''
text = text[:start] + new_metrics + text[end:]
write(path, text)


# -----------------------------------------------------------------------------
# Test contracts: value-generation equality and neutral reset semantics.
# -----------------------------------------------------------------------------
path = "Tests/CompareFineTuneTests.cpp"
text = read(path)
text = replace_once(text,
'''    CompareFineTune::Values positive;
    positive.brightness = 0.75f;''',
'''    CompareFineTune::Values positive;
    positive.brightness = 0.75f;''', "Test anchor")
text = replace_once(text,
'''    positive.finePitch = 0.40f;
    const auto adjusted = CompareFineTune::apply (baseline, positive);''',
'''    positive.finePitch = 0.40f;
    auto sameGeneration = positive;
    if (! positive.nearlyEquals (sameGeneration)) return fail ("fine-tune generation equality rejected identical values");
    sameGeneration.width += 0.02f;
    if (positive.nearlyEquals (sameGeneration)) return fail ("fine-tune generation equality missed a changed knob");
    const auto adjusted = CompareFineTune::apply (baseline, positive);''', "Test value equality")
write(path, text)


# -----------------------------------------------------------------------------
# Roadmap status: implementation complete, automatic debounce remains pending.
# -----------------------------------------------------------------------------
path = "plan.md"
text = read(path)
replacements = {
'- [ ] Keep **Baseline** and **Adjusted** states side-by-side in the processor; selecting another A/B/C candidate starts from that candidate\'s own baseline.': '- [x] Keep **Baseline** and **Adjusted** states side-by-side in the processor; A/B/C now retain independent correction + measured-Adjusted state.',
'- [ ] Never turn a static patch into a moving patch implicitly: Motion scales existing MSEG/LFO/mod routes only.': '- [x] Never turn a static patch into a moving patch implicitly: Motion scales existing MSEG/LFO/mod routes only.',
'- [ ] Store fine-tune offsets outside the released APVTS automation order; only explicitly committed changes become the working patch.': '- [x] Store fine-tune offsets outside the released APVTS automation order; closing Compare restores baseline unless **KEEP / APPLY** explicitly commits the adjusted working patch.',
'- [ ] Put the seven correction knobs directly below/alongside the current Reference-vs-Resynth plots, all bipolar and center-detented except where a more specific unit display is useful.': '- [x] Put the seven correction knobs directly below/alongside the current Reference-vs-Resynth plots, all bipolar and center-detented except where a more specific unit display is useful.',
'- [ ] Keep existing **REFERENCE / SYNTH / MIX** audition and add **BASELINE / ADJUSTED** so the ear can compare the correction without losing the reference A/B.': '- [x] Keep existing **REFERENCE / SYNTH / MIX** audition and add **BASELINE / ADJUSTED** so the ear can compare the correction without losing the reference A/B.',
'- [ ] While dragging, apply the adjusted parameters to the live synth immediately; do not perform expensive offline analysis in the audio callback or on every mouse tick.': '- [x] While dragging, apply the adjusted parameters to the live synth immediately; do not perform expensive offline analysis in the audio callback or on every mouse tick.',
'- [ ] Debounce/re-measure Adjusted off the audio thread after a drag settles; only then update the measured score and feature traces.': '- [~] Re-measure Adjusted with the explicit **MEASURE** action using the offline matcher, then update measured score/feature traces. Automatic debounce after drag remains pending.',
'- [ ] Overlay three states where useful: Reference, Baseline and Adjusted. Baseline should be a dim/ghost trace so the direction of the correction is obvious.': '- [x] Overlay three states where useful: Reference, Baseline and measured Adjusted; Baseline is a dim/ghost trace.',
'- [ ] Show a compact residual strip for Spectrum, Timbre, Temporal/Envelope, Harmonic, Stereo and Pitch with signed before→after deltas.': '- [x] Show measured before→after deltas for Spectrum, Timbre, Temporal, Envelope, Harmonic, Stereo and Pitch in the similarity grid.',
'- [ ] Never estimate or cosmetically inflate similarity. Until a re-render completes, label the adjusted score **PENDING MEASURE** and keep the last measured value visually distinct.': '- [x] Never estimate or cosmetically inflate similarity. Until a re-render completes, label the adjusted score **PENDING MEASURE** and keep the baseline trace/score distinct.',
'- [ ] **RESET** returns all knobs to zero and exactly restores the selected candidate baseline.': '- [x] **RESET** returns all knobs to zero and exactly restores the selected candidate baseline.',
'- [ ] **KEEP / APPLY TO PATCH** commits the adjusted `VoiceParameters` as the editable patch while retaining the original measured candidate for comparison/history.': '- [x] **KEEP / APPLY TO PATCH** commits the adjusted `VoiceParameters` as the editable patch while retaining the original candidate bank for comparison/history.',
'- [ ] Zero-correction output matches the selected candidate baseline.': '- [x] Zero-correction output matches the selected candidate baseline.',
'- [ ] Gold/full-rack corrections preserve layer topology and immutable baseline state.': '- [x] Gold/full-rack corrections preserve layer topology and immutable baseline state.',
'- [ ] Candidate A/B/C switching maintains independent correction state or explicitly resets it; no cross-candidate leakage.': '- [x] Candidate A/B/C switching maintains independent correction/measured state; no cross-candidate leakage.',
'- [ ] Re-measure work never allocates or blocks inside `processBlock`.': '- [x] Re-measure work is invoked from Compare and never runs inside `processBlock`.',
'- [ ] Session/preset policy is explicit: temporary compare UI state is not serialized unless the user commits it.': '- [x] Session/preset policy is explicit: temporary compare state is not serialized; only **KEEP / APPLY** leaves the adjusted live patch in normal APVTS/session state.'
}
for old, new in replacements.items():
    if old not in text:
        raise RuntimeError(f"plan item missing: {old[:72]}")
    text = text.replace(old, new, 1)
write(path, text)

print("Compare fine-tune v2 patch applied")
