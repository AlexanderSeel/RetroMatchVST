from pathlib import Path


def replace(path, old, new):
    p = Path(path)
    text = p.read_text(encoding='utf-8')
    if old not in text:
        raise SystemExit(f'pattern not found in {path}: {old[:100]!r}')
    p.write_text(text.replace(old, new, 1), encoding='utf-8')

# -----------------------------------------------------------------------------
# Large Patch Map: presentation geometry must not inherit compact/manual node
# positions. Routing remains live/editable, but opening BIG always starts from a
# clean, fitted signal-flow layout. Local drags remain valid for the open window.
# -----------------------------------------------------------------------------
replace('Source/UI/SignalLabPage.h',
'''        for (const auto& node : restored.nodes)\n        {\n            if (node.positionValid) restoredNodePositions[node.id.toStdString()] = { node.position.x, node.position.y };\n            if (node.routingMode != 0) restoredNodeRoutingModes[node.id.toStdString()] = node.routingMode;\n        }\n''',
'''        for (const auto& node : restored.nodes)\n        {\n            // The BIG map is a presentation/editor surface, not a magnified copy\n            // of the compact map's persisted manual geometry. Start it from the\n            // canonical signal-flow layout so a displaced compact node cannot\n            // create metres of dead space in the large window.\n            if (! mapOnlyMode && node.positionValid)\n                restoredNodePositions[node.id.toStdString()] = { node.position.x, node.position.y };\n            if (node.routingMode != 0) restoredNodeRoutingModes[node.id.toStdString()] = node.routingMode;\n        }\n''')
replace('Source/UI/SignalLabPage.h',
'''    void stageGraphStateForPersistence()\n    {\n        if (graphModel.nodes.empty()) return;\n''',
'''    void stageGraphStateForPersistence()\n    {\n        // BIG view deliberately owns presentation-only node positions. Audio,\n        // modulation and topology edits already commit through their parameters;\n        // do not overwrite the compact map's saved geometry on every repaint.\n        if (mapOnlyMode || graphModel.nodes.empty()) return;\n''')
replace('Source/UI/SignalLabPage.h',
'''        const float headerHeight = mapOnlyMode ? 58.0f : (bounds.getWidth() < 980.0f ? 56.0f : 30.0f);\n''',
'''        const float headerHeight = mapOnlyMode ? 72.0f : (bounds.getWidth() < 980.0f ? 56.0f : 30.0f);\n''')
replace('Source/UI/SignalLabPage.h',
'''        const bool compactHeader = mapOnlyMode || header.getHeight() > 40.0f;\n        const bool veryNarrow = header.getWidth() < 620.0f;\n        const float gap = veryNarrow ? 3.0f : 4.0f;\n        const float h = compactHeader ? 22.0f : 20.0f;\n''',
'''        const bool compactHeader = mapOnlyMode || header.getHeight() > 40.0f;\n        const bool veryNarrow = header.getWidth() < 620.0f;\n        const float gap = veryNarrow ? 4.0f : 6.0f;\n        // 28 px is the minimum practical toolbar hit target at JUCE logical scale.\n        const float h = mapOnlyMode ? 30.0f : (compactHeader ? 26.0f : 24.0f);\n''')
replace('Source/UI/SignalLabPage.h',
'''        auto buttonRow = compactHeader ? header.removeFromBottom (26.0f) : header;\n''',
'''        auto buttonRow = compactHeader ? header.removeFromBottom (mapOnlyMode ? 36.0f : 30.0f) : header;\n''')

# -----------------------------------------------------------------------------
# Reference Editor: premium minimum action heights. The previous 72px section
# left only a very shallow button after title and panel insets.
# -----------------------------------------------------------------------------
replace('Source/UI/ReferenceEditorDialog.h',
'''        auto utility = area.removeFromTop (34);\n''',
'''        auto utility = area.removeFromTop (40);\n''')
replace('Source/UI/ReferenceEditorDialog.h',
'''        actionSectionBounds = area.removeFromTop (72).toFloat();\n''',
'''        actionSectionBounds = area.removeFromTop (86).toFloat();\n''')
replace('Source/UI/ReferenceEditorDialog.h',
'''        auto actions = actionSectionBounds.toNearestInt().reduced (9, 7);\n        actions.removeFromTop (22);\n''',
'''        auto actions = actionSectionBounds.toNearestInt().reduced (9, 6);\n        actions.removeFromTop (22);\n        // Never collapse a primary action into a thin text strip.\n        if (actions.getHeight() > 42) actions = actions.withHeight (juce::jmax (36, actions.getHeight()));\n''')

