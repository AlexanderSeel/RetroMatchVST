#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def read(path): return (ROOT / path).read_text(encoding="utf-8")
def write(path, text): (ROOT / path).write_text(text, encoding="utf-8")
def replace_once(text, old, new, label):
    count = text.count(old)
    if count != 1: raise RuntimeError(f"{label}: expected 1 match, found {count}")
    return text.replace(old, new, 1)

# Processor snapshot/accept API so offline scoring never reads mutable UI state on worker threads.
path = "Source/PluginProcessor.h"
text = read(path)
text = replace_once(text,
'''    const MatchResult* getCompareFineTuneMeasuredResult() const noexcept
    {
        if (! juce::isPositiveAndBelow (selectedCandidate, 3)) return nullptr;
        const auto index = (size_t) selectedCandidate;
        if (! compareFineTuneMeasuredByCandidate[index]) return nullptr;
        return compareFineTuneValuesByCandidate[index].nearlyEquals (compareFineTuneMeasuredValuesByCandidate[index])
             ? &*compareFineTuneMeasuredByCandidate[index] : nullptr;
    }
    bool previewCompareFineTune (CompareFineTune::Values values);''',
'''    const MatchResult* getCompareFineTuneMeasuredResult() const noexcept
    {
        if (! juce::isPositiveAndBelow (selectedCandidate, 3)) return nullptr;
        const auto index = (size_t) selectedCandidate;
        if (! compareFineTuneMeasuredByCandidate[index]) return nullptr;
        return compareFineTuneValuesByCandidate[index].nearlyEquals (compareFineTuneMeasuredValuesByCandidate[index])
             ? &*compareFineTuneMeasuredByCandidate[index] : nullptr;
    }
    struct CompareFineTuneMeasureRequest
    {
        int candidateIndex = -1;
        CompareFineTune::Values values {};
        SoundFeatures reference;
        VoiceParameters params;
        MatchSettings settings;
        MatchResult baseline;
    };
    std::optional<CompareFineTuneMeasureRequest> makeCompareFineTuneMeasureRequest() const;
    bool acceptCompareFineTuneMeasurement (int candidateIndex, CompareFineTune::Values values, MatchResult measured);
    bool previewCompareFineTune (CompareFineTune::Values values);''', "Processor background measure API")
write(path, text)

path = "Source/PluginProcessor.cpp"
text = read(path)
start = text.index('bool RetroMatchSynthAudioProcessor::measureCompareFineTune()')
end = text.index('bool RetroMatchSynthAudioProcessor::keepCompareFineTune()', start)
new = r'''std::optional<RetroMatchSynthAudioProcessor::CompareFineTuneMeasureRequest>
RetroMatchSynthAudioProcessor::makeCompareFineTuneMeasureRequest() const
{
    if (! currentFeatures || ! juce::isPositiveAndBelow (selectedCandidate, 3)) return std::nullopt;
    const auto index = (size_t) selectedCandidate;
    const auto baseline = candidateBank[index];
    if (baseline.confidence <= 0.0f) return std::nullopt;

    CompareFineTuneMeasureRequest request;
    request.candidateIndex = selectedCandidate;
    request.values = compareFineTuneValuesByCandidate[index];
    request.reference = *currentFeatures;
    request.params = CompareFineTune::apply (baseline.params, request.values);
    request.settings = matchSettings;
    if (baseline.algorithm >= 0) request.settings.algorithm = juce::jlimit (0, 6, baseline.algorithm);
    request.baseline = baseline;
    return request;
}

bool RetroMatchSynthAudioProcessor::acceptCompareFineTuneMeasurement (int candidateIndex,
                                                                       CompareFineTune::Values values,
                                                                       MatchResult measured)
{
    if (! juce::isPositiveAndBelow (candidateIndex, 3)) return false;
    const auto index = (size_t) candidateIndex;
    if (candidateBank[index].confidence <= 0.0f
        || ! compareFineTuneValuesByCandidate[index].nearlyEquals (values))
        return false; // stale worker result: candidate/knobs moved while it rendered.

    const auto& baseline = candidateBank[index];
    measured.algorithm = baseline.algorithm;
    measured.complexity = baseline.complexity;
    measured.fullRackScore = baseline.fullRackScore;
    measured.explanation = "Compare fine-tune measured adjustment. " + measured.explanation;
    compareFineTuneMeasuredValuesByCandidate[index] = values;
    compareFineTuneMeasuredByCandidate[index] = measured;

    if (candidateIndex == selectedCandidate)
    {
        compareFineTunePending = false;
        applyGeneratedRack (measured, selectedCandidate);
        lastMatch = measured;
        currentCandidateFeatures = measured.candidateFeatures.duration > 0.0f
                                 ? std::optional<SoundFeatures> (measured.candidateFeatures) : std::nullopt;
    }
    return true;
}

bool RetroMatchSynthAudioProcessor::measureCompareFineTune()
{
    const auto request = makeCompareFineTuneMeasureRequest();
    if (! request) return false;
    if (request->values.isNeutral())
    {
        resetCompareFineTune();
        return true;
    }
    auto measured = SoundMatcher::evaluateFit (request->reference, request->params, request->settings);
    return acceptCompareFineTuneMeasurement (request->candidateIndex, request->values, std::move (measured));
}

'''
text = text[:start] + new + text[end:]
write(path, text)

