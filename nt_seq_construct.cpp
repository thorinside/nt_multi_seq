#include "nt_seq.h"
#include "engines/SomaEngine.h"
#include "engines/AeSequencerEngine.h"
#include "engines/SeqMarkovEngine.h"
#include "engines/ThorpEngine.h"
#include "engines/FerromagneticEngine.h"
#include <new>
#include <string.h>

// Compile-time guards: each engine must fit in a 2048-byte slot and
// must not require alignment stricter than the pool provides (8 bytes).
static_assert(sizeof(SomaEngine) <= 2048, "SomaEngine exceeds pool slot");
static_assert(sizeof(AeSequencerEngine) <= 2048, "AeSequencerEngine exceeds pool slot");
static_assert(sizeof(SeqMarkovEngine) <= 2048, "SeqMarkovEngine exceeds pool slot");
static_assert(sizeof(ThorpEngine) <= 2048, "ThorpEngine exceeds pool slot");
static_assert(sizeof(FerromagneticEngine) <= 2048, "FerromagneticEngine exceeds pool slot");
static_assert(alignof(SomaEngine) <= 8, "SomaEngine alignment exceeds pool");
static_assert(alignof(AeSequencerEngine) <= 8, "AeSequencerEngine alignment exceeds pool");
static_assert(alignof(SeqMarkovEngine) <= 8, "SeqMarkovEngine alignment exceeds pool");
static_assert(alignof(ThorpEngine) <= 8, "ThorpEngine alignment exceeds pool");
static_assert(alignof(FerromagneticEngine) <= 8, "FerromagneticEngine alignment exceeds pool");
static_assert(SomaEngine::kNumSomaParams <= kMaxEngineParams, "SomaEngine has too many params");
static_assert(AeSequencerEngine::kNumAeParams <= kMaxEngineParams, "AeSequencerEngine has too many params");
static_assert(SeqMarkovEngine::kNumMarkovParams <= kMaxEngineParams, "SeqMarkovEngine has too many params");
static_assert(ThorpEngine::kNumThorpParams <= kMaxEngineParams, "ThorpEngine has too many params");
static_assert(FerromagneticEngine::kNumFerroParams <= kMaxEngineParams, "FerromagneticEngine has too many params");
static_assert(ARRAY_SIZE(specifications) == NUM_SPECS, "specifications array must match NUM_SPECS");

