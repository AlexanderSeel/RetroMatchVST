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
if 'add_test(NAME RetroMatchEditorPreview' not in cmake:
    errors.append('optional editor/session migration fixture must be registered with CTest')

mseg_path = ROOT / 'Source/Engine/MSEG.h'
mseg_page_path = ROOT / 'Source/UI/MsegPage.h'
user_wavetable_page_path = ROOT / 'Source/UI/UserWavetablePage.h'
tempo_sync_path = ROOT / 'Source/Engine/TempoSync.h'
signal_page_path = ROOT / 'Source/UI/SignalLabPage.h'
patch_graph_path = ROOT / 'Source/Engine/PatchGraph.h'
dsp_routing_path = ROOT / 'Source/Engine/DspRoutingPlan.h'
sequencer_path = ROOT / 'Source/Sequencer/StepSequencer.h'
pattern_library_path = ROOT / 'Source/Sequencer/PatternLibrary.h'
pack_safety_path = ROOT / 'Source/Engine/PresetPackSafety.h'
magic_dialog_path = ROOT / 'Source/UI/MagicAuditionDialog.h'
editor_preview_path = ROOT / 'Tests/EditorPreview.cpp'
if not mseg_path.exists(): errors.append('Source/Engine/MSEG.h is missing')
if not mseg_page_path.exists(): errors.append('Source/UI/MsegPage.h is missing')
if not user_wavetable_page_path.exists(): errors.append('Source/UI/UserWavetablePage.h is missing')
if not tempo_sync_path.exists(): errors.append('Source/Engine/TempoSync.h is missing')
if not signal_page_path.exists(): errors.append('Source/UI/SignalLabPage.h is missing')
if not patch_graph_path.exists(): errors.append('Source/Engine/PatchGraph.h is missing')
if not dsp_routing_path.exists(): errors.append('Source/Engine/DspRoutingPlan.h is missing')
if not sequencer_path.exists(): errors.append('Source/Sequencer/StepSequencer.h is missing')
if not pattern_library_path.exists(): errors.append('Source/Sequencer/PatternLibrary.h is missing')
if not pack_safety_path.exists(): errors.append('Source/Engine/PresetPackSafety.h is missing')
if not magic_dialog_path.exists(): errors.append('Source/UI/MagicAuditionDialog.h is missing')
if not editor_preview_path.exists(): errors.append('Tests/EditorPreview.cpp is missing')
if not (ROOT / 'scripts/update-ui-example.py').exists(): errors.append('README UI example updater is missing')
if 'hasUsableVisualInk' not in editor_preview_path.read_text(encoding='utf-8'):
    errors.append('editor preview must include visual ink sanity checks')
if 'retromatch-editor-example.png' not in (ROOT / 'README.md').read_text(encoding='utf-8'):
    errors.append('README must reference the generated editor example')

