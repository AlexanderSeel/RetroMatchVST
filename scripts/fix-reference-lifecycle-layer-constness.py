#!/usr/bin/env python3
from pathlib import Path

path = Path(__file__).resolve().parents[1] / "Source/Matching/SoundMatcher.cpp"
text = path.read_text(encoding="utf-8")
old = '''    for (auto& layer : p.layers)\n    {\n        if (! layer) continue;\n        layer = std::make_shared<VoiceParameters> (*layer);\n        enforceSelfTerminatingTree (*layer, reference, depth + 1);\n    }\n'''
new = '''    for (auto& layer : p.layers)\n    {\n        if (! layer) continue;\n\n        // Stored rack layers are shared_ptr<const VoiceParameters>. Normalise a mutable\n        // clone first, then publish it back as const so candidate trees remain immutable\n        // to their consumers and no shared layer is modified through another candidate.\n        auto mutableLayer = std::make_shared<VoiceParameters> (*layer);\n        enforceSelfTerminatingTree (*mutableLayer, reference, depth + 1);\n        layer = std::move (mutableLayer);\n    }\n'''
count = text.count(old)
if count != 1:
    raise RuntimeError(f"expected one lifecycle layer loop, found {count}")
path.write_text(text.replace(old, new, 1), encoding="utf-8")
print("Fixed recursive lifecycle normalisation for const rack-layer snapshots")