// Per-channel common parameters (static definitions as templates)
static const _NT_parameter channelCommonParams[] = {
    NT_PARAMETER_CV_INPUT("Clock In", 0, 1)
    NT_PARAMETER_CV_INPUT("Reset In", 0, 2)
    { .name = "Routing", .min = 0, .max = kNumRoutings - 1, .def = kRoutingCV, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = routingStrings },
    NT_PARAMETER_CV_OUTPUT_WITH_MODE("Gate Out", 0, 14)
    NT_PARAMETER_CV_OUTPUT_WITH_MODE("Pitch Out", 0, 15)
    NT_PARAMETER_CV_OUTPUT_WITH_MODE("Velocity Out", 0, 16)
    { .name = "MIDI Ch", .min = 1, .max = 16, .def = 1, .unit = kNT_unitNone, .scaling = 0, .enumStrings = nullptr },
    { .name = "MIDI Dest", .min = 0, .max = kNumMidiDests - 1, .def = kMidiDestBreakout, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = midiDestStrings },
    { .name = "Clock Div", .min = 1, .max = 16, .def = 1, .unit = kNT_unitNone, .scaling = 0, .enumStrings = nullptr },
    { .name = "Scale On", .min = 0, .max = 1, .def = 1, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = offOnStrings },
    NT_PARAMETER_CV_INPUT("Note Gate In", 0, 3)
    NT_PARAMETER_CV_INPUT("Note CV In", 0, 4)
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

// Placeholder parameter for greyed-out engine slots
static const _NT_parameter placeholderParam = {
    .name = "-", .min = 0, .max = 0, .def = 0, .unit = kNT_unitNone, .scaling = 0, .enumStrings = nullptr
};

// Engine Type parameter template (used for each channel on the Engines page)
static const _NT_parameter engineTypeParam = {
    .name = "Engine", .min = 0, .max = kNumEngineTypes, .def = 0,
    .unit = kNT_unitEnum, .scaling = 0, .enumStrings = engineTypeWithNoneStrings
};

enum PageGroup : uint8_t {
    kPageGroupGlobal = 1,
    kPageGroupEngines = 2,
    kPageGroupRouting = 3,
    kPageGroupEngine = 4
};

static void sclCallback(void* callbackData)
{
    NtSeq* pThis = (NtSeq*)callbackData;
    pThis->awaitingCallback = false;
    pThis->scaleDirty = true;
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
    int numChannels = specifications[SPEC_CHANNELS];
    if (numChannels < 1) numChannels = 1;
    if (numChannels > kMaxChannels) numChannels = kMaxChannels;

    // Global + engine-type-per-channel + per-channel(common + engine slots)
    int totalParams = kNumGlobalParams + numChannels + numChannels * kParamsPerChannel;

    req.numParameters = totalParams;
    req.sram = sizeof(NtSeq);
    req.dram = 0;
    req.dtc = 0;
    req.itc = 0;
}

_NT_algorithm* construct(const _NT_algorithmMemoryPtrs& ptrs, const _NT_algorithmRequirements& req, const int32_t* specifications)
{
    int numChannels = specifications[SPEC_CHANNELS];
    if (numChannels < 1) numChannels = 1;
    if (numChannels > kMaxChannels) numChannels = kMaxChannels;

    NtSeq* alg = new (ptrs.sram) NtSeq();
    alg->numChannels = numChannels;
    alg->sampleRate = NT_globals.sampleRate;
    alg->cardMounted = false;
    alg->awaitingCallback = false;
    alg->scaleDirty = false;
    alg->focusChannel = -1;
    alg->initDone = false;
    alg->switchingEngine = false;
    alg->warpDirty = false;
    alg->cachedWarpNumNotes = 0;

    // --- Build parameter definitions ---
    int p = 0;

    // Global params
    memcpy(&alg->paramDefs[p], globalParams, sizeof(globalParams));
    p += kNumGlobalParams;

    // Engine Type params (one per channel) — these go on the "Engines" page
    int engineTypeBase = p;
    for (int ch = 0; ch < numChannels; ++ch) {
        alg->paramDefs[p] = engineTypeParam;
        p++;
    }

    // Per-channel params: 15 common + 32 engine slots
    for (int ch = 0; ch < numChannels; ++ch) {
        alg->channels[ch].paramBase = p;

        // Copy common channel params
        memcpy(&alg->paramDefs[p], channelCommonParams, sizeof(channelCommonParams));

        // Per-channel CV output defaults:
        // ch0: gate=15 pitch=16 vel=17, ch1: 18/19/20, etc.
        alg->paramDefs[p + kChGateOut].def = 15 + ch * 3;
        alg->paramDefs[p + kChCvOut].def   = 16 + ch * 3;
        alg->paramDefs[p + kChVelOut].def  = 17 + ch * 3;

        p += kNumChannelCommonParams;

        // Engine param slots — all placeholders (greyed out for None)
        alg->channels[ch].engineParamBase = p;
        alg->channels[ch].numEngineParams = kMaxEngineParams;
        alg->channels[ch].engineType = kEngineNone;
        alg->channels[ch].engine = nullptr;

        for (int i = 0; i < kMaxEngineParams; ++i)
            alg->paramDefs[p + i] = placeholderParam;
        p += kMaxEngineParams;

        // Initialize clock processor
        alg->channels[ch].clockProc.setDivider(1);
        int defaultClock = (int)(alg->sampleRate / 8);
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

    // Page: Engines (one Engine Type param per channel)
    {
        uint8_t* enginesPageIdx = &alg->pageIndices[idxOffset];
        for (int ch = 0; ch < numChannels; ++ch)
            enginesPageIdx[ch] = (uint8_t)(engineTypeBase + ch);
        alg->pageDefs[pageIdx] = {
            .name = "Engines",
            .numParams = (uint8_t)numChannels,
            .group = kPageGroupEngines,
            .unused = {0, 0},
            .params = enginesPageIdx
        };
        idxOffset += numChannels;
        pageIdx++;
    }

    // Build page names for all channels
    for (int ch = 0; ch < numChannels; ++ch) {
        int nameBufBase = ch * 2;
        {
            char* buf = alg->pageNameBufs[nameBufBase];
            int len = strCopy(buf, "Ch ", 3);
            len += NT_intToString(buf + len, ch + 1);
            len += strCopy(buf + len, " Routing", 23 - len);
            buf[len] = 0;
        }
        {
            char* buf = alg->pageNameBufs[nameBufBase + 1];
            int len = strCopy(buf, "Ch ", 3);
            len += NT_intToString(buf + len, ch + 1);
            buf[len] = 0;
        }
    }

    // All routing pages first
    for (int ch = 0; ch < numChannels; ++ch) {
        uint8_t* chPageIdx = &alg->pageIndices[idxOffset];
        for (int i = 0; i < kNumChannelCommonParams; ++i)
            chPageIdx[i] = (uint8_t)(alg->channels[ch].paramBase + i);
        alg->pageDefs[pageIdx] = {
            .name = alg->pageNameBufs[ch * 2],
            .numParams = kNumChannelCommonParams,
            .group = kPageGroupRouting,
            .unused = {0, 0},
            .params = chPageIdx
        };
        idxOffset += kNumChannelCommonParams;
        pageIdx++;
    }

    // Then all engine pages
    for (int ch = 0; ch < numChannels; ++ch) {
        uint8_t* engPageIdx = &alg->pageIndices[idxOffset];
        for (int i = 0; i < kMaxEngineParams; ++i)
            engPageIdx[i] = (uint8_t)(alg->channels[ch].engineParamBase + i);
        alg->pageDefs[pageIdx] = {
            .name = alg->pageNameBufs[ch * 2 + 1],
            .numParams = 0,
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

    // Gray out MIDI params initially (CV mode default) and all engine slots (None)
    int algIdx = NT_algorithmIndex(static_cast<const _NT_algorithm*>(alg));
    if (algIdx >= 0) {
        uint32_t paramOffset = NT_parameterOffset();
        for (int ch = 0; ch < numChannels; ++ch) {
            int base = alg->channels[ch].paramBase;
            NT_setParameterGrayedOut(algIdx, (uint32_t)(base + kChMidiChannel) + paramOffset, true);
            NT_setParameterGrayedOut(algIdx, (uint32_t)(base + kChMidiDest) + paramOffset, true);

        }
    }

    return static_cast<_NT_algorithm*>(alg);
}
