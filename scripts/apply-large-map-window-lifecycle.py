#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
signal = ROOT / "Source/UI/SignalLabPage.h"
static = ROOT / "scripts/static-check.py"

text = signal.read_text(encoding="utf-8")
anchor = '''    static constexpr float kReadableGraphZoom = 0.50f;\n'''
insert = '''    class LargePatchMapWindow final : public juce::DialogWindow\n    {\n    public:\n        explicit LargePatchMapWindow (float desktopScale)\n            : juce::DialogWindow ("RM-01 / LARGE PATCH MAP",\n                                  juce::Colour (0xff101719), true, true, desktopScale)\n        {\n        }\n\n        void closeButtonPressed() override\n        {\n            exitModalState (0);\n        }\n    };\n\n    static constexpr float kReadableGraphZoom = 0.50f;\n'''
if text.count(anchor) != 1:
    raise RuntimeError(f"LargePatchMapWindow insertion point: expected 1, found {text.count(anchor)}")
text = text.replace(anchor, insert, 1)

old = '''        auto* dialogWindow = new juce::DialogWindow ("RM-01 / LARGE PATCH MAP",\n                                                     juce::Colour (0xff101719), true, true, desktopScale);\n'''
new = '''        auto* dialogWindow = new LargePatchMapWindow (desktopScale);\n'''
if text.count(old) != 1:
    raise RuntimeError(f"DialogWindow construction: expected 1, found {text.count(old)}")
text = text.replace(old, new, 1)
signal.write_text(text, encoding="utf-8")

s = static.read_text(encoding="utf-8")
old_contract = "'responsive large signal map': ['bigViewNeedsFit', 'applyFitToView', 'setContentOwned (content, false)', 'getApproximateScaleFactorForComponent', 'mapOnlyMode || header.getHeight() > 40.0f'],"
new_contract = "'responsive large signal map': ['bigViewNeedsFit', 'applyFitToView', 'LargePatchMapWindow', 'closeButtonPressed', 'setContentOwned (content, false)', 'getApproximateScaleFactorForComponent', 'mapOnlyMode || header.getHeight() > 40.0f'],"
if s.count(old_contract) != 1:
    raise RuntimeError("responsive large signal-map static contract not found")
s = s.replace(old_contract, new_contract, 1)
static.write_text(s, encoding="utf-8")

print("Applied explicit LargePatchMapWindow close lifecycle")
