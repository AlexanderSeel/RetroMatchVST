#pragma once

#include <array>
#include <cstdint>

namespace RetroMatchSequencer
{
constexpr int maxSteps = 64;
constexpr int maxHeldNotes = 16;
constexpr int modulationLaneCount = 2;

// Quarter-note lengths are used so the engine remains independent from host/sample rate.
enum class Division
{
    whole,
    half,
    quarter,
    eighth,
    sixteenth,
    thirtySecond,
    eighthTriplet,
    sixteenthTriplet,
    eighthDotted,
    sixteenthDotted
};

enum class ClockSource { host, internal };
enum class RestartMode { never, transportStart, firstNote };
enum class Mode { up, down, upDown, downUp, playedOrder, chord, random, walk, pattern };

struct Step
{
    bool enabled = true;
    bool rest = false;
    bool tie = false;
    int semitone = 0;
    int octave = 0;
    float velocity = 1.0f;
    float gate = 0.85f;
    float probability = 1.0f;
    float modulationProbability = 1.0f;
    int ratchet = 1;
    float microTiming = 0.0f; // fraction of a nominal step, bounded to +/-0.45
    bool glide = false;
    std::array<float, modulationLaneCount> macro {{ 0.5f, 0.5f }};
};

struct Settings
{
    bool enabled = false;
    ClockSource clockSource = ClockSource::host;
    RestartMode restartMode = RestartMode::transportStart;
    Mode mode = Mode::pattern;
    Division division = Division::sixteenth;
    int length = 16;
    float swing = 0.0f; // 0..1; alternating pair keeps the same total duration
    int octaveRange = 1;
    int rootNote = 60;
    double internalBpm = 120.0;
    bool latch = false;
};

struct Transport
{
    bool playing = false;
    bool justStarted = false;
    double bpm = 120.0;
};

struct Trigger
{
    int sampleOffset = 0;
    int durationSamples = 1;
    int midiNote = 60;
    float velocity = 1.0f;
    int stepIndex = 0;
    int ratchetIndex = 0;
    bool glide = false;
    bool tie = false;
    std::array<float, modulationLaneCount> macro {{ 0.5f, 0.5f }};
};

class Core
{
public:
    void prepare (double newSampleRate) noexcept;
    void reset() noexcept;

    void setSettings (const Settings& newSettings) noexcept;
    [[nodiscard]] const Settings& getSettings() const noexcept { return settings; }

    void setStep (int index, const Step& value) noexcept;
    [[nodiscard]] const Step& getStep (int index) const noexcept;
    void clearPattern() noexcept;
    void randomizePattern (std::uint32_t seed, int minSemitone = -12, int maxSemitone = 12,
                           float restProbability = 0.15f) noexcept;

    void setRandomSeed (std::uint32_t seed) noexcept;
    void noteOn (int midiNote, float velocity = 1.0f) noexcept;
    void noteOff (int midiNote) noexcept;
    void allNotesOff (bool clearLatched = true) noexcept;
    [[nodiscard]] int heldNoteCount() const noexcept;

    // Produces timestamped note triggers into caller-owned memory. No lock, heap allocation,
    // file I/O or analysis is performed here, so this can run from the audio callback.
    int processBlock (int numSamples, const Transport& transport,
                      Trigger* output, int outputCapacity) noexcept;

    [[nodiscard]] std::uint64_t getDroppedTriggerCount() const noexcept { return droppedTriggers; }
    [[nodiscard]] int getCurrentStep() const noexcept { return currentStep; }

    [[nodiscard]] static double quarterNotesForDivision (Division division) noexcept;

private:
    struct HeldNote
    {
        int note = 60;
        float velocity = 1.0f;
        std::uint64_t order = 0;
        bool active = false;
    };

    struct PendingTrigger
    {
        Trigger trigger;
        double samplesUntil = 0.0;
        bool active = false;
    };

    static constexpr int pendingCapacity = 256;

    Settings settings;
    std::array<Step, maxSteps> steps {};
    std::array<HeldNote, maxHeldNotes> heldNotes {};
    std::array<PendingTrigger, pendingCapacity> pending {};

    double sampleRate = 44100.0;
    double samplesUntilGrid = 0.0;
    int currentStep = 0;
    std::uint64_t absoluteStep = 0;
    std::uint64_t arpSequence = 0;
    int walkPosition = 0;
    std::uint64_t noteOrder = 0;
    std::uint32_t randomState = 0x524d5331u;
    std::uint64_t droppedTriggers = 0;
    bool wasRunning = false;

    [[nodiscard]] float nextRandom01() noexcept;
    [[nodiscard]] double nominalStepSamples (double bpm) const noexcept;
    [[nodiscard]] double swungStepSamples (double bpm, std::uint64_t stepNumber) const noexcept;
    [[nodiscard]] int collectHeldIndices (std::array<int, maxHeldNotes>& indices, bool playedOrder) const noexcept;
    [[nodiscard]] int selectSingleArpNote (const Step& step, float& sourceVelocity) noexcept;
    void scheduleStep (int stepIndex, double gridOffset, double stepSamples,
                       int blockSamples, Trigger* output, int outputCapacity, int& outputCount) noexcept;
    void emitOrQueue (Trigger trigger, double offset, int blockSamples,
                      Trigger* output, int outputCapacity, int& outputCount) noexcept;
    void drainPending (int blockSamples, Trigger* output, int outputCapacity, int& outputCount) noexcept;
    void clearPending() noexcept;
    static Step sanitizedStep (Step value) noexcept;
    static Settings sanitizedSettings (Settings value) noexcept;
};
}
