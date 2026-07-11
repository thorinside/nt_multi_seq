#include "nt_seq.h"
#include "engines/ThorpEngine.h"
#include <new>
#include <distingnt/serialisation.h>

// Stub: virtual destructors emit a deleting-destructor thunk that references
// operator delete, but we only use placement new on the NT runtime.
#ifndef NT_EMU_DEBUG
void operator delete(void*) noexcept {}
#endif

// Forward declarations
void calculateRequirements(_NT_algorithmRequirements& req, const int32_t* specifications);
_NT_algorithm* construct(const _NT_algorithmMemoryPtrs& ptrs, const _NT_algorithmRequirements& req, const int32_t* specifications);
void parameterChanged(_NT_algorithm* self, int p);
void step(_NT_algorithm* self, float* busFrames, int numFramesBy4);
void midiMessage(_NT_algorithm* self, uint8_t byte0, uint8_t byte1, uint8_t byte2);
bool draw(_NT_algorithm* self);
uint32_t hasCustomUi(_NT_algorithm* self);
void customUi(_NT_algorithm* self, const _NT_uiData& data);
int parameterUiPrefix(_NT_algorithm* self, int p, char* buff);
int parameterString(_NT_algorithm* self, int p, int v, char* buff);

template<EngineType Type>
static void calculateDedicatedRequirements(
    _NT_algorithmRequirements& req,
    const int32_t* specifications)
{
    calculateRequirementsForEngine(req, specifications, Type);
}

template<EngineType Type>
static _NT_algorithm* constructDedicated(
    const _NT_algorithmMemoryPtrs& ptrs,
    const _NT_algorithmRequirements& req,
    const int32_t* specifications)
{
    return constructForEngine(ptrs, req, specifications, Type);
}

void serialise(_NT_algorithm* self, _NT_jsonStream& stream)
{
    NtSeq* alg = static_cast<NtSeq*>(self);
    // Store engine type per channel, then delegate to engine for extra state
    for (uint32_t ch = 0; ch < alg->numChannels; ++ch) {
        // Engine type is already in v[] (framework saves it), but Thorp has extra state
        if (alg->channels[ch].engineType == kEngineThorp && alg->channels[ch].engine) {
            char name[4] = { 't', (char)('0' + ch), 0, 0 };
            stream.addMemberName(name);
            stream.openObject();
            static_cast<ThorpEngine*>(alg->channels[ch].engine)->serialise(stream);
            stream.closeObject();
        }
    }
}

bool deserialise(_NT_algorithm* self, _NT_jsonParse& parse)
{
    NtSeq* alg = static_cast<NtSeq*>(self);
    int numMembers;
    if (!parse.numberOfObjectMembers(numMembers))
        return false;

    for (int m = 0; m < numMembers; ++m) {
        bool matched = false;
        for (uint32_t ch = 0; ch < alg->numChannels; ++ch) {
            if (alg->channels[ch].engineType == kEngineThorp && alg->channels[ch].engine) {
                char name[4] = { 't', (char)('0' + ch), 0, 0 };
                if (parse.matchName(name)) {
                    if (!static_cast<ThorpEngine*>(alg->channels[ch].engine)->deserialise(parse))
                        return false;
                    matched = true;
                    break;
                }
            }
        }
        if (!matched) {
            if (!parse.skipMember())
                return false;
        }
    }
    return true;
}

static const _NT_factory legacyFactory = {
    .guid = NT_MULTICHAR('T', 'h', 'M', 's'),
    .name = "nt_multi_seq",
    .description = "Multi-channel sequencer with runtime engine selection",
    .numSpecifications = ARRAY_SIZE(specifications),
    .specifications = specifications,
    .calculateStaticRequirements = nullptr,
    .initialise = nullptr,
    .calculateRequirements = calculateRequirements,
    .construct = construct,
    .parameterChanged = parameterChanged,
    .step = step,
    .draw = draw,
    .midiRealtime = nullptr,
    .midiMessage = midiMessage,
    .tags = kNT_tagInstrument,
    .hasCustomUi = hasCustomUi,
    .customUi = customUi,
    .setupUi = nullptr,
    .serialise = serialise,
    .deserialise = deserialise,
    .midiSysEx = nullptr,
    .parameterUiPrefix = parameterUiPrefix,
    .parameterString = parameterString,
};

#define DEDICATED_FACTORY(variableName, factoryGuid, factoryName, factoryDescription, engineType) \
    static const _NT_factory variableName = { \
        .guid = factoryGuid, \
        .name = factoryName, \
        .description = factoryDescription, \
        .numSpecifications = ARRAY_SIZE(specifications), \
        .specifications = specifications, \
        .calculateStaticRequirements = nullptr, \
        .initialise = nullptr, \
        .calculateRequirements = calculateDedicatedRequirements<engineType>, \
        .construct = constructDedicated<engineType>, \
        .parameterChanged = parameterChanged, \
        .step = step, \
        .draw = draw, \
        .midiRealtime = nullptr, \
        .midiMessage = midiMessage, \
        .tags = kNT_tagInstrument, \
        .hasCustomUi = hasCustomUi, \
        .customUi = customUi, \
        .setupUi = nullptr, \
        .serialise = serialise, \
        .deserialise = deserialise, \
        .midiSysEx = nullptr, \
        .parameterUiPrefix = parameterUiPrefix, \
        .parameterString = parameterString, \
    }

DEDICATED_FACTORY(thorpFactory,
    NT_MULTICHAR('N', 's', 'T', 'h'),
    "Thorp", "Pattern arpeggiator and chain sequencer", kEngineThorp);
DEDICATED_FACTORY(somaFactory,
    NT_MULTICHAR('N', 's', 'S', 'o'),
    "Soma", "Mutating probability sequencer", kEngineSoma);
DEDICATED_FACTORY(aeSeqFactory,
    NT_MULTICHAR('N', 's', 'A', 'e'),
    "AE Seq", "Analog-style CV and gate sequencer", kEngineAeSeq);
DEDICATED_FACTORY(markovFactory,
    NT_MULTICHAR('N', 's', 'M', 'k'),
    "Markov", "Markov-chain melodic sequencer", kEngineSeqMarkov);
DEDICATED_FACTORY(ferroFactory,
    NT_MULTICHAR('N', 's', 'F', 'e'),
    "Ferro", "Ferromagnetic tape-loop chord sequencer", kEngineFerro);
DEDICATED_FACTORY(quantumFactory,
    NT_MULTICHAR('N', 's', 'Q', 'u'),
    "Quantum", "Hierarchical generative sequencer", kEngineQuantum);

#undef DEDICATED_FACTORY

static const _NT_factory* const factories[] = {
    &legacyFactory,
    &thorpFactory,
    &somaFactory,
    &aeSeqFactory,
    &markovFactory,
    &ferroFactory,
    &quantumFactory,
};

extern "C" uintptr_t pluginEntry(_NT_selector selector, uint32_t data)
{
    switch (selector) {
    case kNT_selector_version:
        return kNT_apiVersionCurrent;
    case kNT_selector_numFactories:
        return ARRAY_SIZE(factories);
    case kNT_selector_factoryInfo:
        return data < ARRAY_SIZE(factories) ? (uintptr_t)factories[data] : 0;
    }
    return 0;
}