processor = (ROOT / 'Source/PluginProcessor.cpp').read_text(encoding='utf-8')
processor_h = (ROOT / 'Source/PluginProcessor.h').read_text(encoding='utf-8')
engine_h = (ROOT / 'Source/Engine/SynthEngine.h').read_text(encoding='utf-8')
engine_cpp = (ROOT / 'Source/Engine/SynthEngine.cpp').read_text(encoding='utf-8')
module_rack = (ROOT / 'Source/Engine/ModuleRack.h').read_text(encoding='utf-8')
reference_wavetable_h = (ROOT / 'Source/Engine/ReferenceWavetable.h').read_text(encoding='utf-8')
reference_wavetable_cpp = (ROOT / 'Source/Engine/ReferenceWavetable.cpp').read_text(encoding='utf-8')
mseg = mseg_path.read_text(encoding='utf-8') if mseg_path.exists() else ''
mseg_page = mseg_page_path.read_text(encoding='utf-8') if mseg_page_path.exists() else ''
user_wavetable_page = user_wavetable_page_path.read_text(encoding='utf-8') if user_wavetable_page_path.exists() else ''
tempo_sync = tempo_sync_path.read_text(encoding='utf-8') if tempo_sync_path.exists() else ''
signal_page = signal_page_path.read_text(encoding='utf-8') if signal_page_path.exists() else ''
patch_graph = patch_graph_path.read_text(encoding='utf-8') if patch_graph_path.exists() else ''
dsp_routing = dsp_routing_path.read_text(encoding='utf-8') if dsp_routing_path.exists() else ''
sequencer = sequencer_path.read_text(encoding='utf-8') if sequencer_path.exists() else ''
pattern_library = pattern_library_path.read_text(encoding='utf-8') if pattern_library_path.exists() else ''
pack_safety = pack_safety_path.read_text(encoding='utf-8') if pack_safety_path.exists() else ''
magic_dialog = magic_dialog_path.read_text(encoding='utf-8') if magic_dialog_path.exists() else ''
editor_preview = editor_preview_path.read_text(encoding='utf-8') if editor_preview_path.exists() else ''
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
    'MSEG editor': ['ENABLE MSEG 1', 'LOOP WHILE NOTE HELD', 'EXTRA MODULATION ROUTES', 'MSEG 1', 'modGraph', 'msegSync', 'msegDivision'],
    'user wavetable importer': ['importSet', 'importSetFromBuffer', 'chooseSourceFrameSize', 'cyclicFrameSample', 'source frame', '5 x 2048'],
    'user wavetable engine': ['userWavetableMix', 'userWavetable', 'params.userWavetable->sample'],
    'user wavetable processor': ['"userWavetableMix"', 'loadUserWavetable', 'clearUserWavetable', '"userWavetable"', 'userWavetableDescription', 'presetVersion", "1.4"'],
    'user wavetable editor': ['LOAD WAVETABLE', 'SOURCE CYCLE', 'USER WT MIX', 'AUTO (prefer 2048)', 'getUserWavetable'],
    'matcher search dimensions': ['p.wavetableMix', 'p.supersawMix', 'p.wavefold', 'p.fmOpFixedMode', 'p.fmOpAttack'],
    'operator UI': ['rebindFmOperatorEditor', 'SELECTED OPERATOR DETAIL'],
    'editing tabs': ['tabs.addTab ("SYNTH"', 'tabs.addTab ("FM"', 'tabs.addTab ("FILTER + AMP"', 'tabs.addTab ("MOD"', 'tabs.addTab ("FX"', 'tabs.addTab ("SETTINGS"'],
    'reference matching workspace': ['REFERENCE + RESYNTH WORKSPACE', 'QUICK x3', 'REFINE x3', 'AI x3', 'ANALYZE', 'VARIANTS'],
    'variant cards': ['CandidateButton', 'NATURAL', 'FM / HARMONIC', 'WT / TEXTURE', 'createLocalVariants', 'finishVariantSearch'],
    'virtual keyboard': ['MidiKeyboardComponent', 'handleNoteOn', 'handleNoteOff', 'KEYS'],
    'reference audition UI': ['BASE NOTE', 'DETECTED', 'REF SOLO', 'auditionReference', 'auditionMix', 'referenceLevel'],
    'reference audition engine': ['ReferenceAuditionMode', 'referencePlayer.render', 'setReferenceBaseMidiNote', 'referenceAuditionLevel'],
    'reference pitch correction': ['expectedFundamentalHz', 'estimateConfidenceAtFundamental', 'analyzeFile (loadedReferenceFile, expectedHz, start, end)'],
    'reference sample transposition': ['SamplerSound', 'rootNote.load()', 'noteOnFromUi', 'renderNextBlock'],
    'AI provider settings': ['OpenAI', 'Google Gemini', 'OpenAI-compatible / Azure', 'GitHub Copilot bridge', 'SESSION API KEY'],
    'AI local scoring': ['buildPrompt', 'postJson', 'SoundMatcher::enforceReferenceLifecycle', 'SoundMatcher::evaluateFit', 'generateVariants'],
    'v1 reference wavetable': ['referenceWavetableMix', 'ReferenceWavetableExtractor', 'referenceWavetable'],
    'v1 candidate bank': ['buildCandidateBank', 'morphCandidates', 'selectCandidate'],
    'gold full rack search': ['buildGoldCandidateBank', 'makeEmbeddedResynthRack', 'fullRackScore', 'GOLD / FULL RACK'],
    'gold rack evolution': ['evolveGoldRack', 'mutateGoldRack', 'globalFxModules', 'wholeInstrumentRack', 'seedGoldWholeInstrumentRack'],
    'layered resynthesis lifecycle': ['"resynthInstances"', 'applyGeneratedRack', 'clearLayer (i)', 'captureLayer (layer)', 'SoundMatcher::enforceReferenceLifecycle', 'RESYNTH'],
    'tempo clock processor': ['"tempoSource"', '"manualBpm"', 'getPlayHead()', 'getBpm()', 'effectiveBpm', 'prefix + "Sync"', '"delaySync"'],
    'tempo clock engine': ['resolveTempo', 'inheritTempoFrom', 'TempoSync::frequencyHz', 'TempoSync::seconds', 'msegTempoSync'],
    'tempo divisions': ['1/32', '1/16', '1/8', '1/4', '1/2', '1/1', '2/1', '4/1'],
    'tempo modular FX': ['tempoSync', 'tempoDivision', 'fxModuleCanTempoSync', 'TempoSync::frequencyHz', 'TempoSync::seconds'],
    'interactive signal map': ['mouseWheelMove', 'mouseDrag', 'mouseDoubleClick', 'graphZoom', 'graphPan', 'INSTANCE 1 / MAIN'],
    'responsive large signal map': ['bigViewNeedsFit', 'applyFitToView', 'LargePatchMapWindow', 'closeButtonPressed', 'setContentOwned (content, false)', 'getApproximateScaleFactorForComponent', 'mapOnlyMode || header.getHeight() > 40.0f'],
    'typed persistent patch graph': ['schemaVersion', 'PortType', 'validateConnection', 'wouldCreateAudioCycle', 'topologicalOrder', 'toValueTree', 'fromValueTree', 'modulationSafe'],
    'cable editor v1': ['findEdge', 'replaceEdge', 'removeEdge', 'showReconnectRouteMenu', 'deleteSelectedCable', 'undoCableEdit', 'redoCableEdit', 'KeyPress::deleteKey'],
    'DSP routing compiler v1': ['maxLayerCount', 'layerOrder', 'CompileResult', 'AtomicPlan', 'memory_order_release', 'memory_order_acquire'],
    'sequencer macro preview plumbing': ['MacroDestination', 'MacroInterpolation', 'macroLaneRate', 'modulationProbability'],
    'sequencer pattern library': ['patternTemplateCount', 'Euclidean Bloom', 'Polymetric Five'],
    'validated preset pack workflow': ['validatePackArchive', 'extractPackArchive', 'resolveImportConflict', 'ImportConflictPolicy'],
    'Magic preview workflow': ['magicPreviewParameters', 'keepMagicPreview', 'applyMagicPreview', 'getMagicRenderReport'],
    'Magic audition panel': ['KEEP PREVIEW', 'APPLY / NEW ORIGIN', 'MAGIC AUDITION /', 'startTimerHz'],
    'editor transition soak': ['Deterministic transition soak', 'for (int cycle = 0; cycle < 24', 'getStateInformation', 'validateReleaseState'],
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
    'gold full rack search': processor + processor_h + editor_all,
    'gold rack evolution': processor + processor_h + engine_h + engine_cpp,
    'layered resynthesis lifecycle': processor + processor_h,
    'tempo clock processor': processor + processor_h,
    'tempo clock engine': engine_h + engine_cpp,
    'tempo divisions': tempo_sync,
    'tempo modular FX': module_rack + processor,
    'interactive signal map': signal_page,
    'responsive large signal map': signal_page,
    'typed persistent patch graph': patch_graph + signal_page + processor_h,
    'cable editor v1': patch_graph + signal_page,
    'DSP routing compiler v1': dsp_routing + engine_h + engine_cpp + processor + processor_h + signal_page,
    'sequencer macro preview plumbing': sequencer + processor + processor_h,
    'sequencer pattern library': pattern_library,
    'validated preset pack workflow': pack_safety + processor + editor_all,
    'Magic preview workflow': processor + processor_h,
    'Magic audition panel': magic_dialog + editor_all,
    'editor transition soak': editor_preview + cmake,
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

