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
if 'Source/Reference/ReferenceSamplePlayer.cpp' not in cmake:
    errors.append('CMakeLists.txt must build the reference sample audition player')

mseg_path = ROOT / 'Source/Engine/MSEG.h'
mseg_page_path = ROOT / 'Source/UI/MsegPage.h'
user_wavetable_page_path = ROOT / 'Source/UI/UserWavetablePage.h'
if not mseg_path.exists(): errors.append('Source/Engine/MSEG.h is missing')
if not mseg_page_path.exists(): errors.append('Source/UI/MsegPage.h is missing')
if not user_wavetable_page_path.exists(): errors.append('Source/UI/UserWavetablePage.h is missing')

processor = (ROOT / 'Source/PluginProcessor.cpp').read_text(encoding='utf-8')
processor_h = (ROOT / 'Source/PluginProcessor.h').read_text(encoding='utf-8')
engine_h = (ROOT / 'Source/Engine/SynthEngine.h').read_text(encoding='utf-8')
engine_cpp = (ROOT / 'Source/Engine/SynthEngine.cpp').read_text(encoding='utf-8')
reference_wavetable_h = (ROOT / 'Source/Engine/ReferenceWavetable.h').read_text(encoding='utf-8')
reference_wavetable_cpp = (ROOT / 'Source/Engine/ReferenceWavetable.cpp').read_text(encoding='utf-8')
mseg = mseg_path.read_text(encoding='utf-8') if mseg_path.exists() else ''
mseg_page = mseg_page_path.read_text(encoding='utf-8') if mseg_page_path.exists() else ''
user_wavetable_page = user_wavetable_page_path.read_text(encoding='utf-8') if user_wavetable_page_path.exists() else ''
matcher = (ROOT / 'Source/Matching/SoundMatcher.cpp').read_text(encoding='utf-8')
analyzer = (ROOT / 'Source/Analysis/SampleAnalyzer.cpp').read_text(encoding='utf-8')
analyzer_h = (ROOT / 'Source/Analysis/SampleAnalyzer.h').read_text(encoding='utf-8')
reference_player = (ROOT / 'Source/Reference/ReferenceSamplePlayer.cpp').read_text(encoding='utf-8')
editor = (ROOT / 'Source/UI/RetroMatchEditorV3.cpp').read_text(encoding='utf-8')
editor_h = (ROOT / 'Source/UI/RetroMatchEditorV3.h').read_text(encoding='utf-8')
ai = (ROOT / 'Source/AI/AISeedProvider.cpp').read_text(encoding='utf-8')
ai_settings = (ROOT / 'Source/AI/AISettings.cpp').read_text(encoding='utf-8')
editor_all = editor + editor_h

