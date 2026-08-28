#ifndef NT_SEQ_H
#define NT_SEQ_H

#include <cstddef>
#include <distingnt/api.h>
#include <distingnt/microtuning.h>
#include <distingnt/wav.h>
#include "engines/SequencerEngine.h"
#include "scale/ScaleQuantizer.h"
#include "clock/ClockProcessor.h"

// Maximum parameters any single engine can define
constexpr int kMaxEngineParams = 32;

// Every fixed engine has exactly three pages.
constexpr int kNumPages = 3;

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

// --- Routing parameter offsets ---
enum RoutingParamOffset {
    kRouteClockIn = 0,
    kRouteResetIn,
    kRouteMode,
    kRouteGateOut,
    kRouteGateOutMode,
    kRoutePitchOut,
    kRoutePitchOutMode,
    kRouteVelocityOut,
    kRouteVelocityOutMode,
    kRouteMidiChannel,
    kRouteMidiDest,
    kRouteClockDiv,
    kRouteScaleEnable,
    kRouteNoteGateIn,
    kRouteNoteCvIn,
    kNumRoutingParams
};

constexpr int kMaxTotalParams = kNumGlobalParams + kNumRoutingParams + kMaxEngineParams;
constexpr int kMaxPageIndices = kMaxTotalParams;

// --- Engine type enum ---
enum EngineType {
    kEngineThorp = 0,
    kEngineSoma,
    kEngineSift,
    kEngineSeqMarkov,
    kEngineFerro,
    kEngineQuantum,
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

// --- Enum string arrays ---
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

// --- Sequencer state ---
struct SequencerState {
    SequencerEngine* engine;
    EngineType engineType;
    ClockProcessor clockProc;
    uint8_t lastMidiNote;
    bool midiNoteOn;
    int paramBase;           // Index of first routing parameter in v[]
    int engineParamBase;     // Index of first engine param in v[]
    int numEngineParams;

    // Clock/reset edge detection
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

    SequencerState()
        : engine(nullptr)
        , engineType(kEngineThorp)
        , lastMidiNote(0)
        , midiNoteOn(false)
        , paramBase(0)
        , engineParamBase(0)
        , numEngineParams(0)
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

    uint32_t sampleRate;

    // Parameter storage
    _NT_parameter paramDefs[kMaxTotalParams];

    // Page storage
    _NT_parameterPages pagesDef;
    _NT_parameterPage pageDefs[kNumPages];
    uint8_t pageIndices[kMaxPageIndices];

    SequencerState seq;

    // Scale system
    ScaleQuantizer scaleQuantizer;
    _NT_sclRequest sclRequest;
    _NT_sclNote sclNotes[kMaxSclNotes];
    char sclName[22];
    char sclDescription[44];
    bool cardMounted;
    bool awaitingCallback;
    bool scaleDirty;

    // Warp LUT cache (precomputed degree-to-warped-degree mapping)
    bool warpDirty;
    int8_t warpLut[128];       // warpLut[degree] = warped degree
    int cachedWarpNumNotes;     // 0 = no valid cache (warp inactive or no scale)

    bool initDone;        // Set true after first step(); guards param sync during init

};

void calculateRequirementsForEngine(
    _NT_algorithmRequirements& req,
    EngineType engineType);
_NT_algorithm* constructForEngine(
    const _NT_algorithmMemoryPtrs& ptrs,
    const _NT_algorithmRequirements& req,
    EngineType engineType);
SequencerEngine* createEngineInstance(EngineType type, uint8_t* mem);

#endif // NT_SEQ_H
