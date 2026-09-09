#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
def read(p): return (ROOT / p).read_text(encoding="utf-8")
def write(p, t): (ROOT / p).write_text(t, encoding="utf-8")
def replace_once(t, old, new, label):
    c = t.count(old)
    if c != 1: raise RuntimeError(f"{label}: expected 1 match, found {c}")
    return t.replace(old, new, 1)

# Directed residual -> conservative musical correction suggestion.
path = "Source/Matching/CompareFineTune.h"
text = read(path)
text = replace_once(text, '#include "../Engine/SynthEngine.h"\n', '#include "../Engine/SynthEngine.h"\n#include "../Analysis/SampleAnalyzer.h"\n', "Auto Nudge feature include")
anchor = '''inline VoiceParameters apply (const VoiceParameters& baseline, Values values)
{'''
insert = r'''inline Values suggestFromResidual (const SoundFeatures& reference, const SoundFeatures& candidate,
                                   const VoiceParameters& baseline, float maxStep = 0.24f) noexcept
{
    Values v;
    maxStep = juce::jlimit (0.02f, 0.40f, maxStep);
    auto limit = [maxStep] (float x) { return juce::jlimit (-maxStep, maxStep, x); };
    auto logRatio = [] (float target, float current) noexcept
    {
        if (target <= 1.0e-5f || current <= 1.0e-5f) return 0.0f;
        return std::log2 (target / current);
    };

    // Match the direction used by each musical macro, not a generic optimizer gradient.
    v.brightness = limit (logRatio (reference.spectralCentroidHz, candidate.spectralCentroidHz) / 1.75f);
    v.lowEnd = limit ((reference.lowEnergyRatio - candidate.lowEnergyRatio) * 2.6f);
    v.punch = limit (logRatio (candidate.attackSeconds, reference.attackSeconds) / 2.25f);

    const float tailResidual = 0.48f * logRatio (reference.releaseSeconds, candidate.releaseSeconds)
                             + 0.34f * logRatio (reference.decaySeconds, candidate.decaySeconds)
                             + 0.18f * (reference.sustainLevel - candidate.sustainLevel);
    v.tail = limit (tailResidual / 1.65f);
    v.width = limit ((reference.stereoWidth - candidate.stereoWidth) * 0.85f);

    bool hasExistingMotion = std::abs (baseline.lfoPitch) > 1.0e-5f || std::abs (baseline.lfoCutoff) > 1.0e-5f
                          || std::abs (baseline.lfoAmp) > 1.0e-5f || std::abs (baseline.msegDepth) > 1.0e-5f;
    for (const auto& slot : baseline.modSlots) hasExistingMotion |= std::abs (slot.amount) > 1.0e-5f;
    for (const auto& slot : baseline.modGraphSlots) hasExistingMotion |= std::abs (slot.amount) > 1.0e-5f;
    for (const auto& slot : baseline.moduleModSlots) hasExistingMotion |= std::abs (slot.amount) > 1.0e-5f;
    if (hasExistingMotion)
        v.motion = limit ((reference.spectralMotion - candidate.spectralMotion) * 2.0f);

    if (reference.fundamentalHz > 20.0f && candidate.fundamentalHz > 20.0f
        && reference.pitchConfidence > 0.20f && candidate.pitchConfidence > 0.20f)
    {
        const float cents = 1200.0f * logRatio (reference.fundamentalHz, candidate.fundamentalHz);
        v.finePitch = limit (cents / 50.0f);
    }
    v.clamp();
    return v;
}

'''
if anchor not in text: raise RuntimeError("Auto Nudge apply anchor missing")
text = text.replace(anchor, insert + anchor, 1)
write(path, text)

# UI button/trial lifecycle. Auto Nudge is accepted only after measured improvement.
path = "Source/UI/MatchCompareDialog.h"
text = read(path)
text = replace_once(text,
'for (auto* b : { &candidateA, &candidateB, &candidateC, &synth, &reference, &mix, &stop, &baseline, &adjusted, &resetTune, &measureTune, &keepTune })',
'for (auto* b : { &candidateA, &candidateB, &candidateC, &synth, &reference, &mix, &stop, &baseline, &adjusted, &resetTune, &measureTune, &autoNudge, &keepTune })', "Auto Nudge visible")
text = replace_once(text,
'''        measureTune.setButtonText ("MEASURE"); keepTune.setButtonText ("KEEP / APPLY");''',
'''        measureTune.setButtonText ("MEASURE"); autoNudge.setButtonText ("AUTO NUDGE"); keepTune.setButtonText ("KEEP / APPLY");''', "Auto Nudge label")
text = replace_once(text,
'''        measureTune.onClick = [this]
        {
            baselineAudition = false;
            startFineTuneMeasurement();
            syncButtonState();
            repaint();
        };
        keepTune.onClick = [this]''',
'''        measureTune.onClick = [this]
        {
            baselineAudition = false;
            startFineTuneMeasurement();
            syncButtonState();
            repaint();
        };
        autoNudge.onClick = [this] { startAutoNudge(); };
        keepTune.onClick = [this]''', "Auto Nudge callback")
