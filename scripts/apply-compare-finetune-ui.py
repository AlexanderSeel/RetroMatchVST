from pathlib import Path


def replace_once(text, old, new, path):
    if old not in text:
        raise SystemExit(f"anchor not found in {path}: {old[:100]!r}")
    return text.replace(old, new, 1)

# Processor header: public compare preview API + private transient state.
p = Path('Source/PluginProcessor.h')
s = p.read_text()
s = replace_once(s, '#include "Matching/OfflineRenderer.h"\n', '#include "Matching/OfflineRenderer.h"\n#include "Matching/CompareFineTune.h"\n', p)
s = replace_once(s,
'''    void morphCandidates (int a, int b, float amount);\n    VoiceParameters getCurrentVoiceParameters() const { return readParams(); }\n''',
'''    void morphCandidates (int a, int b, float amount);\n    CompareFineTune::Values getCompareFineTuneValues() const noexcept { return compareFineTuneValues; }\n    bool previewCompareFineTune (CompareFineTune::Values values);\n    void resetCompareFineTune();\n    bool showCompareFineTuneBaseline (bool baseline);\n    bool isCompareFineTunePending() const noexcept { return compareFineTunePending; }\n    VoiceParameters getCurrentVoiceParameters() const { return readParams(); }\n''', p)
s = replace_once(s,
'''    std::atomic<float> outputPeakLeft { 0.0f };\n    std::atomic<float> outputPeakRight { 0.0f };\n''',
'''    std::atomic<float> outputPeakLeft { 0.0f };\n    std::atomic<float> outputPeakRight { 0.0f };\n    // Transient Compare correction state. It intentionally stays out of APVTS/session\n    // automation until the user explicitly commits a measured patch.\n    CompareFineTune::Values compareFineTuneValues {};\n    bool compareFineTunePending = false;\n''', p)
p.write_text(s)

# Processor implementation: keep candidate metadata truthful while applying audible corrected DSP.
p = Path('Source/PluginProcessor.cpp')
s = p.read_text()
anchor = '''bool RetroMatchSynthAudioProcessor::selectCandidate (int index)\n{\n    if (! juce::isPositiveAndBelow (index, 3) || candidateBank[(size_t) index].confidence <= 0.0f) return false;\n    selectedCandidate = index;\n'''
replacement = '''bool RetroMatchSynthAudioProcessor::selectCandidate (int index)\n{\n    if (! juce::isPositiveAndBelow (index, 3) || candidateBank[(size_t) index].confidence <= 0.0f) return false;\n    selectedCandidate = index;\n    compareFineTuneValues = {};\n    compareFineTunePending = false;\n'''
s = replace_once(s, anchor, replacement, p)
insert_before = '''void RetroMatchSynthAudioProcessor::morphCandidates (int a, int b, float amount)\n'''
methods = r'''bool RetroMatchSynthAudioProcessor::previewCompareFineTune (CompareFineTune::Values values)
{
    if (! juce::isPositiveAndBelow (selectedCandidate, 3)) return false;
    const auto baseline = candidateBank[(size_t) selectedCandidate];
    if (baseline.confidence <= 0.0f) return false;

    values.clamp();
    compareFineTuneValues = values;
    compareFineTunePending = ! values.isNeutral();

    auto preview = baseline;
    preview.params = CompareFineTune::apply (baseline.params, values);
    // Do not pretend this unmeasured preview owns new analysis data. applyGeneratedRack
    // is reused only to make the actual main/layer/global-rack DSP audible.
    applyGeneratedRack (preview, selectedCandidate);
    lastMatch = baseline;
    currentCandidateFeatures = baseline.candidateFeatures.duration > 0.0f
                             ? std::optional<SoundFeatures> (baseline.candidateFeatures)
                             : std::nullopt;
    return true;
}

void RetroMatchSynthAudioProcessor::resetCompareFineTune()
{
    compareFineTuneValues = {};
    compareFineTunePending = false;
    if (juce::isPositiveAndBelow (selectedCandidate, 3)
        && candidateBank[(size_t) selectedCandidate].confidence > 0.0f)
        applyGeneratedRack (candidateBank[(size_t) selectedCandidate], selectedCandidate);
}

bool RetroMatchSynthAudioProcessor::showCompareFineTuneBaseline (bool baseline)
{
    if (! juce::isPositiveAndBelow (selectedCandidate, 3)) return false;
    const auto measured = candidateBank[(size_t) selectedCandidate];
    if (measured.confidence <= 0.0f) return false;

    if (baseline)
        applyGeneratedRack (measured, selectedCandidate);
    else
    {
        auto adjusted = measured;
        adjusted.params = CompareFineTune::apply (measured.params, compareFineTuneValues);
        applyGeneratedRack (adjusted, selectedCandidate);
    }

    // A/B audition must never rewrite the measured score/trace.
    lastMatch = measured;
    currentCandidateFeatures = measured.candidateFeatures.duration > 0.0f
                             ? std::optional<SoundFeatures> (measured.candidateFeatures)
                             : std::nullopt;
    return true;
}

'''
s = replace_once(s, insert_before, methods + insert_before, p)
p.write_text(s)

