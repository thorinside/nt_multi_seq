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
static_assert(SomaEngine::kNumSomaParams <= kMaxEngineParams, "SomaEngine has too many params");
static_assert(AeSequencerEngine::kNumAeParams <= kMaxEngineParams, "AeSequencerEngine has too many params");
static_assert(SeqMarkovEngine::kNumMarkovParams <= kMaxEngineParams, "SeqMarkovEngine has too many params");
static_assert(ThorpEngine::kNumThorpParams <= kMaxEngineParams, "ThorpEngine has too many params");
static_assert(ARRAY_SIZE(specifications) == kMaxChannels, "specifications array must match kMaxChannels");

// Per-channel common parameters (static definitions as templates)
static const _NT_parameter channelCommonParams[] = {
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

// Note weight mode strings
static const char* const noteWeightStrings[] = {
    "Major", "Harmonic", "Equal", nullptr
};

// Global parameters (static definitions as templates)
static const _NT_parameter globalParams[] = {
    { .name = "Root Note", .min = 0, .max = 11, .def = 0, .unit = kNT_unitHasStrings, .scaling = 0, .enumStrings = nullptr },
    { .name = "Octave", .min = 0, .max = 8, .def = 4, .unit = kNT_unitNone, .scaling = 0, .enumStrings = nullptr },
    { .name = "Scale File", .min = 0, .max = 32767, .def = 0, .unit = kNT_unitConfirm, .scaling = 0, .enumStrings = nullptr },
    { .name = "Note Weight", .min = 0, .max = 2, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = noteWeightStrings },
    { .name = "Warp Amount", .min = 0, .max = 100, .def = 0, .unit = kNT_unitPercent, .scaling = 0, .enumStrings = nullptr },
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

// Convert spec value to engine type. Returns -1 for None (spec=0).
static int engineTypeFromSpec(int32_t specValue)
{
    if (specValue <= 0 || specValue > kNumEngineTypes) return -1;
    return specValue - 1;
}

// Count active (non-None) channels from specs
static int countActiveChannels(const int32_t* specifications)
{
    int count = 0;
    for (int i = 0; i < kMaxChannels; ++i) {
        if (specifications[SPEC_SEQ1 + i] > 0)
            count++;
    }
    return count;
}

// Engine name lookup for page naming
static const char* engineName(EngineType type)
{
    switch (type) {
    case kEngineThorp:     return "Thorp";
    case kEngineSoma:      return "Soma";
    case kEngineAeSeq:     return "AE Seq";
    case kEngineSeqMarkov: return "Markov";
    default:               return "?";
    }
}

// Copy string, return chars written (no null terminator added)
static int strCopy(char* dst, const char* src, int maxLen)
{
    int i = 0;
    while (src[i] && i < maxLen) {
        dst[i] = src[i];
        i++;
    }
    return i;
}

void calculateRequirements(_NT_algorithmRequirements& req, const int32_t* specifications)
{
    int numChannels = countActiveChannels(specifications);
    if (numChannels < 1) numChannels = 1;

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
    // Count active channels from specs
    int numChannels = countActiveChannels(specifications);
    if (numChannels < 1) numChannels = 1;

    // Validate against req (the framework allocated based on calculateRequirements)
    int paramsPerChannel = kNumChannelCommonParams + kMaxEngineParams;
    int reqChannels = (int)((req.numParameters - kNumGlobalParams) / paramsPerChannel);
    if (reqChannels > 0 && reqChannels < numChannels)
        numChannels = reqChannels;

    NtSeq* alg = new (ptrs.sram) NtSeq();
    alg->numChannels = numChannels;
    alg->sampleRate = NT_globals.sampleRate;
    alg->cardMounted = false;
    alg->awaitingCallback = false;
    alg->scaleDirty = false;
    alg->focusChannel = -1;

    // --- Determine engine types and build dense channel array ---
    // Count engine type occurrences for page name disambiguation
    int engineCount[kNumEngineTypes] = {};
    int denseIdx = 0;
    for (int slot = 0; slot < kMaxChannels && denseIdx < numChannels; ++slot) {
        int et = engineTypeFromSpec(specifications[SPEC_SEQ1 + slot]);
        if (et < 0) continue;
        engineCount[et]++;
        denseIdx++;
    }

    // Track per-type instance number during page name building
    int engineInstance[kNumEngineTypes] = {};

    // --- Build parameter definitions ---
    int p = 0;

    // Global params
    memcpy(&alg->paramDefs[p], globalParams, sizeof(globalParams));
    p += kNumGlobalParams;

    // Per-channel params — iterate spec slots, skip None
    int ch = 0;
    int pageNameIdx = 0;
    for (int slot = 0; slot < kMaxChannels && ch < numChannels; ++slot) {
        int et = engineTypeFromSpec(specifications[SPEC_SEQ1 + slot]);
        if (et < 0) continue;

        EngineType channelEngineType = (EngineType)et;
        alg->channels[ch].specSlot = slot;
        alg->channels[ch].paramBase = p;

        // Copy common channel params
        memcpy(&alg->paramDefs[p], channelCommonParams, sizeof(channelCommonParams));

        // Per-channel CV output defaults:
        // ch0: gate=14 pitch=15 vel=16, ch1: 17/18/19, etc.
        alg->paramDefs[p + kChGateOut].def = 15 + ch * 3;
        alg->paramDefs[p + kChCvOut].def = 16 + ch * 3;
        alg->paramDefs[p + kChVelOut].def = 17 + ch * 3;

        p += kNumChannelCommonParams;

        // Engine-specific params
        alg->channels[ch].engineParamBase = p;
        alg->channels[ch].engineType = channelEngineType;

        // Create engine
        uint8_t* engineMem = alg->enginePool + ch * 2048;
        alg->channels[ch].engine = createEngine(channelEngineType, engineMem);
        alg->channels[ch].engine->init(NT_globals.sampleRate);

        // Fill engine parameter slots — only actual params, no padding
        _NT_parameter engineDefs[kMaxEngineParams];
        int numEngineDefs = alg->channels[ch].engine->getParameterDefs(engineDefs);
        alg->channels[ch].numEngineParams = numEngineDefs;

        for (int i = 0; i < numEngineDefs; ++i)
            alg->paramDefs[p + i] = engineDefs[i];
        p += numEngineDefs;

        // Build page names into pageNameBufs
        const char* eName = engineName(channelEngineType);
        int inst = engineInstance[channelEngineType]++;
        bool needNumber = (engineCount[channelEngineType] > 1);

        // Routing page name: "<Engine>[ N] Routing"
        {
            char* buf = alg->pageNameBufs[pageNameIdx];
            int len = strCopy(buf, eName, 20);
            if (needNumber) {
                buf[len++] = ' ';
                len += NT_intToString(buf + len, inst + 1);
            }
            len += strCopy(buf + len, " Routing", 23 - len);
            buf[len] = 0;
        }
        pageNameIdx++;

        // Engine page name: "<Engine>[ N]"
        {
            char* buf = alg->pageNameBufs[pageNameIdx];
            int len = strCopy(buf, eName, 20);
            if (needNumber) {
                buf[len++] = ' ';
                len += NT_intToString(buf + len, inst + 1);
            }
            buf[len] = 0;
        }
        pageNameIdx++;

        // Initialize clock processor
        alg->channels[ch].clockProc.setDivider(1);
        int defaultClock = (int)(alg->sampleRate / 8);
        if (defaultClock < 1) defaultClock = 1;
        alg->channels[ch].clockPeriodSamples = defaultClock;
        alg->channels[ch].samplesSinceClock = defaultClock;
        alg->channels[ch].gateSamplesRemaining = 0;
        alg->channels[ch].lastMidiNote = 0;
        alg->channels[ch].midiNoteOn = false;

        ch++;
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
    for (int c = 0; c < (int)alg->numChannels; ++c) {
        int nameBufBase = c * 2;

        // Channel routing page
        uint8_t* chPageIdx = &alg->pageIndices[idxOffset];
        for (int i = 0; i < kNumChannelCommonParams; ++i)
            chPageIdx[i] = (uint8_t)(alg->channels[c].paramBase + i);
        alg->pageDefs[pageIdx] = {
            .name = alg->pageNameBufs[nameBufBase],
            .numParams = kNumChannelCommonParams,
            .group = kPageGroupRouting,
            .unused = {0, 0},
            .params = chPageIdx
        };
        idxOffset += kNumChannelCommonParams;
        pageIdx++;

        // Channel engine page — only actual engine params
        int numEng = alg->channels[c].numEngineParams;
        uint8_t* engPageIdx = &alg->pageIndices[idxOffset];
        for (int i = 0; i < numEng; ++i)
            engPageIdx[i] = (uint8_t)(alg->channels[c].engineParamBase + i);

        _NT_parameterPage engPage;
        (void)alg->channels[c].engine->getPageDefs(
            &engPage, engPageIdx, alg->channels[c].engineParamBase);

        alg->pageDefs[pageIdx] = {
            .name = alg->pageNameBufs[nameBufBase + 1],
            .numParams = (uint8_t)numEng,
            .group = kPageGroupEngine,
            .unused = {0, 0},
            .params = engPageIdx
        };
        alg->channels[c].enginePageIndex = pageIdx;
        idxOffset += numEng;
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

    // Gray out MIDI params initially (CV mode default)
    int algIdx = NT_algorithmIndex(static_cast<const _NT_algorithm*>(alg));
    if (algIdx >= 0) {
        uint32_t paramOffset = NT_parameterOffset();
        for (int c = 0; c < (int)alg->numChannels; ++c) {
            int base = alg->channels[c].paramBase;
            NT_setParameterGrayedOut(algIdx, (uint32_t)(base + kChMidiChannel) + paramOffset, true);
            NT_setParameterGrayedOut(algIdx, (uint32_t)(base + kChMidiDest) + paramOffset, true);
        }
    }

    return static_cast<_NT_algorithm*>(alg);
}
