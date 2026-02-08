#include "nt_seq.h"
#include <new>

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

static const _NT_factory factory = {
    .guid = NT_MULTICHAR('T', 'h', 'M', 's'),
    .name = "nt_multi_seq",
    .description = "Multi-channel sequencer with selectable engines and .scl microtuning",
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
    .serialise = nullptr,
    .deserialise = nullptr,
    .midiSysEx = nullptr,
    .parameterUiPrefix = parameterUiPrefix,
    .parameterString = parameterString,
};

extern "C" uintptr_t pluginEntry(_NT_selector selector, uint32_t data)
{
    switch (selector) {
    case kNT_selector_version:
        return kNT_apiVersionCurrent;
    case kNT_selector_numFactories:
        return 1;
    case kNT_selector_factoryInfo:
        return (data == 0) ? (uintptr_t)&factory : 0;
    }
    return 0;
}
