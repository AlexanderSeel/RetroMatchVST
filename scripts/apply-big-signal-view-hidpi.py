#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
path = root / "Source/UI/SignalLabPage.h"
text = path.read_text(encoding="utf-8")
old = '''        auto* dialogWindow = new juce::DialogWindow ("RM-01 / LARGE PATCH MAP",\n                                                     juce::Colour (0xff101719), true, true);\n'''
new = '''        const float desktopScale = juce::Component::getApproximateScaleFactorForComponent (this);\n        auto* dialogWindow = new juce::DialogWindow ("RM-01 / LARGE PATCH MAP",\n                                                     juce::Colour (0xff101719), true, true, desktopScale);\n'''
if text.count(old) != 1:
    raise RuntimeError(f"expected one DialogWindow constructor, found {text.count(old)}")
path.write_text(text.replace(old, new, 1), encoding="utf-8")

static = root / "scripts/static-check.py"
s = static.read_text(encoding="utf-8")
old_token = "'setContentOwned (content, false)', 'mapOnlyMode || header.getHeight() > 40.0f'"
new_token = "'setContentOwned (content, false)', 'getApproximateScaleFactorForComponent', 'mapOnlyMode || header.getHeight() > 40.0f'"
if s.count(old_token) != 1:
    raise RuntimeError("responsive static contract not found")
static.write_text(s.replace(old_token, new_token, 1), encoding="utf-8")
print("Applied large-map HiDPI propagation")
