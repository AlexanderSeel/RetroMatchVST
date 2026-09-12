#include "../Source/Sequencer/StepSequencer.h"
#include "../Source/Sequencer/PatternLibrary.h"

#include <cmath>
#include <iostream>

using namespace RetroMatchSequencer;

namespace
{
int fail (const char* message)
{
    std::cerr << "FAILED: " << message << '\n';
    return 1;
}

bool near (int actual, int expected, int tolerance = 1)
{
    return std::abs (actual - expected) <= tolerance;
}

Settings internalPatternSettings()
{
    Settings settings;
    settings.enabled = true;
    settings.clockSource = ClockSource::internal;
    settings.mode = Mode::pattern;
    settings.division = Division::sixteenth;
    settings.internalBpm = 120.0;
    settings.length = 4;
    settings.rootNote = 60;
    settings.noteGate = false;
    return settings;
}
}

int main()
{
    {
        std::array<Step, maxSteps> previous {};
        for (int templateIndex = 0; templateIndex < patternTemplateCount; ++templateIndex)
        {
            const auto pattern = makePatternTemplate (templateIndex);
            if (pattern.name == nullptr || pattern.length < 1 || pattern.length > maxSteps)
                return fail ("pattern library returned an invalid template descriptor");
            bool differs = templateIndex == 0;
            for (int i = 0; i < pattern.length; ++i)
            {
                const auto& step = pattern.steps[(size_t) i];
                if (step.semitone < -48 || step.semitone > 48 || step.velocity < 0.0f || step.velocity > 1.0f
                    || step.gate < 0.02f || step.gate > 1.0f || step.probability < 0.0f || step.probability > 1.0f
                    || step.modulationProbability < 0.0f || step.modulationProbability > 1.0f
                    || step.ratchet < 1 || step.ratchet > 8)
                    return fail ("pattern library escaped step safety bounds");
                differs = differs || step.semitone != previous[(size_t) i].semitone
                                  || step.rest != previous[(size_t) i].rest;
            }
            if (templateIndex > 0 && ! differs)
                return fail ("pattern library templates are not distinct");
            previous = pattern.steps;
        }
    }

    if (std::abs (Core::quarterNotesForDivision (Division::sixteenth) - 0.25) > 1.0e-12
        || std::abs (Core::quarterNotesForDivision (Division::eighthTriplet) - 1.0 / 3.0) > 1.0e-12
        || std::abs (Core::quarterNotesForDivision (Division::sixteenthDotted) - 0.375) > 1.0e-12)
        return fail ("musical division lengths are incorrect");

    {
        Core core;
        core.prepare (48000.0);
        core.setSettings (internalPatternSettings());
        Trigger events[16] {};
        Transport transport;
        const int count = core.processBlock (24000, transport, events, 16);
        if (count != 4
            || ! near (events[0].sampleOffset, 0)
            || ! near (events[1].sampleOffset, 6000)
            || ! near (events[2].sampleOffset, 12000)
            || ! near (events[3].sampleOffset, 18000))
            return fail ("120 BPM sixteenth-note clock is not sample accurate");
    }

    {
        Core core;
        core.prepare (48000.0);
        auto settings = internalPatternSettings();
        settings.noteGate = true;
        core.setSettings (settings);
        Trigger events[8] {};
        Transport transport;
        if (core.processBlock (24000, transport, events, 8) != 0)
            return fail ("note-gated sequencer advanced without a held note");
        core.noteOn (60, 1.0f);
        if (core.processBlock (24000, transport, events, 8) == 0)
            return fail ("note-gated sequencer did not start from a held note");

        core.allNotesOff();
        core.reset();
        core.setSettings (settings);
        core.noteOn (62, 1.0f);
        if (core.processBlock (1, transport, events, 8) == 0 || events[0].midiNote != 62)
            return fail ("pattern mode did not transpose from the played root note");
    }

    {
        Core core;
        core.prepare (48000.0);
        auto settings = internalPatternSettings();
        settings.swing = 0.5f;
        core.setSettings (settings);
        Trigger events[8] {};
        Transport transport;
        const int count = core.processBlock (12000, transport, events, 8);
        if (count != 2 || ! near (events[0].sampleOffset, 0) || ! near (events[1].sampleOffset, 7500))
            return fail ("swing does not preserve the alternating two-step grid");
        const int nextCount = core.processBlock (128, transport, events, 8);
        if (nextCount < 1 || ! near (events[0].sampleOffset, 0))
            return fail ("swing pair does not return to the nominal grid boundary");
    }

    {
        Core core;
        core.prepare (48000.0);
        auto settings = internalPatternSettings();
        settings.length = 1;
        settings.rootNote = 60;
        core.setSettings (settings);
        Step step;
        step.semitone = 7;
        step.octave = 1;
        step.velocity = 0.8f;
        step.gate = 0.5f;
        core.setStep (0, step);
        Trigger event {};
        Transport transport;
        if (core.processBlock (1, transport, &event, 1) != 1
            || event.midiNote != 79
            || std::abs (event.velocity - 0.8f) > 1.0e-6f
            || ! near (event.durationSamples, 3000))
            return fail ("pattern pitch/velocity/gate mapping is incorrect");
    }

    {
        Core core;
        core.prepare (48000.0);
        auto settings = internalPatternSettings();
        settings.length = 1;
        core.setSettings (settings);
        Step step;
        step.ratchet = 4;
        step.gate = 0.5f;
        core.setStep (0, step);
        Trigger events[8] {};
        Transport transport;
        const int count = core.processBlock (6000, transport, events, 8);
        if (count != 4
            || ! near (events[0].sampleOffset, 0)
            || ! near (events[1].sampleOffset, 1500)
            || ! near (events[2].sampleOffset, 3000)
            || ! near (events[3].sampleOffset, 4500)
            || ! near (events[0].durationSamples, 750))
            return fail ("ratchet timing/gate subdivision is incorrect");
    }

    {
        Core core;
        core.prepare (48000.0);
        auto settings = internalPatternSettings();
        settings.length = 2;
        core.setSettings (settings);
        Step first;
        first.rest = true;
        core.setStep (0, first);
        Step second;
        second.probability = 0.0f;
        core.setStep (1, second);
        Trigger events[4] {};
        Transport transport;
        if (core.processBlock (12000, transport, events, 4) != 0)
            return fail ("rest/probability-zero steps emitted triggers");
    }

    {
        Core core;
        core.prepare (48000.0);
        auto settings = internalPatternSettings();
        settings.length = 1;
        core.setSettings (settings);
        Step step;
        step.modulationProbability = 0.0f;
        step.macro = {{ 0.12f, 0.88f }};
        core.setStep (0, step);
        Trigger event {};
        Transport transport;
        if (core.processBlock (1, transport, &event, 1) != 1
            || std::abs (event.macro[0] - 0.5f) > 1.0e-6f
            || std::abs (event.macro[1] - 0.5f) > 1.0e-6f
            || event.macroDestination[0] != MacroDestination::none
            || event.macroDestination[1] != MacroDestination::none)
            return fail ("modulation probability did not preserve the note while neutralizing macro lanes");
    }

    {
        Core core;
        core.prepare (48000.0);
        auto settings = internalPatternSettings();
        settings.length = 2;
        settings.macroDestination = {{ MacroDestination::cutoff, MacroDestination::pitch }};
        settings.macroInterpolation = {{ MacroInterpolation::linear, MacroInterpolation::smooth }};
        settings.macroLaneRate = {{ 0.5f, 2.0f }};
        core.setSettings (settings);
        Step first, second;
        first.macro = {{ 0.0f, 0.0f }};
        second.macro = {{ 1.0f, 1.0f }};
        core.setStep (0, first);
        core.setStep (1, second);
        Trigger events[4] {};
        Transport transport;
        if (core.processBlock (24000, transport, events, 4) != 4
            || std::abs (events[0].macro[0] - 0.0f) > 1.0e-6f
            || std::abs (events[1].macro[0] - 0.5f) > 1.0e-6f
            || std::abs (events[2].macro[0] - 1.0f) > 1.0e-6f
            || events[0].macroDestination[0] != MacroDestination::cutoff
            || events[0].macroDestination[1] != MacroDestination::pitch)
            return fail ("macro lane interpolation, polymetric rate, or destination metadata is incorrect");

        settings.macroLaneRate = {{ -10.0f, 10.0f }};
        settings.macroDestination = {{ (MacroDestination) 99, (MacroDestination) -1 }};
        settings.outputMode = (OutputMode) 99;
        settings.targetScope = (TargetScope) 99;
        settings.targetLayer = 99;
        core.setSettings (settings);
        if (core.getSettings().macroLaneRate[0] != 0.25f
            || core.getSettings().macroLaneRate[1] != 4.0f
            || core.getSettings().macroDestination[0] != MacroDestination::wavetablePosition
            || core.getSettings().macroDestination[1] != MacroDestination::none
            || core.getSettings().outputMode != OutputMode::motionOnly
            || core.getSettings().targetScope != TargetScope::layerInstance
            || core.getSettings().targetLayer != 6)
            return fail ("macro lane settings were not bounded");
    }

    {
        Core core;
        core.prepare (48000.0);
        auto settings = internalPatternSettings();
        settings.mode = Mode::up;
        settings.length = 4;
        settings.octaveRange = 1;
        core.setSettings (settings);
        core.noteOn (67, 0.7f);
        core.noteOn (60, 0.9f);
        core.noteOn (64, 0.8f);
        Trigger events[8] {};
        Transport transport;
        const int count = core.processBlock (24000, transport, events, 8);
        if (count != 4 || events[0].midiNote != 60 || events[1].midiNote != 64
            || events[2].midiNote != 67 || events[3].midiNote != 60)
            return fail ("up arpeggiator does not sort/cycle held notes deterministically");
    }

    {
        Core core;
        core.prepare (48000.0);
        auto settings = internalPatternSettings();
        settings.mode = Mode::up;
        settings.length = 4;
        settings.octaveRange = 2;
        core.setSettings (settings);
        core.noteOn (64);
        core.noteOn (60);
        Trigger events[4] {};
        Transport transport;
        const int count = core.processBlock (24000, transport, events, 4);
        if (count != 4 || events[0].midiNote != 60 || events[1].midiNote != 64
            || events[2].midiNote != 72 || events[3].midiNote != 76)
            return fail ("arpeggiator octave-range control does not extend held notes across octaves");
    }

    {
        Core core;
        core.prepare (48000.0);
        auto settings = internalPatternSettings();
        settings.mode = Mode::up;
        settings.length = 4;
        settings.restartMode = RestartMode::firstNote;
        core.setSettings (settings);
        core.noteOn (60);
        core.noteOn (64);
        Trigger events[4] {};
        Transport transport;
        if (core.processBlock (12000, transport, events, 4) != 2
            || events[0].midiNote != 60 || events[1].midiNote != 64)
            return fail ("first-note restart fixture did not establish its initial phrase");
        core.allNotesOff();
        core.noteOn (67);
        core.noteOn (71);
        if (core.processBlock (12000, transport, events, 4) != 2
            || events[0].midiNote != 67 || events[1].midiNote != 71
            || events[0].stepIndex != 0)
            return fail ("FIRST NOTE restart did not reset the arpeggiator phrase and pattern position");
    }

    {
        Core core;
        core.prepare (48000.0);
        auto settings = internalPatternSettings();
        settings.clockSource = ClockSource::host;
        settings.restartMode = RestartMode::transportStart;
        settings.length = 4;
        core.setSettings (settings);
        for (int i = 0; i < 4; ++i)
        {
            Step step;
            step.semitone = i * 2;
            core.setStep (i, step);
        }
        Trigger events[4] {};
        Transport host;
        host.playing = true;
        host.bpm = 120.0;
        if (core.processBlock (12000, host, events, 4) != 2
            || events[0].stepIndex != 0 || events[1].stepIndex != 1)
            return fail ("host transport restart fixture did not advance before restart");
        host.justStarted = true;
        if (core.processBlock (6000, host, events, 4) != 1
            || events[0].stepIndex != 0 || events[0].midiNote != 60)
            return fail ("TRANSPORT restart did not reset sequence position on host start");
    }

    {
        Core core;
        core.prepare (48000.0);
        auto settings = internalPatternSettings();
        settings.clockSource = ClockSource::host;
        settings.restartMode = RestartMode::transportStart;
        settings.length = 2;
        core.setSettings (settings);
        Transport host;
        host.playing = true;
        host.justStarted = true;
        Trigger events[4] {};
        if (core.processBlock (1, host, events, 4) != 1 || events[0].stepIndex != 0)
            return fail ("host transport stop/resume fixture did not establish its first step");
        host.playing = false;
        host.justStarted = false;
        if (core.processBlock (12000, host, events, 4) != 0)
            return fail ("host stop did not suppress sequencer triggers");
        host.playing = true;
        host.justStarted = true;
        if (core.processBlock (1, host, events, 4) != 1 || events[0].stepIndex != 0)
            return fail ("host resume did not restart the sequencer at step zero");
    }

    {
        Core core;
        core.prepare (48000.0);
        auto settings = internalPatternSettings();
        settings.mode = Mode::playedOrder;
        settings.length = 3;
        settings.latch = true;
        core.setSettings (settings);
        core.noteOn (67);
        core.noteOn (60);
        core.noteOn (64);
        core.noteOff (60);
        Trigger events[4] {};
        Transport transport;
        const int count = core.processBlock (18000, transport, events, 4);
        if (count != 3 || events[0].midiNote != 67 || events[1].midiNote != 60 || events[2].midiNote != 64)
            return fail ("played-order/latch behavior is incorrect");
    }

    {
        Core a, b;
        a.prepare (48000.0);
        b.prepare (48000.0);
        auto settings = internalPatternSettings();
        settings.mode = Mode::random;
        settings.length = 8;
        a.setSettings (settings);
        b.setSettings (settings);
        a.setRandomSeed (12345);
        b.setRandomSeed (12345);
        for (int note : { 48, 52, 55, 60 })
        {
            a.noteOn (note);
            b.noteOn (note);
        }
        Trigger first[8] {}, second[8] {};
        Transport transport;
        const int firstCount = a.processBlock (48000, transport, first, 8);
        const int secondCount = b.processBlock (48000, transport, second, 8);
        if (firstCount != secondCount)
            return fail ("deterministic random mode produced different event counts");
        for (int i = 0; i < firstCount; ++i)
            if (first[i].midiNote != second[i].midiNote || first[i].sampleOffset != second[i].sampleOffset)
                return fail ("deterministic random mode produced different sequences");
    }

    {
        Core core;
        core.prepare (48000.0);
        auto settings = internalPatternSettings();
        settings.length = 8;
        core.setSettings (settings);
        core.randomizePattern (0x12345678u, -7, 7, 0.25f);
        for (int i = 0; i < settings.length; ++i)
        {
            const auto& step = core.getStep (i);
            if (step.semitone < -7 || step.semitone > 7
                || step.velocity < 0.0f || step.velocity > 1.0f
                || step.gate < 0.02f || step.gate > 1.0f
                || step.ratchet < 1 || step.ratchet > 8
                || step.microTiming < -0.45f || step.microTiming > 0.45f)
                return fail ("bounded pattern randomization escaped its safety limits");
        }
    }

    std::cout << "Sequencer core tests passed.\n";
    return 0;
}