# -----------------------------------------------------------------------------
# Compare: dedicate a real correction panel header, minimum-size buttons and
# enough vertical budget for metric title/value/bar rows.
# -----------------------------------------------------------------------------
replace('Source/UI/MatchCompareDialog.h',
'''        auto top = r.removeFromTop (38);\n''',
'''        auto top = r.removeFromTop (42);\n''')
replace('Source/UI/MatchCompareDialog.h',
'''        auto tune = r.removeFromTop (96).reduced (2, 3);\n        auto tuneButtons = tune.removeFromRight (310);\n        auto tuneRow1 = tuneButtons.removeFromTop (30);\n''',
'''        auto tune = r.removeFromTop (132).reduced (10, 7);\n        // Keep the POST-ANALYSIS title/status in a dedicated header band.\n        tune.removeFromTop (28);\n        auto tuneButtons = tune.removeFromRight (330);\n        auto tuneRow1 = tuneButtons.removeFromTop (36);\n''')
replace('Source/UI/MatchCompareDialog.h',
'''        auto tuneRow2 = tuneButtons.removeFromTop (30);\n''',
'''        tuneButtons.removeFromTop (4);\n        auto tuneRow2 = tuneButtons.removeFromTop (36);\n''')
replace('Source/UI/MatchCompareDialog.h',
'''            fineTuneLabels[i].setBounds (c.removeFromTop (16));\n            fineTune[i].setBounds (c.reduced (2, 0));\n''',
'''            fineTuneLabels[i].setBounds (c.removeFromTop (18));\n            fineTune[i].setBounds (c.reduced (3, 1));\n''')
replace('Source/UI/MatchCompareDialog.h',
'''        auto fineTunePanel = body.withTrimmedTop (82).withHeight (96).toFloat().reduced (1.0f);\n''',
'''        auto fineTunePanel = body.withTrimmedTop (86).withHeight (132).toFloat().reduced (1.0f);\n''')
replace('Source/UI/MatchCompareDialog.h',
'''        g.drawText (correctionStatus,\n                    fineTunePanel.withTrimmedLeft (fineTunePanel.getWidth() - 390.0f).withHeight (22.0f).reduced (4.0f, 0.0f),\n                    juce::Justification::centredRight, true);\n        body.removeFromTop (180);\n\n        auto waveArea = body.removeFromTop (body.getHeight() * 34 / 100).toFloat().reduced (3.0f);\n        auto spectrumArea = body.removeFromTop (body.getHeight() * 52 / 100).toFloat().reduced (3.0f);\n        auto metricsArea = body.toFloat().reduced (3.0f);\n''',
'''        g.drawText (correctionStatus,\n                    fineTunePanel.withTrimmedLeft (250.0f).withTrimmedRight (10.0f).withHeight (24.0f),\n                    juce::Justification::centredRight, true);\n        body.removeFromTop (224);\n\n        // Similarity is a decision surface, not a footer. Give its percentage,\n        // delta and progress bar independent vertical rows.\n        auto waveArea = body.removeFromTop (body.getHeight() * 30 / 100).toFloat().reduced (3.0f);\n        auto spectrumArea = body.removeFromTop (body.getHeight() * 56 / 100).toFloat().reduced (3.0f);\n        auto metricsArea = body.toFloat().reduced (3.0f);\n''')
# The percentages were still allowed to share a very small residual cell. Force
# a clear title row + numeric row + progress row and slightly tighter gutters.
replace('Source/UI/MatchCompareDialog.h',
'''            auto cell = juce::Rectangle<float> (r.getX() + (i % columns) * cw,\n                                                r.getY() + (i / columns) * rh,\n                                                cw, rh).reduced (7.0f, 5.0f);\n''',
'''            auto cell = juce::Rectangle<float> (r.getX() + (i % columns) * cw,\n                                                r.getY() + (i / columns) * rh,\n                                                cw, rh).reduced (5.0f, 4.0f);\n''')
replace('Source/UI/MatchCompareDialog.h',
'''            auto content = cell.reduced (10.0f, 6.0f);\n            auto titleArea = content.removeFromTop (18.0f);\n''',
'''            auto content = cell.reduced (10.0f, 7.0f);\n            auto titleArea = content.removeFromTop (16.0f);\n''')
replace('Source/UI/MatchCompareDialog.h',
'''            auto bar = content.removeFromBottom (8.0f);\n            auto valueArea = content;\n''',
'''            auto bar = content.removeFromBottom (9.0f);\n            content.removeFromBottom (5.0f);\n            auto valueArea = content;\n''')

# -----------------------------------------------------------------------------
# Persistent workspace: use available width instead of the old 420px hard cap,
# and lift primary buttons to proper minimum heights. Pop-out Reference Editor
# and Compare remain the progressive-disclosure detail surfaces.
# -----------------------------------------------------------------------------
replace('Source/UI/RetroMatchEditorV3.cpp',
'''    const int workspaceWidth = juce::jlimit (340, 420, (int) std::round (outer.getWidth() * 0.30));\n''',
'''    const int workspaceWidth = juce::jlimit (390, 520, (int) std::round (outer.getWidth() * 0.34));\n''')
replace('Source/UI/RetroMatchEditorV3.cpp',
'''    load.setBounds (w.removeFromTop (34));\n''',
'''    load.setBounds (w.removeFromTop (38));\n''')
replace('Source/UI/RetroMatchEditorV3.cpp',
'''    auto auditionRow = w.removeFromTop (30);\n''',
'''    auto auditionRow = w.removeFromTop (34);\n''')
replace('Source/UI/RetroMatchEditorV3.cpp',
'''    auto actionRow = w.removeFromTop (32);\n''',
'''    auto actionRow = w.removeFromTop (36);\n''')
replace('Source/UI/RetroMatchEditorV3.cpp',
'''    const int candidateGap = 5;\n    const int cardH = juce::jmax (24, (w.getHeight() - candidateGap * 2) / 3);\n''',
'''    const int candidateGap = 7;\n    const int cardH = juce::jmax (34, (w.getHeight() - candidateGap * 2) / 3);\n''')

print('premium UI layout pass applied')
