#include "nt_seq.h"
#include "engines/ThorpEngine.h"
#include <new>
#include <distingnt/serialisation.h>

// Stub: virtual destructors emit a deleting-destructor thunk that references
// operator delete, but we only use placement new on the NT runtime.
#ifndef NT_EMU_DEBUG
void operator delete(void*) noexcept {}
#endif

void parameterChanged(_NT_algorithm* self, int p);
void step(_NT_algorithm* self, float* busFrames, int numFramesBy4);
void midiMessage(_NT_algorithm* self, uint8_t byte0, uint8_t byte1, uint8_t byte2);
bool draw(_NT_algorithm* self);
uint32_t hasCustomUi(_NT_algorithm* self);
void customUi(_NT_algorithm* self, const _NT_uiData& data);
int parameterString(_NT_algorithm* self, int p, int v, char* buff);

template<EngineType Type>
static void calculateDedicatedRequirements(
    _NT_algorithmRequirements& req,
    const int32_t* specifications)
{
    (void)specifications;
    calculateRequirementsForEngine(req, Type);
}

template<EngineType Type>
static _NT_algorithm* constructDedicated(
    const _NT_algorithmMemoryPtrs& ptrs,
    const _NT_algorithmRequirements& req,
    const int32_t* specifications)
{
    (void)specifications;
    return constructForEngine(ptrs, req, Type);
}

void serialise(_NT_algorithm* self, _NT_jsonStream& stream)
{
    NtSeq* alg = static_cast<NtSeq*>(self);
    if (alg->seq.engineType == kEngineThorp && alg->seq.engine) {
        stream.addMemberName("thorp");
        stream.openObject();
        static_cast<ThorpEngine*>(alg->seq.engine)->serialise(stream);
        stream.closeObject();
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
        if (alg->seq.engineType == kEngineThorp && alg->seq.engine) {
            if (parse.matchName("thorp")) {
                if (!static_cast<ThorpEngine*>(alg->seq.engine)->deserialise(parse))
                    return false;
                matched = true;
            }
        }
        if (!matched) {
            if (!parse.skipMember())
                return false;
        }
    }
    return true;
}

#define DEDICATED_FACTORY(variableName, factoryGuid, factoryName, factoryDescription, engineType) \
    static const _NT_factory variableName = { \
        .guid = factoryGuid, \
        .name = factoryName, \
        .description = factoryDescription, \
        .numSpecifications = 0, \
        .specifications = nullptr, \
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
        .parameterUiPrefix = nullptr, \
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