required_tokens = {
    'processor wavetable parameters': ['"wavetableMix"', '"wavetablePosition"', '"wavetableWarp"', '"supersawMix"', '"unisonDetune"', '"unisonSpread"', '"wavefold"'],
    'FM detail parameters': ['"FixedHz"', '"Attack"', '"Decay"', '"Sustain"', '"Release"', '"KeyScale"', '"Velocity"'],
    'engine wavetable/unison/fold': ['wavetableWave', 'renderSupersaw', 'foldSample'],
    'nonlinear oversampling engine': ['Oversampling<float>', 'processSamplesUp', 'processSamplesDown', 'fixedLatencySamples', 'compensateLatency'],
    'nonlinear oversampling processor': ['"oversamplingQuality"', 'setLatencySamples (engine.getLatencySamples())', 'referenceLatencyDelay', 'stateWithPost10Defaults'],
    'MSEG engine': ['MsegParameters', 'MultiSegmentEnvelope', 'loopStartPoint', 'loopEndPoint', 'noteOn()', 'noteOff()', 'shapeProgress'],
    'MSEG voice routing': ['mseg.setSampleRate', 'mseg.setParameters', 'mseg.noteOn', 'mseg.noteOff', 'params.modGraphSlots', 'ModSource::mseg1'],
    'MSEG processor state': ['"msegEnabled"', '"msegLoopEnabled"', '"msegLevel"', '"msegTime"', '"msegCurve"', '"modGraph"'],
    'MSEG editor': ['ENABLE MSEG 1', 'LOOP WHILE NOTE HELD', 'POST-1.0 MODULATION GRAPH', 'MSEG 1', 'modGraph'],
    'user wavetable importer': ['importSet', 'importSetFromBuffer', 'chooseSourceFrameSize', 'cyclicFrameSample', 'source frame', '5 x 2048'],
    'user wavetable engine': ['userWavetableMix', 'userWavetable', 'params.userWavetable->sample'],
    'user wavetable processor': ['"userWavetableMix"', 'loadUserWavetable', 'clearUserWavetable', '"userWavetable"', 'userWavetableDescription', 'presetVersion", "1.3"'],
    'user wavetable editor': ['LOAD WAVETABLE', 'SOURCE CYCLE', 'USER WT MIX', 'AUTO (prefer 2048)', 'getUserWavetable'],
    'matcher search dimensions': ['p.wavetableMix', 'p.supersawMix', 'p.wavefold', 'p.fmOpFixedMode', 'p.fmOpAttack'],
    'operator UI': ['rebindFmOperatorEditor', 'SELECTED OPERATOR DETAIL'],
    'editing tabs': ['tabs.addTab ("SYNTH"', 'tabs.addTab ("FM"', 'tabs.addTab ("FILTER + AMP"', 'tabs.addTab ("MOD"', 'tabs.addTab ("FX"', 'tabs.addTab ("SETTINGS"'],
    'reference matching workspace': ['REFERENCE + RESYNTH WORKSPACE', 'QUICK x3', 'REFINE x3', 'AI x3', 'ANALYZE', 'VARIANTS'],
    'variant cards': ['CandidateButton', 'NATURAL', 'FM / HARMONIC', 'WT / TEXTURE', 'createLocalVariants', 'finishVariantSearch'],
    'virtual keyboard': ['MidiKeyboardComponent', 'handleNoteOn', 'handleNoteOff', 'KEYS'],
    'reference audition UI': ['BASE NOTE', 'DETECTED', 'REF SOLO', 'auditionReference', 'auditionMix', 'referenceLevel'],
    'reference audition engine': ['ReferenceAuditionMode', 'referencePlayer.render', 'setReferenceBaseMidiNote', 'referenceAuditionLevel'],
    'reference pitch correction': ['expectedFundamentalHz', 'estimateConfidenceAtFundamental', 'analyzeFile (loadedReferenceFile, expectedHz)'],
    'reference sample transposition': ['SamplerSound', 'rootNote.load()', 'noteOnFromUi', 'renderNextBlock'],
    'AI provider settings': ['OpenAI', 'Google Gemini', 'OpenAI-compatible / Azure', 'GitHub Copilot bridge', 'SESSION API KEY'],
    'AI local scoring': ['buildPrompt', 'postJson', 'SoundMatcher::evaluateFit', 'generateVariants'],
    'v1 reference wavetable': ['referenceWavetableMix', 'ReferenceWavetableExtractor', 'referenceWavetable'],
    'v1 candidate bank': ['buildCandidateBank', 'morphCandidates', 'selectCandidate'],
}
texts = {
    'processor wavetable parameters': processor,
    'FM detail parameters': processor,
    'engine wavetable/unison/fold': engine_cpp,
    'nonlinear oversampling engine': engine_h + engine_cpp,
    'nonlinear oversampling processor': processor + processor_h,
    'MSEG engine': mseg,
    'MSEG voice routing': engine_h + engine_cpp,
    'MSEG processor state': processor,
    'MSEG editor': processor + mseg_page,
    'user wavetable importer': reference_wavetable_h + reference_wavetable_cpp,
    'user wavetable engine': engine_h + engine_cpp,
    'user wavetable processor': processor + processor_h,
    'user wavetable editor': processor + user_wavetable_page,
    'matcher search dimensions': matcher,
    'operator UI': editor_all,
    'editing tabs': editor_all,
    'reference matching workspace': editor_all,
    'variant cards': editor_all,
    'virtual keyboard': editor_all + processor_h + engine_h,
    'reference audition UI': editor_all,
    'reference audition engine': processor + processor_h,
    'reference pitch correction': analyzer + analyzer_h + processor,
    'reference sample transposition': reference_player,
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
if 'noteOnFromUi' not in engine_h or 'noteOnFromEditor' not in processor_h:
    errors.append('virtual keyboard is not wired through a safe synth audition path')
if 'sessionApiKey' not in ai_settings or 'setValue ("ai.' not in ai_settings:
    errors.append('AI settings persistence/session-secret separation is missing')

# Host automation compatibility is append-only. The v1.0 outputGain endpoint must
# remain before post-1.0 quality/MSEG additions, and userWavetableMix must be the
# newest parameter after the existing graph surface.
output_gain_position = processor.find('"outputGain", "Output Gain"')
oversampling_position = processor.find('"oversamplingQuality", "Nonlinear Oversampling"')
mseg_position = processor.find('"msegEnabled", "MSEG 1 Enabled"')
graph_amount_position = processor.rfind('"modGraph" + index + "Amount"')
user_wavetable_position = processor.find('"userWavetableMix", "User Wavetable Mix"')
if output_gain_position < 0 or oversampling_position <= output_gain_position:
    errors.append('oversamplingQuality must be appended after the complete v1.0 parameter surface')
if mseg_position <= oversampling_position:
    errors.append('MSEG parameters must remain appended after oversamplingQuality')
if graph_amount_position < 0 or user_wavetable_position <= graph_amount_position:
    errors.append('userWavetableMix must remain appended after the complete MSEG/graph parameter surface')

# Never append MSEG to the original mod slot source choices: adding an item would
# change the normalized automation values of existing choice parameters. MSEG is
# available only in the new modGraph choices.
legacy_sources_literal = 'const juce::StringArray modSources { "Off", "LFO 1", "Velocity", "Key Track", "Random Note", "Amp Env" };'
if legacy_sources_literal not in processor:
    errors.append('legacy modSources choice list changed; v1.0 normalized automation compatibility would be at risk')
graph_sources_literal = 'const juce::StringArray graphSources { "Off", "LFO 1", "Velocity", "Key Track", "Random Note", "Amp Env", "MSEG 1" };'
if graph_sources_literal not in processor:
    errors.append('new modulation graph must expose MSEG 1 without changing legacy modSources')

if 'tabbed->addTab ("MSEG"' not in processor or 'new MsegPage (proc.apvts)' not in processor:
    errors.append('dedicated MSEG editor tab is not attached to the processor editor')
if 'tabbed->addTab ("WAVETABLE"' not in processor or 'new UserWavetablePage (proc)' not in processor:
    errors.append('dedicated user wavetable editor tab is not attached to the processor editor')
if 'stateWithPost10Defaults' not in processor:
    errors.append('post-1.0 preset/session migration helper is missing')

# OpenAI Responses models in the GPT-5.x family may reject temperature. Keep
# the OpenAI request conservative instead of sending an unsupported knob.
openai_start = ai.find('juce::String makeOpenAIRequest')
gemini_start = ai.find('juce::String makeGeminiRequest')
if openai_start < 0 or gemini_start <= openai_start:
    errors.append('OpenAI request builder could not be located')
else:
    openai_request = ai[openai_start:gemini_start]
    if 'setProperty ("temperature"' in openai_request:
        errors.append('OpenAI Responses request must not blindly send temperature')

if errors:
    print('RetroMatch static checks FAILED')
    for e in errors: print(' -', e)
    sys.exit(1)

print('RetroMatch static checks passed')
print(' - C/C++ JUCE project configuration present')
print(' - source delimiter balance passed')
print(' - wavetable, supersaw/unison, wavefold and FM-detail plumbing present')
print(' - nonlinear 1x/2x/4x oversampling and fixed-latency plumbing present')
print(' - oversamplingQuality remains appended after the v1.0 parameter surface')
print(' - six-point MSEG engine and append-only modulation graph present')
print(' - legacy MOD choice ranges remain unchanged; MSEG routing is isolated to new graph slots')
print(' - arbitrary user wavetable import, embedded state and separate oscillator layer present')
print(' - userWavetableMix remains appended after the existing post-1.0 graph surface')
print(' - dedicated MSEG and WAVETABLE editor tabs present')
print(' - reference-to-variant workspace and editing tabs present')
print(' - Quick/Refine/AI three-variant workflow present')
print(' - optional virtual keyboard audition path present')
print(' - reference sample solo/mix audition and editable detected base note present')
print(' - OpenAI request omits unsupported temperature sampling control')
print(' - AI provider settings and local candidate scoring present')
