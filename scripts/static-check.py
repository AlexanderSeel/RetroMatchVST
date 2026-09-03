#!/usr/bin/env python3
"""Fast source-integrity checks that do not require JUCE or a compiler."""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
errors: list[str] = []

def stripped_cpp(text: str) -> str:
    out = []
    i = 0
    state = "code"
    while i < len(text):
        c = text[i]
        n = text[i + 1] if i + 1 < len(text) else ""
        if state == "code":
            if c == '/' and n == '/': state = "line"; out += "  "; i += 2; continue
            if c == '/' and n == '*': state = "block"; out += "  "; i += 2; continue
            if c == '"': state = "string"; out.append(' '); i += 1; continue
            if c == "'": state = "char"; out.append(' '); i += 1; continue
            out.append(c); i += 1; continue
        if state == "line":
            if c == '\n': state = "code"; out.append('\n')
            else: out.append(' ')
            i += 1; continue
        if state == "block":
            if c == '*' and n == '/': state = "code"; out += "  "; i += 2
            else: out.append('\n' if c == '\n' else ' '); i += 1
            continue
        if state in ("string", "char"):
            quote = '"' if state == "string" else "'"
            if c == '\\': out += "  "; i += min(2, len(text) - i); continue
            if c == quote: state = "code"
            out.append('\n' if c == '\n' else ' '); i += 1
    return ''.join(out)

def check_balance(path: Path) -> None:
    text = stripped_cpp(path.read_text(encoding='utf-8'))
    pairs = {')': '(', ']': '[', '}': '{'}
    stack: list[tuple[str, int]] = []
    for pos, c in enumerate(text):
        if c in '([{': stack.append((c, pos))
        elif c in ')]}':
            if not stack or stack[-1][0] != pairs[c]:
                errors.append(f"{path.relative_to(ROOT)}: unmatched {c} near byte {pos}")
                return
            stack.pop()
    if stack:
        errors.append(f"{path.relative_to(ROOT)}: unclosed {stack[-1][0]} near byte {stack[-1][1]}")

for source in list((ROOT / 'Source').rglob('*.cpp')) + list((ROOT / 'Source').rglob('*.h')) + list((ROOT / 'Tests').rglob('*.cpp')):
    check_balance(source)

cmake = (ROOT / 'CMakeLists.txt').read_text(encoding='utf-8')
if 'VERSION 1.0.0 LANGUAGES C CXX' not in cmake:
    errors.append('CMakeLists.txt must enable C and CXX for JUCE 9.0.1 and declare v1.0.0')
if 'Source/UI/RetroMatchEditorV3.cpp' not in cmake or 'Source/AI/AISeedProvider.cpp' not in cmake:
    errors.append('CMakeLists.txt must build the workflow-first editor and AI seed provider')

processor = (ROOT / 'Source/PluginProcessor.cpp').read_text(encoding='utf-8')
processor_h = (ROOT / 'Source/PluginProcessor.h').read_text(encoding='utf-8')
engine_h = (ROOT / 'Source/Engine/SynthEngine.h').read_text(encoding='utf-8')
engine_cpp = (ROOT / 'Source/Engine/SynthEngine.cpp').read_text(encoding='utf-8')
matcher = (ROOT / 'Source/Matching/SoundMatcher.cpp').read_text(encoding='utf-8')
editor = (ROOT / 'Source/UI/RetroMatchEditorV3.cpp').read_text(encoding='utf-8')
editor_h = (ROOT / 'Source/UI/RetroMatchEditorV3.h').read_text(encoding='utf-8')
ai = (ROOT / 'Source/AI/AISeedProvider.cpp').read_text(encoding='utf-8')
ai_settings = (ROOT / 'Source/AI/AISettings.cpp').read_text(encoding='utf-8')
editor_all = editor + editor_h