text = replace_once(text,
'''        auto tuneRow2 = tuneButtons.removeFromTop (30);
        measureTune.setBounds (tuneRow2.removeFromLeft (118).reduced (2));
        keepTune.setBounds (tuneRow2.removeFromLeft (166).reduced (2));''',
'''        auto tuneRow2 = tuneButtons.removeFromTop (30);
        measureTune.setBounds (tuneRow2.removeFromLeft (86).reduced (2));
        autoNudge.setBounds (tuneRow2.removeFromLeft (104).reduced (2));
        keepTune.setBounds (tuneRow2.removeFromLeft (104).reduced (2));''', "Auto Nudge layout")
text = replace_once(text,
'''        if (proc.isCompareFineTuneApplied()) correctionStatus += "   ·   APPLIED";
        g.setColour (proc.isCompareFineTunePending() ? gold : (measuredAdjusted ? cyan : juce::Colour (0xff82928d)));''',
'''        if (proc.isCompareFineTuneApplied()) correctionStatus += "   ·   APPLIED";
        if (juce::Time::getMillisecondCounterHiRes() < autoNudgeMessageUntilMs && autoNudgeMessage.isNotEmpty())
            correctionStatus += "   ·   " + autoNudgeMessage;
        g.setColour (proc.isCompareFineTunePending() ? gold : (measuredAdjusted ? cyan : juce::Colour (0xff82928d)));''', "Auto Nudge status")
text = replace_once(text,
'''    juce::TextButton baseline, adjusted, resetTune, measureTune, keepTune;''',
'''    juce::TextButton baseline, adjusted, resetTune, measureTune, autoNudge, keepTune;''', "Auto Nudge member")
text = replace_once(text,
'''    bool autoMeasureArmed = false;
    bool closing = false;
    double lastFineTuneChangeMs = 0.0;
    juce::ThreadPool compareMeasurePool { 1 };''',
'''    bool autoMeasureArmed = false;
    bool autoNudgeTrial = false;
    int autoNudgeCandidate = -1;
    float autoNudgeBaselineScore = 0.0f;
    juce::String autoNudgeMessage;
    double autoNudgeMessageUntilMs = 0.0;
    bool closing = false;
    double lastFineTuneChangeMs = 0.0;
    juce::ThreadPool compareMeasurePool { 1 };''', "Auto Nudge fields")
text = replace_once(text,
'''        candidateA.setToggleState (proc.selectedCandidate == 0, juce::dontSendNotification);
        candidateB.setToggleState (proc.selectedCandidate == 1, juce::dontSendNotification);
        candidateC.setToggleState (proc.selectedCandidate == 2, juce::dontSendNotification);
        baseline.setToggleState (baselineAudition, juce::dontSendNotification);
        adjusted.setToggleState (! baselineAudition, juce::dontSendNotification);
        measureTune.setButtonText (compareMeasureRunning ? "MEASURING..." : "MEASURE");
        measureTune.setEnabled (proc.isCompareFineTunePending() && ! compareMeasureRunning);
        keepTune.setEnabled (! proc.getCompareFineTuneValues().isNeutral() && ! compareMeasureRunning);''',
'''        candidateA.setToggleState (proc.selectedCandidate == 0, juce::dontSendNotification);
        candidateB.setToggleState (proc.selectedCandidate == 1, juce::dontSendNotification);
        candidateC.setToggleState (proc.selectedCandidate == 2, juce::dontSendNotification);
        candidateA.setEnabled (! compareMeasureRunning); candidateB.setEnabled (! compareMeasureRunning); candidateC.setEnabled (! compareMeasureRunning);
        baseline.setToggleState (baselineAudition, juce::dontSendNotification);
        adjusted.setToggleState (! baselineAudition, juce::dontSendNotification);
        measureTune.setButtonText (compareMeasureRunning ? "MEASURING..." : "MEASURE");
        measureTune.setEnabled (proc.isCompareFineTunePending() && ! compareMeasureRunning);
        autoNudge.setEnabled (proc.getCompareFineTuneValues().isNeutral() && proc.getSelectedCandidateBaseline() != nullptr && ! compareMeasureRunning);
        keepTune.setEnabled (! proc.getCompareFineTuneValues().isNeutral() && ! compareMeasureRunning);''', "Auto Nudge enable rules")

anchor = '''    void startFineTuneMeasurement()
    {'''
