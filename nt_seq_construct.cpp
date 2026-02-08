#include "nt_seq.h"
#include "engines/SomaEngine.h"
#include "engines/AeSequencerEngine.h"
#include "engines/SeqMarkovEngine.h"
#include "engines/ThorpEngine.h"
#include <new>
#include <string.h>

// Compile-time guards: each engine must fit in a 2048-byte slot and
// must not require alignment stricter than the pool provides (8 bytes).
static_assert(sizeof(SomaEngine) <= 2048, "SomaEngine exceeds pool slot");
static_assert(sizeof(AeSequencerEngine) <= 2048, "AeSequencerEngine exceeds pool slot");
static_assert(sizeof(SeqMarkovEngine) <= 2048, "SeqMarkovEngine exceeds pool slot");
static_assert(sizeof(ThorpEngine) <= 2048, "ThorpEngine exceeds pool slot");
static_assert(alignof(SomaEngine) <= 8, "SomaEngine alignment exceeds pool");
static_assert(alignof(AeSequencerEngine) <= 8, "AeSequencerEngine alignment exceeds pool");
static_assert(alignof(SeqMarkovEngine) <= 8, "SeqMarkovEngine alignment exceeds pool");
static_assert(alignof(ThorpEngine) <= 8, "ThorpEngine alignment exceeds pool");

// Per-channel common parameters (static definitions as templates)
static const _NT_parameter channelCommonParams[] = {
    { .name = "Engine", .min = 0, .max = kNumEngineTypes - 1, .def = kEngineSoma, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = engineTypeStrings },
    NT_PARAMETER_CV_INPUT( "Clock In", 0, 1 )
    NT_PARAMETER_CV_INPUT( "Reset In", 0, 2 )
    { .name = "Routing", .min = 0, .max = kNumRoutings - 1, .def = kRoutingCV, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = routingStrings },
    NT_PARAMETER_CV_OUTPUT( "Pitch Out", 1, 15 )
    NT_PARAMETER_OUTPUT_MODE( "Pitch mode" )
    NT_PARAMETER_CV_OUTPUT( "Gate Out", 1, 14 )
    NT_PARAMETER_OUTPUT_MODE( "Gate mode" )
    NT_PARAMETER_CV_OUTPUT( "Velocity Out", 1, 16 )
    NT_PARAMETER_OUTPUT_MODE( "Velocity mode" )
    { .name = "MIDI Ch", .min = 1, .max = 16, .def = 1, .unit = kNT_unitNone, .scaling = 0, .enumStrings = nullptr },
    { .name = "MIDI Dest", .min = 0, .max = kNumMidiDests - 1, .def = kMidiDestBreakout, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = midiDestStrings },
    { .name = "Clock Div", .min = 1, .max = 16, .def = 1, .unit = kNT_unitNone, .scaling = 0, .enumStrings = nullptr },
    { .name = "Scale On", .min = 0, .max = 1, .def = 1, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = offOnStrings },
    NT_PARAMETER_CV_INPUT( "Note Gate In", 0, 3 )
    NT_PARAMETER_CV_INPUT( "Note CV In", 0, 4 )
};
static_assert(sizeof(channelCommonParams) / sizeof(channelCommonParams[0]) == kNumChannelCommonParams, "Channel param count mismatch");

// Global parameters (static definitions as templates)
static const _NT_parameter globalParams[] = {
    { .name = "Root Note", .min = 0, .max = 11, .def = 0, .unit = kNT_unitHasStrings, .scaling = 0, .enumStrings = nullptr },
    { .name = "Octave", .min = 0, .max = 8, .def = 4, .unit = kNT_unitNone, .scaling = 0, .enumStrings = nullptr },
    { .name = "Scale File", .min = 0, .max = 32767, .def = 0, .unit = kNT_unitConfirm, .scaling = 0, .enumStrings = nullptr },
};
static_assert(sizeof(globalParams) / sizeof(globalParams[0]) == kNumGlobalParams, "Global param count mismatch");

// Helper to create engine instance
static SequencerEngine* createEngine(EngineType type, uint8_t* mem)
{
    switch (type) {
    case kEngineSoma:
        return new (mem) SomaEngine();
    case kEngineAeSeq:
        return new (mem) AeSequencerEngine();
    case kEngineSeqMarkov:
        return new (mem) SeqMarkovEngine();
    case kEngineThorp:
        return new (mem) ThorpEngine();
    default:
        return new (mem) SomaEngine(); // Fallback to Soma
    }
}