# Compare dialog: seven musical correction knobs + baseline/adjusted audition + truthful pending status.
p = Path('Source/UI/MatchCompareDialog.h')
s = p.read_text()
s = replace_once(s,
'''        for (auto* b : { &candidateA, &candidateB, &candidateC, &synth, &reference, &mix, &stop })\n            addAndMakeVisible (*b);\n\n        candidateA.setButtonText ("A"); candidateB.setButtonText ("B"); candidateC.setButtonText ("C");\n        synth.setButtonText ("SYNTH"); reference.setButtonText ("REFERENCE"); mix.setButtonText ("MIX"); stop.setButtonText ("STOP");\n''',
'''        for (auto* b : { &candidateA, &candidateB, &candidateC, &synth, &reference, &mix, &stop, &baseline, &adjusted, &resetTune })\n            addAndMakeVisible (*b);\n\n        candidateA.setButtonText ("A"); candidateB.setButtonText ("B"); candidateC.setButtonText ("C");\n        synth.setButtonText ("SYNTH"); reference.setButtonText ("REFERENCE"); mix.setButtonText ("MIX"); stop.setButtonText ("STOP");\n        baseline.setButtonText ("BASELINE"); adjusted.setButtonText ("ADJUSTED"); resetTune.setButtonText ("RESET");\n        baseline.setClickingTogglesState (true); adjusted.setClickingTogglesState (true);\n        baseline.setRadioGroupId (0x524d46); adjusted.setRadioGroupId (0x524d46);\n\n        static constexpr const char* tuneNames[] { "BRIGHT", "LOW END", "PUNCH", "TAIL", "WIDTH", "MOTION", "FINE PITCH" };\n        for (size_t i = 0; i < fineTune.size(); ++i)\n        {\n            addAndMakeVisible (fineTune[i]);\n            addAndMakeVisible (fineTuneLabels[i]);\n            fineTune[i].setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);\n            fineTune[i].setTextBoxStyle (juce::Slider::TextBoxBelow, false, 54, 14);\n            fineTune[i].setRange (-1.0, 1.0, 0.01);\n            fineTune[i].setDoubleClickReturnValue (true, 0.0);\n            fineTune[i].setValue (0.0, juce::dontSendNotification);\n            fineTuneLabels[i].setText (tuneNames[i], juce::dontSendNotification);\n            fineTuneLabels[i].setJustificationType (juce::Justification::centred);\n            fineTuneLabels[i].setColour (juce::Label::textColourId, juce::Colour (0xffb8c8c3));\n            fineTuneLabels[i].setFont (juce::Font (juce::FontOptions (8.5f, juce::Font::bold)));\n            fineTune[i].onValueChange = [this] { applyFineTune(); };\n        }\n''', p)
s = replace_once(s,
'''        stop.onClick = [this]\n        {\n            proc.allEditorNotesOff();\n            proc.setReferenceAuditionMode (RetroMatchSynthAudioProcessor::ReferenceAuditionMode::synthOnly);\n            repaint();\n        };\n\n        syncButtonState();\n''',
'''        stop.onClick = [this]\n        {\n            proc.allEditorNotesOff();\n            proc.setReferenceAuditionMode (RetroMatchSynthAudioProcessor::ReferenceAuditionMode::synthOnly);\n            repaint();\n        };\n        baseline.onClick = [this]\n        {\n            baselineAudition = true;\n            proc.showCompareFineTuneBaseline (true);\n            syncButtonState();\n            repaint();\n        };\n        adjusted.onClick = [this]\n        {\n            baselineAudition = false;\n            proc.showCompareFineTuneBaseline (false);\n            syncButtonState();\n            repaint();\n        };\n        resetTune.onClick = [this]\n        {\n            syncingFineTune = true;\n            for (auto& slider : fineTune) slider.setValue (0.0, juce::dontSendNotification);\n            syncingFineTune = false;\n            baselineAudition = false;\n            proc.resetCompareFineTune();\n            syncButtonState();\n            repaint();\n        };\n\n        syncButtonState();\n''', p)
s = replace_once(s,
'''        stop.setBounds (top.removeFromLeft (76).reduced (2));\n    }\n''',
'''        stop.setBounds (top.removeFromLeft (76).reduced (2));\n\n        auto tune = r.removeFromTop (96).reduced (2, 3);\n        auto tuneButtons = tune.removeFromRight (272);\n        baseline.setBounds (tuneButtons.removeFromTop (28).removeFromLeft (88).reduced (2));\n        adjusted.setBounds (tuneButtons.removeFromTop (28).removeFromLeft (88).reduced (2));\n        resetTune.setBounds (tuneButtons.removeFromTop (28).removeFromLeft (88).reduced (2));\n        const int cell = juce::jmax (54, tune.getWidth() / (int) fineTune.size());\n        for (size_t i = 0; i < fineTune.size(); ++i)\n        {\n            auto c = tune.removeFromLeft (i + 1 == fineTune.size() ? tune.getWidth() : cell);\n            fineTuneLabels[i].setBounds (c.removeFromTop (16));\n            fineTune[i].setBounds (c.reduced (2, 0));\n        }\n    }\n''', p)
s = replace_once(s,
'''        auto body = getLocalBounds().reduced (22);\n        body.removeFromTop (88);\n''',
'''        auto body = getLocalBounds().reduced (22);\n        auto fineTunePanel = body.withTrimmedTop (82).withHeight (96).toFloat().reduced (1.0f);\n        panel (g, fineTunePanel, "POST-ANALYSIS CORRECTION", cyan);\n        g.setColour (proc.isCompareFineTunePending() ? gold : juce::Colour (0xff82928d));\n        g.setFont (juce::Font (juce::FontOptions (9.0f, juce::Font::bold)));\n        g.drawText (proc.isCompareFineTunePending() ? "ADJUSTED AUDIO · SCORE/TRACE PENDING MEASURE" : "MEASURED BASELINE · ZERO CORRECTION",\n                    fineTunePanel.withTrimmedLeft (fineTunePanel.getWidth() - 260.0f).withHeight (22.0f).reduced (4.0f, 0.0f),\n                    juce::Justification::centredRight, true);\n        body.removeFromTop (180);\n''', p)
s = replace_once(s,
'''    juce::TextButton candidateA, candidateB, candidateC, synth, reference, mix, stop;\n''',
'''    juce::TextButton candidateA, candidateB, candidateC, synth, reference, mix, stop;\n    juce::TextButton baseline, adjusted, resetTune;\n    std::array<juce::Slider, 7> fineTune;\n    std::array<juce::Label, 7> fineTuneLabels;\n    bool syncingFineTune = false;\n    bool baselineAudition = false;\n''', p)
s = replace_once(s,
'''        candidateC.setToggleState (proc.selectedCandidate == 2, juce::dontSendNotification);\n    }\n\n    void select (int index)\n''',
'''        candidateC.setToggleState (proc.selectedCandidate == 2, juce::dontSendNotification);\n        baseline.setToggleState (baselineAudition, juce::dontSendNotification);\n        adjusted.setToggleState (! baselineAudition, juce::dontSendNotification);\n    }\n\n    CompareFineTune::Values fineTuneValues() const\n    {\n        CompareFineTune::Values v;\n        v.brightness = (float) fineTune[0].getValue();\n        v.lowEnd = (float) fineTune[1].getValue();\n        v.punch = (float) fineTune[2].getValue();\n        v.tail = (float) fineTune[3].getValue();\n        v.width = (float) fineTune[4].getValue();\n        v.motion = (float) fineTune[5].getValue();\n        v.finePitch = (float) fineTune[6].getValue();\n        return v;\n    }\n\n    void applyFineTune()\n    {\n        if (syncingFineTune) return;\n        baselineAudition = false;\n        proc.previewCompareFineTune (fineTuneValues());\n        syncButtonState();\n        repaint();\n    }\n\n    void select (int index)\n''', p)
s = replace_once(s,
'''        if (proc.selectCandidate (index))\n        {\n            syncButtonState();\n            repaint();\n        }\n''',
'''        if (proc.selectCandidate (index))\n        {\n            syncingFineTune = true;\n            for (auto& slider : fineTune) slider.setValue (0.0, juce::dontSendNotification);\n            syncingFineTune = false;\n            baselineAudition = false;\n            syncButtonState();\n            repaint();\n        }\n''', p)
p.write_text(s)

print('Compare fine-tune UI integration applied')