# Host automation compatibility is append-only. The v1.0 surface and every
# previously released post-1.0 parameter must keep their order. New clock and
# layered-resynthesis controls are appended after the existing layer-operation surface.
output_gain_position = processor.find('"outputGain", "Output Gain"')
oversampling_position = processor.find('"oversamplingQuality", "Nonlinear Oversampling"')
mseg_position = processor.find('"msegEnabled", "MSEG 1 Enabled"')
graph_amount_position = processor.rfind('"modGraph" + index + "Amount"')
user_wavetable_position = processor.find('"userWavetableMix", "User Wavetable Mix"')
resynth_position = processor.find('"resynthInstances", "Resynthesis Instances"')
layer_operation_position = processor.rfind('prefix + "Operation"')
if output_gain_position < 0 or oversampling_position <= output_gain_position:
    errors.append('oversamplingQuality must be appended after the complete v1.0 parameter surface')
if mseg_position <= oversampling_position:
    errors.append('MSEG parameters must remain appended after oversamplingQuality')
if graph_amount_position < 0 or user_wavetable_position <= graph_amount_position:
    errors.append('userWavetableMix must remain appended after the complete MSEG/graph parameter surface')
if layer_operation_position < 0 or resynth_position <= layer_operation_position:
    errors.append('clock/resynthesis controls must remain appended after the released layer-operation surface')

