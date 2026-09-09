#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
path = ROOT / 'Source/UI/SignalLabPage.h'
text = path.read_text(encoding='utf-8')

def once(old: str, new: str) -> None:
    global text
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f'expected one anchor, found {count}: {old[:100]!r}')
    text = text.replace(old, new, 1)

once('''            if (const int output = hitTestModOutput (e.position); output >= 0)
            {
                connecting = true;
''', '''            if (const int output = hitTestModOutput (e.position); output >= 0)
            {
                connectionValidationMessage.clear();
                connecting = true;
''')

once('''        if (inputPort) modelNode.ports.push_back (PatchGraph::audioInput (id == "GLOBALBUS"));
        if (outputPort) modelNode.ports.push_back (PatchGraph::audioOutput());
        if (modInputPort) modelNode.ports.push_back (PatchGraph::modulationInput());
''', '''        const bool ownsAudioPorts = role != NodeRole::clock && role != NodeRole::modHub;
        if (inputPort && ownsAudioPorts) modelNode.ports.push_back (PatchGraph::audioInput (id == "GLOBALBUS"));
        if (outputPort && ownsAudioPorts) modelNode.ports.push_back (PatchGraph::audioOutput());
        if (modInputPort) modelNode.ports.push_back (PatchGraph::modulationInput());
''')

path.write_text(text, encoding='utf-8')
print('typed graph review fixes applied')
