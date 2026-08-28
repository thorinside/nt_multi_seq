#include "nt_seq.h"
#include "engines/SomaEngine.h"
#include "engines/SiftEngine.h"
#include "engines/SeqMarkovEngine.h"
#include "engines/ThorpEngine.h"
#include "engines/FerromagneticEngine.h"
#include "engines/QuantumEngine.h"
#include <new>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static_assert(alignof(SomaEngine) <= 8, "SomaEngine alignment exceeds 8 bytes");
static_assert(alignof(SiftEngine) <= 8, "SiftEngine alignment exceeds 8 bytes");
static_assert(alignof(SeqMarkovEngine) <= 8, "SeqMarkovEngine alignment exceeds 8 bytes");
static_assert(alignof(ThorpEngine) <= 8, "ThorpEngine alignment exceeds 8 bytes");
static_assert(alignof(FerromagneticEngine) <= 8, "FerromagneticEngine alignment exceeds 8 bytes");
static_assert(alignof(QuantumEngine) <= 8, "QuantumEngine alignment exceeds 8 bytes");
static_assert(SomaEngine::kNumSomaParams <= kMaxEngineParams, "SomaEngine has too many params");
static_assert(SiftEngine::kNumSiftParams <= kMaxEngineParams, "SiftEngine has too many params");
static_assert(SeqMarkovEngine::kNumMarkovParams <= kMaxEngineParams, "SeqMarkovEngine has too many params");
static_assert(ThorpEngine::kNumThorpParams <= kMaxEngineParams, "ThorpEngine has too many params");
static_assert(FerromagneticEngine::kNumFerroParams <= kMaxEngineParams, "FerromagneticEngine has too many params");
static_assert(QuantumEngine::kNumQuantumParams <= kMaxEngineParams, "QuantumEngine has too many params");