insert = r'''    void setFineTuneControls (CompareFineTune::Values v)
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

'''
if anchor not in text: raise RuntimeError("Auto Nudge measurement anchor missing")
text = text.replace(anchor, insert + anchor, 1)
text = replace_once(text,
'''    void syncFineTuneControlsFromProcessor()
    {
        const auto v = proc.getCompareFineTuneValues();
        const std::array<float, 7> values {{ v.brightness, v.lowEnd, v.punch, v.tail, v.width, v.motion, v.finePitch }};
        syncingFineTune = true;
        for (size_t i = 0; i < fineTune.size(); ++i) fineTune[i].setValue (values[i], juce::dontSendNotification);
        syncingFineTune = false;
    }''',
'''    void syncFineTuneControlsFromProcessor()
    {
        setFineTuneControls (proc.getCompareFineTuneValues());
    }''', "Auto Nudge share control setter")
text = replace_once(text,
'''    void completeFineTuneMeasurement (int candidateIndex, CompareFineTune::Values values, MatchResult measured)
    {
        if (closing) return;
        proc.acceptCompareFineTuneMeasurement (candidateIndex, values, std::move (measured));
        compareMeasureRunning = false;
        if (proc.isCompareFineTunePending())
        {
            autoMeasureArmed = true;
            lastFineTuneChangeMs = juce::Time::getMillisecondCounterHiRes();
        }
        syncButtonState();
        repaint();
    }''',
'''    void completeFineTuneMeasurement (int candidateIndex, CompareFineTune::Values values, MatchResult measured)
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
    }''', "Auto Nudge measured acceptance")
text = replace_once(text,
'''        const auto values = fineTuneValues();
        proc.previewCompareFineTune (values);
        autoMeasureArmed = ! values.isNeutral();''',
'''        const auto values = fineTuneValues();
        autoNudgeTrial = false;
        autoNudgeMessage.clear();
        proc.previewCompareFineTune (values);
        autoMeasureArmed = ! values.isNeutral();''', "Manual edit cancels Auto Nudge trial")
write(path, text)

# Tests for suggestion direction and static-motion safety.
path = "Tests/CompareFineTuneTests.cpp"
text = read(path)
anchor = '''    CompareFineTune::Values bright, dark;
'''
insert = r'''    SoundFeatures residualReference, residualCandidate;
    residualReference.spectralCentroidHz = 4200.0f; residualCandidate.spectralCentroidHz = 2500.0f;
    residualReference.lowEnergyRatio = 0.30f; residualCandidate.lowEnergyRatio = 0.16f;
    residualReference.attackSeconds = 0.02f; residualCandidate.attackSeconds = 0.08f;
    residualReference.decaySeconds = 0.50f; residualCandidate.decaySeconds = 0.24f;
    residualReference.releaseSeconds = 0.70f; residualCandidate.releaseSeconds = 0.30f;
    residualReference.sustainLevel = 0.75f; residualCandidate.sustainLevel = 0.55f;
    residualReference.stereoWidth = 0.65f; residualCandidate.stereoWidth = 0.22f;
    residualReference.spectralMotion = 0.40f; residualCandidate.spectralMotion = 0.12f;
    residualReference.fundamentalHz = 222.0f; residualCandidate.fundamentalHz = 220.0f;
    residualReference.pitchConfidence = residualCandidate.pitchConfidence = 0.9f;
    const auto nudge = CompareFineTune::suggestFromResidual (residualReference, residualCandidate, baseline);
    if (nudge.brightness <= 0.0f || nudge.lowEnd <= 0.0f || nudge.punch <= 0.0f
        || nudge.tail <= 0.0f || nudge.width <= 0.0f || nudge.motion <= 0.0f || nudge.finePitch <= 0.0f)
        return fail ("Auto Nudge residual directions are inconsistent with macro mappings");
    const auto staticNudge = CompareFineTune::suggestFromResidual (residualReference, residualCandidate, staticPatch);
    if (std::abs (staticNudge.motion) > 1.0e-6f)
        return fail ("Auto Nudge suggested motion for a patch without existing motion topology");

'''
if anchor not in text: raise RuntimeError("Auto Nudge test anchor missing")
text = text.replace(anchor, insert + anchor, 1)
write(path, text)

# Roadmap status.
path = "plan.md"
text = read(path)
replace_once_text = '- [ ] Optional **AUTO NUDGE** comes later: derive small suggested offsets from signed residuals, then re-render and accept only improvements. It must not become an unbounded second optimizer.'
replacement = '- [x] **AUTO NUDGE** derives one bounded correction step from directed feature residuals, performs a real background re-render, and automatically rejects the suggestion unless measured total similarity improves.'
if replace_once_text not in text: raise RuntimeError("Auto Nudge plan item missing")
text = text.replace(replace_once_text, replacement, 1)
write(path, text)

print("Measured Auto Nudge patch applied")
