from pathlib import Path


def replace_once(text, old, new, label):
    n = text.count(old)
    if n != 1:
        raise RuntimeError(f"{label}: expected 1 match, got {n}")
    return text.replace(old, new, 1)

h = Path('Source/UI/RetroMatchEditorV3.h')
cpp = Path('Source/UI/RetroMatchEditorV3.cpp')
compare = Path('Source/UI/MatchCompareDialog.h')
ref = Path('Source/UI/ReferenceEditorDialog.h')

ht = h.read_text(encoding='utf-8')
ct = cpp.read_text(encoding='utf-8')
mt = compare.read_text(encoding='utf-8')
rt = ref.read_text(encoding='utf-8')

# Main window: expose resynthesis method + depth directly in the reference workspace.
ht = replace_once(ht,
'''    juce::Slider masterOutput;\n    juce::Label masterOutputLabel;\n    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> masterOutputAttachment;\n''',
'''    juce::Slider masterOutput;\n    juce::Label masterOutputLabel;\n    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> masterOutputAttachment;\n    juce::Label resynthStrategyLabel, resynthComplexityLabel;\n    juce::ComboBox resynthStrategyChoice, resynthComplexityChoice;\n    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> resynthStrategyAttachment, resynthComplexityAttachment;\n''', 'header resynth controls')

ct = replace_once(ct,
'''    addAndMakeVisible (masterOutputLabel); addAndMakeVisible (masterOutput); addAndMakeVisible (masterMeter);\n\n    load.onClick = [this] { chooseFile(); };\n''',
'''    addAndMakeVisible (masterOutputLabel); addAndMakeVisible (masterOutput); addAndMakeVisible (masterMeter);\n\n    styleTextLabel (resynthStrategyLabel, "METHOD");\n    styleTextLabel (resynthComplexityLabel, "DEPTH");\n    resynthStrategyLabel.setColour (juce::Label::textColourId, goldColour (*this));\n    resynthComplexityLabel.setColour (juce::Label::textColourId, goldColour (*this));\n    resynthStrategyChoice.addItemList ({ "Balanced Hybrid", "Reference Wavetable", "Spectral Subtractive",\n                                         "FM / Harmonic", "Layered Studio", "Texture / Chop" }, 1);\n    resynthComplexityChoice.addItemList ({ "Classic / 1-3", "Studio / 4", "Deep / 6", "Maximum / 8" }, 1);\n    resynthStrategyChoice.setTooltip ("Resynthesis / matching topology used by Quick and Refine. Reference Wavetable and Texture / Chop deliberately use more of the loaded sample.");\n    resynthComplexityChoice.setTooltip ("How many complementary synth instances the resynthesis may build: legacy 1-3, 4, 6 or 8 layers.");\n    resynthStrategyAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "resynthStrategy", resynthStrategyChoice);\n    resynthComplexityAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "resynthComplexity", resynthComplexityChoice);\n    for (auto* c : std::array<juce::Component*, 4> { &resynthStrategyLabel, &resynthStrategyChoice, &resynthComplexityLabel, &resynthComplexityChoice })\n        addAndMakeVisible (*c);\n\n    load.onClick = [this] { chooseFile(); };\n''', 'constructor resynth controls')

ct = replace_once(ct,
'''    load.setBounds (w.removeFromTop (34));\n    w.removeFromTop (5);\n\n    referencePitchInfo.setBounds (w.removeFromTop (18));\n''',
'''    load.setBounds (w.removeFromTop (34));\n    w.removeFromTop (5);\n\n    auto methodRow = w.removeFromTop (28);\n    resynthStrategyLabel.setBounds (methodRow.removeFromLeft (62));\n    resynthStrategyChoice.setBounds (methodRow.reduced (2, 1));\n    w.removeFromTop (3);\n    auto depthRow = w.removeFromTop (28);\n    resynthComplexityLabel.setBounds (depthRow.removeFromLeft (62));\n    resynthComplexityChoice.setBounds (depthRow.reduced (2, 1));\n    w.removeFromTop (5);\n\n    referencePitchInfo.setBounds (w.removeFromTop (18));\n''', 'workspace resynth layout')

