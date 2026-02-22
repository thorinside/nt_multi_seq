#ifndef NT_SEQ_H
#define NT_SEQ_H

#include <cstddef>
#include <distingnt/api.h>
#include <distingnt/microtuning.h>
#include <distingnt/wav.h>
#include "engines/SequencerEngine.h"
#include "scale/ScaleQuantizer.h"
#include "clock/ClockProcessor.h"

// Maximum channels supported
constexpr int kMaxChannels = 4;

// Maximum parameters any single engine can define
constexpr int kMaxEngineParams = 32;

// Per-channel param stride: 15 common + 32 engine slots
constexpr int kParamsPerChannel = 15 + kMaxEngineParams;

// Maximum total parameters (upper bound for array sizing)
// Global(5) + engine-type-per-channel(4) + per-channel(47) * 4 = 197
constexpr int kMaxTotalParams = 5 + kMaxChannels + kMaxChannels * kParamsPerChannel;

// Maximum pages: 1 global + 1 engines + 2 per channel (common + engine)
constexpr int kMaxPages = 1 + 1 + kMaxChannels * 2;

// Maximum page param indices
constexpr int kMaxPageIndices = kMaxTotalParams;

// Maximum .scl notes
constexpr int kMaxSclNotes = 128;

// --- Global parameter indices ---
enum GlobalParam {
    kParamRootNote = 0,
    kParamOctave,
    kParamScaleFile,
    kParamNoteWeight,   // 0=Major, 1=Harmonic, 2=Equal
    kParamWarpAmount,   // 0-100%, post-quantization bias toward characteristic notes
    kNumGlobalParams
};

// --- Per-channel common parameter offsets ---
enum ChannelParamOffset {
    kChClockIn = 0,
    kChResetIn,
    kChRouting,
    kChGateOut,
    kChGateOutMode,
    kChCvOut,
    kChCvOutMode,
    kChVelOut,
    kChVelOutMode,
    kChMidiChannel,
    kChMidiDest,
    kChClockDiv,
    kChScaleEnable,
    kChNoteGateIn,
    kChNoteCvIn,
    kNumChannelCommonParams
};

// --- Engine type enum (includes None for runtime switching) ---
enum EngineType {
    kEngineNone = -1,
    kEngineThorp = 0,
    kEngineSoma,
    kEngineAeSeq,
    kEngineSeqMarkov,
    kNumEngineTypes
};

// --- Output mode enum ---
enum Routing {
    kRoutingCV = 0,
    kRoutingMIDI,
    kNumRoutings
};

// --- MIDI destination enum ---
enum MidiDest {
    kMidiDestBreakout = 0,
    kMidiDestSelectBus,
    kMidiDestUSB,
    kMidiDestInternal,
    kNumMidiDests
};

// --- Specification ---
// Single spec: number of channels (1-4)
enum SpecIndex {
    SPEC_CHANNELS = 0,
    NUM_SPECS
};

static const _NT_specification specifications[] = {
    { .name = "Channels", .min = 1, .max = 4, .def = 1, .type = kNT_typeGeneric },
};

// --- Enum string arrays ---
// Engine type with None for the runtime param (0=None, 1=Thorp, 2=Soma, 3=AE Seq, 4=Markov)
static const char* const engineTypeWithNoneStrings[] = {
    "None", "Thorp", "Soma", "AE Seq", "Markov", nullptr
};

static const char* const routingStrings[] = {
    "CV", "MIDI", nullptr
};

static const char* const offOnStrings[] = {
    "Off", "On", nullptr
};

static const char* const midiDestStrings[] = {
    "Breakout", "Sel.Bus", "USB", "Internal", nullptr
};

// MIDI destination flag mapping
static const uint32_t midiDestFlags[] = {
    kNT_destinationBreakout,
    kNT_destinationSelectBus,
    kNT_destinationUSB,
    kNT_destinationInternal
};

// --- Channel state ---
struct ChannelState {
    SequencerEngine* engine;
    EngineType engineType;
    ClockProcessor clockProc;
    uint8_t lastMidiNote;
    bool midiNoteOn;
    int paramBase;           // Index of first per-channel common param in v[]
    int engineParamBase;     // Index of first engine param in v[]
    int numEngineParams;     // Actual number of engine params (no padding)
    int enginePageIndex;     // Index into pageDefs[] for engine page

    // Per-channel clock/reset edge detection
    bool clockHigh;
    bool resetHigh;
    bool noteGateHigh;
    int clockPeriodSamples;
    int samplesSinceClock;
    int gateSamplesRemaining; // -1=legato hold, 0=off, >0=countdown in samples

    // Cached CV output for sample-and-hold
    float cachedPitch;
    float cachedGate;
    float cachedVelocity;

    ChannelState()
        : engine(nullptr)
        , engineType(kEngineNone)
        , lastMidiNote(0)
        , midiNoteOn(false)
        , paramBase(0)
        , engineParamBase(0)
        , numEngineParams(0)
        , enginePageIndex(0)
        , clockHigh(false)
        , resetHigh(false)
        , noteGateHigh(false)
        , clockPeriodSamples(0)
        , samplesSinceClock(0)
        , gateSamplesRemaining(0)
        , cachedPitch(0.0f)
        , cachedGate(0.0f)
        , cachedVelocity(0.0f)
    {}
};

// --- Main algorithm struct ---
struct NtSeq : public _NT_algorithm {
    NtSeq() {}
    ~NtSeq() {}

    uint32_t numChannels;
    uint32_t sampleRate;

    // Parameter storage (mutable copy for dynamic updates)
    _NT_parameter paramDefs[kMaxTotalParams];
    int numParams;

    // Page storage
    _NT_parameterPages pagesDef;
    _NT_parameterPage pageDefs[kMaxPages];
    uint8_t pageIndices[kMaxPageIndices];
    int numPages;

    // Channel states
    ChannelState channels[kMaxChannels];

    // Dynamic page name buffers (2 pages per channel, 24 chars each)
    char pageNameBufs[kMaxChannels * 2][24];

    // Scale system
    ScaleQuantizer scaleQuantizer;
    _NT_sclRequest sclRequest;
    _NT_sclNote sclNotes[kMaxSclNotes];
    char sclName[22];
    char sclDescription[44];
    bool cardMounted;
    bool awaitingCallback;
    bool scaleDirty;

    // UI state
    int8_t focusChannel;  // -1 = overview, 0-7 = focused channel
    bool initDone;        // Set true after first step(); guards param sync during init
    bool pagesDirty;      // Set true when engine page sizes change; flushed in step()

    // Re-entrancy guard for engine switching (prevents recursive switches during value resets)
    bool switchingEngine;

    // Engine memory pool (engines placed here via placement new)
    // Must be aligned for ARM strd instructions used by engine constructors.
    // Each slot is 2048 bytes; 2048 is a multiple of 8, so all slots stay aligned.
    alignas(8) uint8_t enginePool[kMaxChannels * 2048];
};

#endif // NT_SEQ_H