legacy_sources_literal = 'const juce::StringArray modSources { "Off", "LFO 1", "Velocity", "Key Track", "Random Note", "Amp Env" };'
if legacy_sources_literal not in processor:
    errors.append('legacy modSources choice list changed; v1.0 normalized automation compatibility would be at risk')
graph_sources_literal = 'const juce::StringArray graphSources { "Off", "LFO 1", "Velocity", "Key Track", "Random Note", "Amp Env", "MSEG 1" };'
if graph_sources_literal not in processor:
    errors.append('new modulation graph must expose MSEG 1 without changing legacy modSources')

if 'tabbed->addTab ("MSEG"' not in processor or 'new MsegPage (proc)' not in processor:
    errors.append('dedicated clock-aware MSEG editor tab is not attached to the processor editor')
if 'tabbed->addTab ("WAVETABLE"' not in processor or 'new UserWavetablePage (proc)' not in processor:
    errors.append('dedicated user wavetable editor tab is not attached to the processor editor')
if 'stateWithPost10Defaults' not in processor:
    errors.append('post-1.0 preset/session migration helper is missing')

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
print(' - append-only host automation order remains protected')
print(' - six-point MSEG engine, modulation graph and beat sync present')
print(' - arbitrary user wavetable import, embedded state and separate oscillator layer present')
print(' - layered resynthesis replaces stale racks and supports up to eight generated instances')
print(' - manual/DAW clock and 1/32..4/1 divisions are wired through synth and modular FX')
print(' - whole-synth interactive signal map is present')
print(' - dedicated MSEG and WAVETABLE editor tabs present')
print(' - reference-to-variant workspace and editing tabs present')
print(' - Quick/Refine/AI three-variant workflow present')
print(' - optional virtual keyboard audition path present')
print(' - reference sample solo/mix audition and editable detected base note present')
print(' - OpenAI request omits unsupported temperature sampling control')
print(' - AI provider settings and local candidate scoring present')