# Compare dialog: ASCII title, method/depth readout and proper internal metric padding.
mt = mt.replace('REFERENCE  ↔  RESYNTH VISUAL COMPARE', 'REFERENCE  VS  RESYNTH VISUAL COMPARE')
mt = replace_once(mt,
'''        g.drawText ("REFERENCE  VS  RESYNTH VISUAL COMPARE", 24, 16, getWidth() - 48, 26,\n                    juce::Justification::centredLeft);\n\n        auto legend = juce::Rectangle<float> ((float) getWidth() - 300.0f, 18.0f, 270.0f, 22.0f);\n''',
'''        g.drawText ("REFERENCE  VS  RESYNTH VISUAL COMPARE", 24, 14, getWidth() - 48, 24,\n                    juce::Justification::centredLeft);\n\n        const juce::StringArray methods { "Balanced Hybrid", "Reference Wavetable", "Spectral Subtractive",\n                                           "FM / Harmonic", "Layered Studio", "Texture / Chop" };\n        const juce::StringArray depths { "Classic / 1-3", "Studio / 4", "Deep / 6", "Maximum / 8" };\n        const int methodIndex = juce::jlimit (0, methods.size() - 1, (int) std::lround (proc.apvts.getRawParameterValue ("resynthStrategy")->load()));\n        const int depthIndex = juce::jlimit (0, depths.size() - 1, (int) std::lround (proc.apvts.getRawParameterValue ("resynthComplexity")->load()));\n        g.setColour (juce::Colour (0xffc9d6d1));\n        g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));\n        g.drawText ("METHOD  " + methods[methodIndex] + "    /    DEPTH  " + depths[depthIndex],\n                    24, 38, getWidth() - 360, 16, juce::Justification::centredLeft, true);\n\n        auto legend = juce::Rectangle<float> ((float) getWidth() - 300.0f, 18.0f, 270.0f, 22.0f);\n''', 'compare method readout')

mt = replace_once(mt,
'''            auto titleArea = cell.removeFromTop (18.0f);\n            g.setColour (juce::Colour (0xffd4dfdb));\n            g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));\n            g.drawText (values[(size_t) i].first, titleArea, juce::Justification::centredLeft);\n\n            auto valueArea = cell.removeFromTop (juce::jmax (16.0f, cell.getHeight() - 12.0f));\n            g.setColour (accent);\n            g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));\n            g.drawText (juce::String (values[(size_t) i].second * 100.0f, 1) + "%", valueArea,\n                        juce::Justification::centredRight);\n\n            auto bar = cell.removeFromBottom (8.0f);\n''',
'''            auto content = cell.reduced (10.0f, 6.0f);\n            auto titleArea = content.removeFromTop (18.0f);\n            g.setColour (juce::Colour (0xffd4dfdb));\n            g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));\n            g.drawText (values[(size_t) i].first, titleArea, juce::Justification::centredLeft);\n\n            auto bar = content.removeFromBottom (8.0f);\n            auto valueArea = content;\n            g.setColour (accent);\n            g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));\n            g.drawText (juce::String (values[(size_t) i].second * 100.0f, 1) + "%", valueArea,\n                        juce::Justification::centredRight);\n\n''', 'metric padding')

# Large reference editor: always show the currently selected method/depth.
rt = replace_once(rt,
'''        g.drawText (proc.getReferenceFile().getFileName() + "  /  " + juce::String (duration, 2) + " s",\n                    286, 11, getWidth() - 308, 24, juce::Justification::centredRight, true);\n\n        drawSection (g, editSectionBounds, "SELECTION + VIEW");\n''',
'''        g.drawText (proc.getReferenceFile().getFileName() + "  /  " + juce::String (duration, 2) + " s",\n                    286, 11, getWidth() - 308, 24, juce::Justification::centredRight, true);\n\n        const juce::StringArray methods { "Balanced Hybrid", "Reference Wavetable", "Spectral Subtractive",\n                                           "FM / Harmonic", "Layered Studio", "Texture / Chop" };\n        const juce::StringArray depths { "Classic / 1-3", "Studio / 4", "Deep / 6", "Maximum / 8" };\n        const int methodIndex = juce::jlimit (0, methods.size() - 1, (int) std::lround (proc.apvts.getRawParameterValue ("resynthStrategy")->load()));\n        const int depthIndex = juce::jlimit (0, depths.size() - 1, (int) std::lround (proc.apvts.getRawParameterValue ("resynthComplexity")->load()));\n        g.setColour (p.primary);\n        g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));\n        g.drawText ("RESYNTH METHOD  " + methods[methodIndex] + "    /    DEPTH  " + depths[depthIndex],\n                    22, 35, getWidth() - 44, 16, juce::Justification::centredLeft, true);\n\n        drawSection (g, editSectionBounds, "SELECTION + VIEW");\n''', 'reference method readout')
rt = replace_once(rt, '        area.removeFromTop (32);\n', '        area.removeFromTop (50);\n', 'reference header height')

h.write_text(ht, encoding='utf-8')
cpp.write_text(ct, encoding='utf-8')
compare.write_text(mt, encoding='utf-8')
ref.write_text(rt, encoding='utf-8')
print('Applied resynthesis method controls, compare title/spacing and method readouts.')