required_tokens = {
    'processor wavetable parameters': ['"wavetableMix"', '"wavetablePosition"', '"wavetableWarp"', '"supersawMix"', '"unisonDetune"', '"unisonSpread"', '"wavefold"'],
    'FM detail parameters': ['"FixedHz"', '"Attack"', '"Decay"', '"Sustain"', '"Release"', '"KeyScale"', '"Velocity"'],
    'engine wavetable/unison/fold': ['wavetableWave', 'renderSupersaw', 'foldSample'],
    'matcher search dimensions': ['p.wavetableMix', 'p.supersawMix', 'p.wavefold', 'p.fmOpFixedMode', 'p.fmOpAttack'],
    'operator UI': ['rebindFmOperatorEditor', 'SELECTED OPERATOR DETAIL'],
    'editing tabs': ['tabs.addTab ("SYNTH"', 'tabs.addTab ("FM"', 'tabs.addTab ("FILTER + AMP"', 'tabs.addTab ("MOD"', 'tabs.addTab ("FX"', 'tabs.addTab ("SETTINGS"'],
    'reference matching workspace': ['REFERENCE + RESYNTH WORKSPACE', 'QUICK x3', 'REFINE x3', 'AI x3', 'ANALYZE', 'VARIANTS'],
    'variant cards': ['CandidateButton', 'NATURAL', 'FM / HARMONIC', 'WT / TEXTURE', 'createLocalVariants', 'finishVariantSearch'],
    'virtual keyboard': ['MidiKeyboardComponent', 'handleNoteOn', 'handleNoteOff', 'KEYS'],
    'AI provider settings': ['OpenAI', 'Google Gemini', 'OpenAI-compatible / Azure', 'GitHub Copilot bridge', 'SESSION API KEY'],
    'AI local scoring': ['buildPrompt', 'postJson', 'SoundMatcher::evaluateFit', 'generateVariants'],
    'v1 reference wavetable': ['referenceWavetableMix', 'ReferenceWavetableExtractor', 'referenceWavetable'],
    'v1 candidate bank': ['buildCandidateBank', 'morphCandidates', 'selectCandidate'],
}
texts = {
    'processor wavetable parameters': processor,
    'FM detail parameters': processor,
    'engine wavetable/unison/fold': engine_cpp,
    'matcher search dimensions': matcher,
    'operator UI': editor_all,
    'editing tabs': editor_all,
    'reference matching workspace': editor_all,
    'variant cards': editor_all,
    'virtual keyboard': editor_all + processor_h + engine_h,
    'AI provider settings': editor_all + ai_settings,
    'AI local scoring': ai,
    'v1 reference wavetable': processor + engine_cpp,
    'v1 candidate bank': processor + editor_all,
}
for name, tokens in required_tokens.items():
    for token in tokens:
        if token not in texts[name]: errors.append(f'{name}: missing {token}')

if 'ModDestination::wavefold' not in engine_cpp or 'wavetablePosition' not in engine_h:
    errors.append('new modulation destinations are not wired through the engine')
if 'presetVersion", "1.0"' not in processor:
    errors.append('preset version was not bumped to 1.0')
if 'noteOnFromUi' not in engine_h or 'noteOnFromEditor' not in processor_h:
    errors.append('virtual keyboard is not wired through a safe synth audition path')
if 'sessionApiKey' not in ai_settings or 'setValue ("ai.' not in ai_settings:
    errors.append('AI settings persistence/session-secret separation is missing')

if errors:
    print('RetroMatch static checks FAILED')
    for e in errors: print(' -', e)
    sys.exit(1)

print('RetroMatch static checks passed')
print(' - C/C++ JUCE project configuration present')
print(' - source delimiter balance passed')
print(' - wavetable, supersaw/unison, wavefold and FM-detail plumbing present')
print(' - reference-to-variant workspace and editing tabs present')
print(' - Quick/Refine/AI three-variant workflow present')
print(' - optional virtual keyboard audition path present')
print(' - AI provider settings and local candidate scoring present')
