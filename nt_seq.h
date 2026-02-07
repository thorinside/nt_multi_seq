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

// Maximum parameters per engine (pre-allocated slots)
constexpr int kMaxEngineParams = 15;

// Maximum total parameters
// Global(3) + per-channel(12 common + 15 engine) * 4 = 111
constexpr int kMaxTotalParams = 3 + kMaxChannels * (12 + kMaxEngineParams);

// Maximum pages: 1 global + 2 per channel (common + engine)
constexpr int kMaxPages = 1 + kMaxChannels * 2;

// Maximum page param indices
constexpr int kMaxPageIndices = kMaxTotalParams;

// Maximum .scl notes
constexpr int kMaxSclNotes = 128;

// --- Global parameter indices ---
enum GlobalParam {
    kParamRootNote = 0,
    kParamOctave,
    kParamScaleFile,
    kNumGlobalParams
};

// --- Per-channel common parameter offsets ---
enum ChannelParamOffset {
    kChEngineType = 0,
    kChClockIn,
    kChResetIn,
    kChOutputMode,
    kChCvOut,
    kChCvOutMode,
    kChGateOut,
    kChGateOutMode,
    kChMidiChannel,
    kChMidiDest,
    kChClockDiv,
    kChScaleEnable,
    kNumChannelCommonParams
};

// --- Engine type enum ---
enum EngineType {
    kEngineThorp = 0,
    kEngineSoma,
    kEngineAeSeq,
    kEngineSeqMarkov,
    kNumEngineTypes
};

// --- Output mode enum ---
enum OutputMode {
    kOutputCV = 0,
    kOutputMIDI,
    kNumOutputModes
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
enum SpecIndex {
    SPEC_CHANNELS = 0,
    NUM_SPECS
};

static const _NT_specification specifications[] = {
    { .name = "Channels", .min = 1, .max = kMaxChannels, .def = 2, .type = kNT_typeGeneric },
};

// --- Enum string arrays ---
static const char* const engineTypeStrings[] = {
    "Thorp", "Soma", "AE Seq", "Markov", nullptr
};

static const char* const outputModeStrings[] = {
    "CV", "MIDI", nullptr
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
    int paramBase;           // Index of first per-channel param in v[]
    int engineParamBase;     // Index of first engine param in v[]

    // Per-channel clock/reset edge detection
    bool clockHigh;
    bool resetHigh;

    // Cached CV output for sample-and-hold
    float cachedPitch;
    float cachedGate;
    float cachedVelocity;

    ChannelState()
        : engine(nullptr)
        , engineType(kEngineSoma)
        , lastMidiNote(0)
        , midiNoteOn(false)
        , paramBase(0)
        , engineParamBase(0)
        , clockHigh(false)
        , resetHigh(false)
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
    int8_t focusChannel;  // -1 = overview, 0-3 = focused channel

    // Engine memory pool (engines placed here via placement new)
    // SomaEngine ~860 bytes, AeSequencerEngine ~880 bytes, Thorp ~2KB
    uint8_t enginePool[kMaxChannels * 2048];
};

#endif // NT_SEQ_H