void calculateRequirements(_NT_algorithmRequirements& req, const int32_t* specifications)
{
    int32_t numChannels = specifications[SPEC_CHANNELS];
    int paramsPerChannel = kNumChannelCommonParams + kMaxEngineParams;
    int totalParams = kNumGlobalParams + numChannels * paramsPerChannel;

    req.numParameters = totalParams;
    req.sram = sizeof(NtSeq);
    req.dram = 0;
    req.dtc = 0;
    req.itc = 0;
}

enum PageGroup : uint8_t {
    kPageGroupGlobal = 1,
    kPageGroupRouting = 2,
    kPageGroupEngine = 3
};

static void sclCallback(void* callbackData)
{
    NtSeq* pThis = (NtSeq*)callbackData;
    pThis->awaitingCallback = false;
    pThis->scaleDirty = true;
}

_NT_algorithm* construct(const _NT_algorithmMemoryPtrs& ptrs, const _NT_algorithmRequirements& req, const int32_t* specifications)
{
    int32_t numChannels = specifications[SPEC_CHANNELS];

    NtSeq* alg = new (ptrs.sram) NtSeq();
    alg->numChannels = numChannels;
    alg->sampleRate = NT_globals.sampleRate;
    alg->cardMounted = false;
    alg->awaitingCallback = false;
    alg->scaleDirty = false;
    alg->focusChannel = -1;

    // --- Build parameter definitions ---
    int p = 0;

    // Global params
    memcpy(&alg->paramDefs[p], globalParams, sizeof(globalParams));
    p += kNumGlobalParams;

    // Per-channel params
    for (int ch = 0; ch < (int)numChannels; ++ch) {
        alg->channels[ch].paramBase = p;

        // Copy common channel params
        memcpy(&alg->paramDefs[p], channelCommonParams, sizeof(channelCommonParams));

        // Per-channel CV output defaults:
        // ch0: gate=14 pitch=15 vel=16, ch1: 17/18/19, etc.
        alg->paramDefs[p + kChGateOut].def = 14 + ch * 3;
        alg->paramDefs[p + kChCvOut].def = 15 + ch * 3;
        alg->paramDefs[p + kChVelOut].def = 16 + ch * 3;

        p += kNumChannelCommonParams;

        // Engine-specific params (pre-allocate with defaults from Soma)
        alg->channels[ch].engineParamBase = p;
        alg->channels[ch].engineType = kEngineSoma;

        // Create engine
        uint8_t* engineMem = alg->enginePool + ch * 2048;
        alg->channels[ch].engine = createEngine(kEngineSoma, engineMem);
        alg->channels[ch].engine->init(NT_globals.sampleRate);

        // Fill engine parameter slots
        _NT_parameter engineDefs[kMaxEngineParams];
        int numEngineDefs = alg->channels[ch].engine->getParameterDefs(engineDefs);

        for (int i = 0; i < kMaxEngineParams; ++i) {
            if (i < numEngineDefs) {
                alg->paramDefs[p + i] = engineDefs[i];
            } else {
                // Unused slot - placeholder
                alg->paramDefs[p + i] = { .name = "-", .min = 0, .max = 0, .def = 0, .unit = kNT_unitNone, .scaling = 0, .enumStrings = nullptr };
            }
        }
        p += kMaxEngineParams;

        // Initialize clock processor
        alg->channels[ch].clockProc.setDivider(1);
        int defaultClock = (int)(alg->sampleRate / 8); // ~125ms at 48kHz
        if (defaultClock < 1) defaultClock = 1;
        alg->channels[ch].clockPeriodSamples = defaultClock;
        alg->channels[ch].samplesSinceClock = defaultClock;
        alg->channels[ch].gateSamplesRemaining = 0;
        alg->channels[ch].lastMidiNote = 0;
        alg->channels[ch].midiNoteOn = false;
    }

    alg->numParams = p;

    // --- Build parameter pages ---
    int pageIdx = 0;
    int idxOffset = 0;

    // Page: Global
    uint8_t* globalPageIdx = &alg->pageIndices[idxOffset];
    for (int i = 0; i < kNumGlobalParams; ++i)
        globalPageIdx[i] = (uint8_t)i;
    alg->pageDefs[pageIdx] = {
        .name = "Global",
        .numParams = kNumGlobalParams,
        .group = kPageGroupGlobal,
        .unused = {0, 0},
        .params = globalPageIdx
    };
    idxOffset += kNumGlobalParams;
    pageIdx++;

    // Per-channel pages
    static const char* chCommonPageNames[] = { "Sequencer 1 Routing", "Sequencer 2 Routing", "Sequencer 3 Routing", "Sequencer 4 Routing" };
    static const char* chEnginePageNames[] = { "Sequencer 1", "Sequencer 2", "Sequencer 3", "Sequencer 4" };

    for (int ch = 0; ch < (int)numChannels; ++ch) {
        // Channel common page
        uint8_t* chPageIdx = &alg->pageIndices[idxOffset];
        for (int i = 0; i < kNumChannelCommonParams; ++i)
            chPageIdx[i] = (uint8_t)(alg->channels[ch].paramBase + i);
        alg->pageDefs[pageIdx] = {
            .name = chCommonPageNames[ch],
            .numParams = kNumChannelCommonParams,
            .group = kPageGroupRouting,
            .unused = {0, 0},
            .params = chPageIdx
        };
        idxOffset += kNumChannelCommonParams;
        pageIdx++;

        // Channel engine page — reserve kMaxEngineParams index slots so
        // engine swap can update the page without overflowing into the
        // next page's index space.
        uint8_t* engPageIdx = &alg->pageIndices[idxOffset];
        // Pre-fill all slots with sequential engine param indices
        for (int i = 0; i < kMaxEngineParams; ++i)
            engPageIdx[i] = (uint8_t)(alg->channels[ch].engineParamBase + i);

        _NT_parameterPage engPage;
        (void)alg->channels[ch].engine->getPageDefs(
            &engPage, engPageIdx, alg->channels[ch].engineParamBase);

        // Use the page name from the engine but override with our naming.
        // Keep page length fixed to reserved slot count so all engine params
        // remain reachable even on hosts that snapshot page layout at construct.
        alg->pageDefs[pageIdx] = {
            .name = chEnginePageNames[ch],
            .numParams = (uint8_t)kMaxEngineParams,
            .group = kPageGroupEngine,
            .unused = {0, 0},
            .params = engPageIdx
        };
        alg->channels[ch].enginePageIndex = pageIdx;
        idxOffset += kMaxEngineParams;
        pageIdx++;
    }

    alg->numPages = pageIdx;
    alg->pagesDef.numPages = pageIdx;
    alg->pagesDef.pages = alg->pageDefs;

    // Assign to _NT_algorithm
    alg->parameters = alg->paramDefs;
    alg->parameterPages = &alg->pagesDef;

    // Setup .scl request
    alg->sclRequest.notes = alg->sclNotes;
    alg->sclRequest.maxNotes = kMaxSclNotes;
    alg->sclRequest.nameBuffer = alg->sclName;
    alg->sclRequest.nameBufferSize = sizeof(alg->sclName);
    alg->sclRequest.descriptionBuffer = alg->sclDescription;
    alg->sclRequest.descriptionBufferSize = sizeof(alg->sclDescription);
    alg->sclRequest.callback = sclCallback;
    alg->sclRequest.callbackData = alg;

    // Gray out MIDI params initially (CV mode default) and unused engine slots
    int algIdx = NT_algorithmIndex(static_cast<const _NT_algorithm*>(alg));
    if (algIdx >= 0) {
        uint32_t paramOffset = NT_parameterOffset();
        for (int ch = 0; ch < (int)numChannels; ++ch) {
            int base = alg->channels[ch].paramBase;
            // Gray out MIDI params when in CV mode (default)
            NT_setParameterGrayedOut(algIdx, (uint32_t)(base + kChMidiChannel) + paramOffset, true);
            NT_setParameterGrayedOut(algIdx, (uint32_t)(base + kChMidiDest) + paramOffset, true);

            // Gray out unused engine param slots
            int numEngDefs = 0;
            _NT_parameter tempDefs[kMaxEngineParams];
            if (alg->channels[ch].engine)
                numEngDefs = alg->channels[ch].engine->getParameterDefs(tempDefs);
            for (int i = numEngDefs; i < kMaxEngineParams; ++i)
                NT_setParameterGrayedOut(algIdx, (uint32_t)(alg->channels[ch].engineParamBase + i) + paramOffset, true);
        }
    }

    return static_cast<_NT_algorithm*>(alg);
}
