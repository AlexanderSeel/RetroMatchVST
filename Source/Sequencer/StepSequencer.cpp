#include "StepSequencer.h"

#include <algorithm>
#include <cmath>

namespace RetroMatchSequencer
{
namespace
{
template <typename T>
T bounded (T low, T high, T value) noexcept
{
    return std::max (low, std::min (high, value));
}
}

double Core::quarterNotesForDivision (Division division) noexcept
{
    switch (division)
    {
        case Division::whole:            return 4.0;
        case Division::half:             return 2.0;
        case Division::quarter:          return 1.0;
        case Division::eighth:           return 0.5;
        case Division::sixteenth:        return 0.25;
        case Division::thirtySecond:     return 0.125;
        case Division::eighthTriplet:    return 1.0 / 3.0;
        case Division::sixteenthTriplet: return 1.0 / 6.0;
        case Division::eighthDotted:     return 0.75;
        case Division::sixteenthDotted:  return 0.375;
    }
    return 0.25;
}

Step Core::sanitizedStep (Step value) noexcept
{
    value.semitone = bounded (-48, 48, value.semitone);
    value.octave = bounded (-4, 4, value.octave);
    value.velocity = bounded (0.0f, 1.0f, value.velocity);
    value.gate = bounded (0.02f, 1.0f, value.gate);
    value.probability = bounded (0.0f, 1.0f, value.probability);
    value.modulationProbability = bounded (0.0f, 1.0f, value.modulationProbability);
    value.ratchet = bounded (1, 8, value.ratchet);
    value.microTiming = bounded (-0.45f, 0.45f, value.microTiming);
    for (auto& macro : value.macro)
        macro = bounded (0.0f, 1.0f, macro);
    return value;
}

Settings Core::sanitizedSettings (Settings value) noexcept
{
    value.length = bounded (1, maxSteps, value.length);
    value.swing = bounded (0.0f, 0.95f, value.swing);
    value.octaveRange = bounded (1, 4, value.octaveRange);
    value.rootNote = bounded (0, 127, value.rootNote);
    value.internalBpm = bounded (20.0, 400.0, value.internalBpm);
    value.outputMode = (OutputMode) bounded (0, 1, (int) value.outputMode);
    value.targetScope = (TargetScope) bounded (0, 2, (int) value.targetScope);
    value.targetLayer = bounded (0, 6, value.targetLayer);
    for (int lane = 0; lane < modulationLaneCount; ++lane)
    {
        value.macroDestination[(std::size_t) lane] = (MacroDestination) bounded (0, 5, (int) value.macroDestination[(std::size_t) lane]);
        value.macroInterpolation[(std::size_t) lane] = (MacroInterpolation) bounded (0, 3, (int) value.macroInterpolation[(std::size_t) lane]);
        value.macroLaneRate[(std::size_t) lane] = bounded (0.25f, 4.0f, value.macroLaneRate[(std::size_t) lane]);
    }
    return value;
}

void Core::prepare (double newSampleRate) noexcept
{
    sampleRate = bounded (1000.0, 768000.0, newSampleRate);
    reset();
}

void Core::reset() noexcept
{
    samplesUntilGrid = 0.0;
    currentStep = 0;
    absoluteStep = 0;
    arpSequence = 0;
    walkPosition = 0;
    wasRunning = false;
    clearPending();
}

void Core::setSettings (const Settings& newSettings) noexcept
{
    settings = sanitizedSettings (newSettings);
    currentStep %= settings.length;
}

void Core::setStep (int index, const Step& value) noexcept
{
    if (index < 0 || index >= maxSteps)
        return;
    steps[(std::size_t) index] = sanitizedStep (value);
}

const Step& Core::getStep (int index) const noexcept
{
    index = bounded (0, maxSteps - 1, index);
    return steps[(std::size_t) index];
}

void Core::clearPattern() noexcept
{
    for (auto& step : steps)
    {
        step = Step {};
        step.rest = true;
    }
}

void Core::setRandomSeed (std::uint32_t seed) noexcept
{
    randomState = seed != 0 ? seed : 0x524d5331u;
}

float Core::nextRandom01() noexcept
{
    // xorshift32: deterministic, tiny state and appropriate for musical probability.
    auto x = randomState;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    randomState = x != 0 ? x : 0x524d5331u;
    return static_cast<float> (randomState & 0x00ffffffu) / 16777216.0f;
}

void Core::randomizePattern (std::uint32_t seed, int minSemitone, int maxSemitone,
                             float restProbability) noexcept
{
    if (minSemitone > maxSemitone)
        std::swap (minSemitone, maxSemitone);
    minSemitone = bounded (-48, 48, minSemitone);
    maxSemitone = bounded (-48, 48, maxSemitone);
    restProbability = bounded (0.0f, 0.95f, restProbability);
    setRandomSeed (seed);

    const int span = std::max (1, maxSemitone - minSemitone + 1);
    for (int i = 0; i < settings.length; ++i)
    {
        Step step;
        step.semitone = minSemitone + static_cast<int> (nextRandom01() * span);
        step.semitone = bounded (minSemitone, maxSemitone, step.semitone);
        step.rest = nextRandom01() < restProbability;
        step.velocity = 0.62f + nextRandom01() * 0.38f;
        step.gate = 0.35f + nextRandom01() * 0.65f;
        step.probability = 0.78f + nextRandom01() * 0.22f;
        step.modulationProbability = 0.70f + nextRandom01() * 0.30f;
        const float ratchetChance = nextRandom01();
        step.ratchet = ratchetChance > 0.93f ? 3 : (ratchetChance > 0.80f ? 2 : 1);
        step.microTiming = (nextRandom01() * 2.0f - 1.0f) * 0.08f;
        step.macro[0] = nextRandom01();
        step.macro[1] = nextRandom01();
        steps[(std::size_t) i] = sanitizedStep (step);
    }
}

int Core::heldNoteCount() const noexcept
{
    int count = 0;
    for (const auto& held : heldNotes)
        if (held.active)
            ++count;
    return count;
}

void Core::noteOn (int midiNote, float velocity) noexcept
{
    midiNote = bounded (0, 127, midiNote);
    velocity = bounded (0.0f, 1.0f, velocity);
    const bool wasEmpty = heldNoteCount() == 0;

    for (auto& held : heldNotes)
    {
        if (held.active && held.note == midiNote)
        {
            held.velocity = velocity;
            held.order = ++noteOrder;
            return;
        }
    }

    HeldNote* destination = nullptr;
    for (auto& held : heldNotes)
    {
        if (! held.active)
        {
            destination = &held;
            break;
        }
    }

    if (destination == nullptr)
    {
        destination = &heldNotes[0];
        for (auto& held : heldNotes)
            if (held.order < destination->order)
                destination = &held;
    }

    destination->note = midiNote;
    destination->velocity = velocity;
    destination->order = ++noteOrder;
    destination->active = true;

    if (wasEmpty && settings.restartMode == RestartMode::firstNote)
        reset();
}

void Core::noteOff (int midiNote) noexcept
{
    if (settings.latch)
        return;

    for (auto& held : heldNotes)
        if (held.active && held.note == midiNote)
            held.active = false;
}

void Core::allNotesOff (bool clearLatched) noexcept
{
    if (settings.latch && ! clearLatched)
        return;
    for (auto& held : heldNotes)
        held.active = false;
}

double Core::nominalStepSamples (double bpm) const noexcept
{
    bpm = bounded (20.0, 400.0, bpm);
    return sampleRate * 60.0 / bpm * quarterNotesForDivision (settings.division);
}

double Core::swungStepSamples (double bpm, std::uint64_t stepNumber) const noexcept
{
    const double nominal = nominalStepSamples (bpm);
    const double swingOffset = static_cast<double> (settings.swing) * 0.5;
    return nominal * ((stepNumber & 1u) == 0u ? 1.0 + swingOffset : 1.0 - swingOffset);
}

int Core::collectHeldIndices (std::array<int, maxHeldNotes>& indices, bool playedOrder) const noexcept
{
    int count = 0;
    for (int i = 0; i < maxHeldNotes; ++i)
        if (heldNotes[(std::size_t) i].active)
            indices[(std::size_t) count++] = i;

    for (int i = 1; i < count; ++i)
    {
        const int key = indices[(std::size_t) i];
        int j = i - 1;
        auto comesBefore = [&] (int lhs, int rhs)
        {
            if (playedOrder)
                return heldNotes[(std::size_t) lhs].order < heldNotes[(std::size_t) rhs].order;
            return heldNotes[(std::size_t) lhs].note < heldNotes[(std::size_t) rhs].note;
        };
        while (j >= 0 && comesBefore (key, indices[(std::size_t) j]))
        {
            indices[(std::size_t) (j + 1)] = indices[(std::size_t) j];
            --j;
        }
        indices[(std::size_t) (j + 1)] = key;
    }
    return count;
}

int Core::selectSingleArpNote (const Step& step, float& sourceVelocity) noexcept
{
    if (settings.mode == Mode::pattern)
    {
        std::array<int, maxHeldNotes> indices {};
        const int count = collectHeldIndices (indices, true);
        if (count == 0)
        {
            // Keep the low-level, non-gated pattern API useful for offline
            // pattern tests/tools, but never let a note-gated preset free-run.
            if (settings.noteGate)
                return -1;
            sourceVelocity = 1.0f;
            return bounded (0, 127, settings.rootNote + step.semitone + step.octave * 12);
        }

        const auto& held = heldNotes[(std::size_t) indices[0]];
        sourceVelocity = held.velocity;
        // Pattern pitches are authored around rootNote. A played key becomes
        // the live root, so the same pattern can be performed in any key.
        const int transposedRoot = held.note;
        return bounded (0, 127, transposedRoot + step.semitone + step.octave * 12);
    }

    std::array<int, maxHeldNotes> indices {};
    const bool byOrder = settings.mode == Mode::playedOrder;
    const int count = collectHeldIndices (indices, byOrder);
    if (count == 0)
        return -1;

    const int span = count * settings.octaveRange;
    int linearIndex = 0;

    switch (settings.mode)
    {
        case Mode::up:
        case Mode::playedOrder:
            linearIndex = static_cast<int> (arpSequence % static_cast<std::uint64_t> (span));
            break;
        case Mode::down:
            linearIndex = span - 1 - static_cast<int> (arpSequence % static_cast<std::uint64_t> (span));
            break;
        case Mode::upDown:
        case Mode::downUp:
        {
            const int period = span <= 1 ? 1 : span * 2 - 2;
            const int position = static_cast<int> (arpSequence % static_cast<std::uint64_t> (period));
            linearIndex = position < span ? position : period - position;
            if (settings.mode == Mode::downUp)
                linearIndex = span - 1 - linearIndex;
            break;
        }
        case Mode::random:
            linearIndex = bounded (0, span - 1, static_cast<int> (nextRandom01() * span));
            break;
        case Mode::walk:
        {
            if (arpSequence == 0)
                walkPosition = 0;
            else
            {
                const int direction = nextRandom01() < 0.5f ? -1 : 1;
                walkPosition = bounded (0, span - 1, walkPosition + direction);
            }
            linearIndex = walkPosition;
            break;
        }
        case Mode::chord:
        case Mode::pattern:
            linearIndex = 0;
            break;
    }

    ++arpSequence;
    const int octave = linearIndex / count;
    const int heldIndex = indices[(std::size_t) (linearIndex % count)];
    sourceVelocity = heldNotes[(std::size_t) heldIndex].velocity;
    return bounded (0, 127, heldNotes[(std::size_t) heldIndex].note + octave * 12
                          + step.semitone + step.octave * 12);
}

float Core::macroValue (int lane) noexcept
{
    lane = bounded (0, modulationLaneCount - 1, lane);
    const auto interpolation = settings.macroInterpolation[(std::size_t) lane];
    if (interpolation == MacroInterpolation::random)
        return nextRandom01();

    const int length = std::max (1, settings.length);
    const double position = (double) absoluteStep * settings.macroLaneRate[(std::size_t) lane];
    const double base = std::floor (position);
    const int first = ((int) base) % length;
    const float a = steps[(std::size_t) first].macro[(std::size_t) lane];
    if (interpolation == MacroInterpolation::hold)
        return a;

    const float b = steps[(std::size_t) ((first + 1) % length)].macro[(std::size_t) lane];
    const float fraction = (float) (position - base);
    const float t = interpolation == MacroInterpolation::smooth
        ? fraction * fraction * (3.0f - 2.0f * fraction) : fraction;
    return a + (b - a) * t;
}

void Core::emitOrQueue (Trigger trigger, double offset, int blockSamples,
                        Trigger* output, int outputCapacity, int& outputCount) noexcept
{
    if (offset < 0.0)
        offset = 0.0;

    if (offset < blockSamples)
    {
        trigger.sampleOffset = bounded (0, std::max (0, blockSamples - 1), static_cast<int> (std::llround (offset)));
        if (output != nullptr && outputCount < outputCapacity)
            output[outputCount++] = trigger;
        else
            ++droppedTriggers;
        return;
    }

    for (auto& item : pending)
    {
        if (! item.active)
        {
            item.trigger = trigger;
            item.samplesUntil = offset - blockSamples;
            item.active = true;
            return;
        }
    }
    ++droppedTriggers;
}

void Core::drainPending (int blockSamples, Trigger* output, int outputCapacity, int& outputCount) noexcept
{
    for (auto& item : pending)
    {
        if (! item.active)
            continue;

        if (item.samplesUntil < blockSamples)
        {
            item.trigger.sampleOffset = bounded (0, std::max (0, blockSamples - 1),
                                                 static_cast<int> (std::llround (item.samplesUntil)));
            if (output != nullptr && outputCount < outputCapacity)
                output[outputCount++] = item.trigger;
            else
                ++droppedTriggers;
            item.active = false;
        }
        else
        {
            item.samplesUntil -= blockSamples;
        }
    }
}

void Core::clearPending() noexcept
{
    for (auto& item : pending)
        item.active = false;
}

void Core::scheduleStep (int stepIndex, double gridOffset, double stepSamples,
                         int blockSamples, Trigger* output, int outputCapacity, int& outputCount) noexcept
{
    const auto& step = steps[(std::size_t) stepIndex];
    if (! step.enabled || step.rest || step.probability <= 0.0f)
        return;
    if (step.probability < 1.0f && nextRandom01() > step.probability)
        return;

    // Recover the unswung nominal duration from the current swung interval. This
    // keeps micro-timing proportional to the actual host/internal BPM instead of
    // silently treating host-synced patterns as 120 BPM.
    const double swingOffset = static_cast<double> (settings.swing) * 0.5;
    const double swingFactor = (absoluteStep & 1u) == 0u ? 1.0 + swingOffset : 1.0 - swingOffset;
    const double nominal = stepSamples / std::max (0.025, swingFactor);
    const double microOffset = static_cast<double> (step.microTiming) * nominal;
    const int ratchetCount = bounded (1, 8, step.ratchet);
    const double ratchetSpacing = stepSamples / ratchetCount;
    const int durationSamples = std::max (1, static_cast<int> (std::llround (ratchetSpacing * step.gate)));

    auto makeTrigger = [&] (int note, float sourceVelocity, int ratchetIndex)
    {
        Trigger trigger;
        trigger.durationSamples = durationSamples;
        trigger.midiNote = bounded (0, 127, note);
        trigger.velocity = bounded (0.0f, 1.0f, sourceVelocity * step.velocity);
        trigger.stepIndex = stepIndex;
        trigger.ratchetIndex = ratchetIndex;
        trigger.glide = step.glide;
        trigger.tie = step.tie;
        for (int lane = 0; lane < modulationLaneCount; ++lane)
        {
            trigger.macro[(std::size_t) lane] = macroValue (lane);
            trigger.macroDestination[(std::size_t) lane] = settings.macroDestination[(std::size_t) lane];
        }
        if (step.modulationProbability < 1.0f && nextRandom01() > step.modulationProbability)
        {
            trigger.macro = {{ 0.5f, 0.5f }};
            trigger.macroDestination = {{ MacroDestination::none, MacroDestination::none }};
        }
        return trigger;
    };

    if (settings.mode == Mode::chord)
    {
        std::array<int, maxHeldNotes> indices {};
        const int count = collectHeldIndices (indices, false);
        for (int ratchetIndex = 0; ratchetIndex < ratchetCount; ++ratchetIndex)
        {
            const double offset = gridOffset + microOffset + ratchetSpacing * ratchetIndex;
            for (int i = 0; i < count; ++i)
            {
                const auto& held = heldNotes[(std::size_t) indices[(std::size_t) i]];
                const int note = held.note + step.semitone + step.octave * 12;
                emitOrQueue (makeTrigger (note, held.velocity, ratchetIndex), offset,
                             blockSamples, output, outputCapacity, outputCount);
            }
        }
        ++arpSequence;
        return;
    }

    float sourceVelocity = 1.0f;
    const int note = selectSingleArpNote (step, sourceVelocity);
    if (note < 0)
        return;

    for (int ratchetIndex = 0; ratchetIndex < ratchetCount; ++ratchetIndex)
    {
        const double offset = gridOffset + microOffset + ratchetSpacing * ratchetIndex;
        emitOrQueue (makeTrigger (note, sourceVelocity, ratchetIndex), offset,
                     blockSamples, output, outputCapacity, outputCount);
    }
}

int Core::processBlock (int numSamples, const Transport& transport,
                        Trigger* output, int outputCapacity) noexcept
{
    if (numSamples <= 0 || outputCapacity < 0)
        return 0;

    const bool running = settings.enabled
                      && (settings.clockSource == ClockSource::internal || transport.playing)
                      && (! settings.noteGate || heldNoteCount() > 0);
    if (! running)
    {
        clearPending();
        wasRunning = false;
        return 0;
    }

    if (settings.clockSource == ClockSource::host
        && transport.justStarted
        && settings.restartMode == RestartMode::transportStart)
        reset();

    int outputCount = 0;
    drainPending (numSamples, output, outputCapacity, outputCount);

    const double bpm = settings.clockSource == ClockSource::internal
                     ? settings.internalBpm
                     : bounded (20.0, 400.0, transport.bpm);

    double gridOffset = samplesUntilGrid;
    while (gridOffset < numSamples)
    {
        const double stepSamples = swungStepSamples (bpm, absoluteStep);
        scheduleStep (currentStep, gridOffset, stepSamples,
                      numSamples, output, outputCapacity, outputCount);
        currentStep = (currentStep + 1) % settings.length;
        ++absoluteStep;
        gridOffset += stepSamples;
    }

    samplesUntilGrid = gridOffset - numSamples;
    wasRunning = true;
    return outputCount;
}
}