# Compare UI worker job + debounce.
path = "Source/UI/MatchCompareDialog.h"
text = read(path)
text = replace_once(text,
'''        measureTune.onClick = [this]
        {
            baselineAudition = false;
            proc.measureCompareFineTune();
            syncButtonState();
            repaint();
        };''',
'''        measureTune.onClick = [this]
        {
            baselineAudition = false;
            startFineTuneMeasurement();
            syncButtonState();
            repaint();
        };''', "Compare manual measure uses background job")
text = replace_once(text,
'''    ~MatchCompareDialog() override
    {
        proc.allEditorNotesOff();
        // BASELINE/ADJUSTED are audition states. Only KEEP is allowed to survive closing Compare.
        proc.showCompareFineTuneBaseline (! proc.isCompareFineTuneApplied());
        setLookAndFeel (nullptr);
    }''',
'''    ~MatchCompareDialog() override
    {
        closing = true;
        compareMeasurePool.removeAllJobs (true, 10000);
        proc.allEditorNotesOff();
        // BASELINE/ADJUSTED are audition states. Only KEEP is allowed to survive closing Compare.
        proc.showCompareFineTuneBaseline (! proc.isCompareFineTuneApplied());
        setLookAndFeel (nullptr);
    }''', "Compare worker shutdown")
text = replace_once(text,
'''    bool syncingFineTune = false;
    bool baselineAudition = false;

    void timerCallback() override
    {
        laf.setPalette (proc.lightPalette.load());
        syncButtonState();
        repaint();
    }''',
'''    bool syncingFineTune = false;
    bool baselineAudition = false;
    bool compareMeasureRunning = false;
    bool autoMeasureArmed = false;
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
    }''', "Compare background worker fields")
text = replace_once(text,
'''        measureTune.setEnabled (proc.isCompareFineTunePending());
        keepTune.setEnabled (! proc.getCompareFineTuneValues().isNeutral());
    }

    void syncFineTuneControlsFromProcessor()''',
'''        measureTune.setButtonText (compareMeasureRunning ? "MEASURING..." : "MEASURE");
        measureTune.setEnabled (proc.isCompareFineTunePending() && ! compareMeasureRunning);
        keepTune.setEnabled (! proc.getCompareFineTuneValues().isNeutral() && ! compareMeasureRunning);
    }

    void startFineTuneMeasurement()
    {
        if (closing || compareMeasureRunning || ! proc.isCompareFineTunePending()) return;
        auto request = proc.makeCompareFineTuneMeasureRequest();
        if (! request || request->values.isNeutral()) return;
        compareMeasureRunning = true;
        autoMeasureArmed = false;
        auto* job = new FineTuneMeasureJob (juce::Component::SafePointer<MatchCompareDialog> (this), std::move (*request));
        if (! compareMeasurePool.addJob (job, true))
        {
            delete job;
            compareMeasureRunning = false;
        }
    }

    void completeFineTuneMeasurement (int candidateIndex, CompareFineTune::Values values, MatchResult measured)
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
    }

    void syncFineTuneControlsFromProcessor()''', "Compare background helpers")
text = replace_once(text,
'''    void applyFineTune()
    {
        if (syncingFineTune) return;
        baselineAudition = false;
        proc.previewCompareFineTune (fineTuneValues());
        syncButtonState();
        repaint();
    }''',
'''    void applyFineTune()
    {
        if (syncingFineTune) return;
        baselineAudition = false;
        const auto values = fineTuneValues();
        proc.previewCompareFineTune (values);
        autoMeasureArmed = ! values.isNeutral();
        lastFineTuneChangeMs = juce::Time::getMillisecondCounterHiRes();
        syncButtonState();
        repaint();
    }''', "Compare debounce arm")
text = replace_once(text,
'''            syncFineTuneControlsFromProcessor();
            baselineAudition = false;
            syncButtonState();''',
'''            syncFineTuneControlsFromProcessor();
            baselineAudition = false;
            autoMeasureArmed = proc.isCompareFineTunePending();
            lastFineTuneChangeMs = juce::Time::getMillisecondCounterHiRes();
            syncButtonState();''', "Compare candidate debounce state")
write(path, text)

# Plan: automatic debounce is now implemented; manual MEASURE remains available as override.
path = "plan.md"
text = read(path)
text = replace_once(text,
'- [~] Re-measure Adjusted with the explicit **MEASURE** action using the offline matcher, then update measured score/feature traces. Automatic debounce after drag remains pending.',
'- [x] Debounce Adjusted after knob movement (~550 ms), render/score it on a dedicated background worker, reject stale results, and update measured score/feature traces only after a valid offline result. **MEASURE** remains as a manual immediate trigger.',
"Plan background remeasure")
write(path, text)

print("Background Compare remeasure patch applied")