static const _NT_parameter routingParams[] = {
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
static_assert(ARRAY_SIZE(routingParams) == kNumRoutingParams, "Routing param count mismatch");

static const char* const noteWeightStrings[] = {
    "Major", "Harmonic", "Equal", nullptr
};

static const _NT_parameter globalParams[] = {
    { .name = "Root Note", .min = 0, .max = 11, .def = 0, .unit = kNT_unitHasStrings, .scaling = 0, .enumStrings = nullptr },
    { .name = "Octave", .min = 0, .max = 8, .def = 4, .unit = kNT_unitNone, .scaling = 0, .enumStrings = nullptr },
    { .name = "Scale File", .min = 0, .max = 32767, .def = 0, .unit = kNT_unitConfirm, .scaling = 0, .enumStrings = nullptr },
    { .name = "Note Weight", .min = 0, .max = 2, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = noteWeightStrings },
    { .name = "Warp Amount", .min = 0, .max = 100, .def = 0, .unit = kNT_unitPercent, .scaling = 0, .enumStrings = nullptr },
};
static_assert(ARRAY_SIZE(globalParams) == kNumGlobalParams, "Global param count mismatch");

enum PageGroup : uint8_t {
    kPageGroupGlobal = 1,
    kPageGroupRouting,
    kPageGroupEngine
};

static void sclCallback(void* callbackData)
{
    NtSeq* alg = static_cast<NtSeq*>(callbackData);
    alg->awaitingCallback = false;
    alg->scaleDirty = true;
}

static int engineParameterCount(EngineType type)
{
    switch (type) {
    case kEngineThorp:     return ThorpEngine::kNumThorpParams;
    case kEngineSoma:      return SomaEngine::kNumSomaParams;
    case kEngineSift:      return SiftEngine::kNumSiftParams;
    case kEngineSeqMarkov: return SeqMarkovEngine::kNumMarkovParams;
    case kEngineFerro:     return FerromagneticEngine::kNumFerroParams;
    case kEngineQuantum:   return QuantumEngine::kNumQuantumParams;
    default:               return 0;
    }
}

static size_t engineSize(EngineType type)
{
    switch (type) {
    case kEngineThorp:     return sizeof(ThorpEngine);
    case kEngineSoma:      return sizeof(SomaEngine);
    case kEngineSift:      return sizeof(SiftEngine);
    case kEngineSeqMarkov: return sizeof(SeqMarkovEngine);
    case kEngineFerro:     return sizeof(FerromagneticEngine);
    case kEngineQuantum:   return sizeof(QuantumEngine);
    default:               return 0;
    }
}

SequencerEngine* createEngineInstance(EngineType type, uint8_t* mem)
{
    switch (type) {
    case kEngineThorp:     return new (mem) ThorpEngine();
    case kEngineSoma:      return new (mem) SomaEngine();
    case kEngineSift:      return new (mem) SiftEngine();
    case kEngineSeqMarkov: return new (mem) SeqMarkovEngine();
    case kEngineFerro:     return new (mem) FerromagneticEngine();
    case kEngineQuantum:   return new (mem) QuantumEngine();
    default:               return nullptr;
    }
}

void calculateRequirementsForEngine(
    _NT_algorithmRequirements& req,
    EngineType engineType)
{
    req.numParameters = kNumGlobalParams
        + kNumRoutingParams
        + engineParameterCount(engineType);
    req.sram = sizeof(NtSeq) + 7 + engineSize(engineType);
    req.dram = 0;
    req.dtc = 0;
    req.itc = 0;
}

_NT_algorithm* constructForEngine(
    const _NT_algorithmMemoryPtrs& ptrs,
    const _NT_algorithmRequirements& req,
    EngineType engineType)
{
    (void)req;

    NtSeq* alg = new (ptrs.sram) NtSeq();
    alg->sampleRate = NT_globals.sampleRate;
    alg->cardMounted = false;
    alg->awaitingCallback = false;
    alg->scaleDirty = false;
    alg->warpDirty = false;
    alg->cachedWarpNumNotes = 0;
    alg->initDone = false;
    alg->sclName[0] = 0;
    alg->sclDescription[0] = 0;

    uintptr_t engineAddress = reinterpret_cast<uintptr_t>(ptrs.sram + sizeof(NtSeq));
    engineAddress = (engineAddress + 7u) & ~static_cast<uintptr_t>(7u);
    alg->seq.engine = createEngineInstance(engineType, reinterpret_cast<uint8_t*>(engineAddress));
    alg->seq.engineType = engineType;
    if (alg->seq.engine)
        alg->seq.engine->init(alg->sampleRate);

    int p = 0;
    memcpy(&alg->paramDefs[p], globalParams, sizeof(globalParams));
    p += kNumGlobalParams;

    alg->seq.paramBase = p;
    memcpy(&alg->paramDefs[p], routingParams, sizeof(routingParams));
    p += kNumRoutingParams;

    alg->seq.engineParamBase = p;
    alg->seq.numEngineParams = alg->seq.engine
        ? alg->seq.engine->getParameterDefs(&alg->paramDefs[p])
        : 0;

    alg->seq.clockProc.setDivider(1);
    int defaultClock = static_cast<int>(alg->sampleRate / 8);
    if (defaultClock < 1)
        defaultClock = 1;
    alg->seq.clockPeriodSamples = defaultClock;
    alg->seq.samplesSinceClock = defaultClock;
    alg->seq.gateSamplesRemaining = 0;
    alg->seq.lastMidiNote = 0;
    alg->seq.midiNoteOn = false;
    if (alg->seq.engine)
        alg->seq.engine->setWeightMode(globalParams[kParamNoteWeight].def);

    int indexOffset = 0;
    uint8_t* globalPageIndices = &alg->pageIndices[indexOffset];
    for (int i = 0; i < kNumGlobalParams; ++i)
        globalPageIndices[i] = static_cast<uint8_t>(i);
    alg->pageDefs[0] = {
        .name = "Global",
        .numParams = kNumGlobalParams,
        .group = kPageGroupGlobal,
        .unused = {0, 0},
        .params = globalPageIndices
    };
    indexOffset += kNumGlobalParams;

    uint8_t* routingPageIndices = &alg->pageIndices[indexOffset];
    for (int i = 0; i < kNumRoutingParams; ++i)
        routingPageIndices[i] = static_cast<uint8_t>(alg->seq.paramBase + i);
    alg->pageDefs[1] = {
        .name = "Routing",
        .numParams = kNumRoutingParams,
        .group = kPageGroupRouting,
        .unused = {0, 0},
        .params = routingPageIndices
    };
    indexOffset += kNumRoutingParams;

    uint8_t* enginePageIndices = &alg->pageIndices[indexOffset];
    for (int i = 0; i < alg->seq.numEngineParams; ++i)
        enginePageIndices[i] = static_cast<uint8_t>(alg->seq.engineParamBase + i);
    alg->pageDefs[2] = {
        .name = alg->seq.engine ? alg->seq.engine->name() : "Sequencer",
        .numParams = static_cast<uint8_t>(alg->seq.numEngineParams),
        .group = kPageGroupEngine,
        .unused = {0, 0},
        .params = enginePageIndices
    };

    alg->pagesDef.numPages = kNumPages;
    alg->pagesDef.pages = alg->pageDefs;
    alg->parameters = alg->paramDefs;
    alg->parameterPages = &alg->pagesDef;

    alg->sclRequest.notes = alg->sclNotes;
    alg->sclRequest.maxNotes = kMaxSclNotes;
    alg->sclRequest.nameBuffer = alg->sclName;
    alg->sclRequest.nameBufferSize = sizeof(alg->sclName);
    alg->sclRequest.descriptionBuffer = alg->sclDescription;
    alg->sclRequest.descriptionBufferSize = sizeof(alg->sclDescription);
    alg->sclRequest.callback = sclCallback;
    alg->sclRequest.callbackData = alg;

    int algIdx = NT_algorithmIndex(static_cast<const _NT_algorithm*>(alg));
    if (algIdx >= 0) {
        uint32_t paramOffset = NT_parameterOffset();
        int base = alg->seq.paramBase;
        NT_setParameterGrayedOut(algIdx, static_cast<uint32_t>(base + kRouteMidiChannel) + paramOffset, true);
        NT_setParameterGrayedOut(algIdx, static_cast<uint32_t>(base + kRouteMidiDest) + paramOffset, true);
    }

    return static_cast<_NT_algorithm*>(alg);
}
