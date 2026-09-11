#include "mix/nt_seq_mix.h"
#include "common/ParamStrings.h"
#include <new>
#include <string.h>

static const char* const mixModeStrings[] = { "Sum", "Average", nullptr };

static const _NT_parameter mixParams[] = {
    NT_PARAMETER_CV_INPUT("Pitch In", 0, 15)
    NT_PARAMETER_CV_OUTPUT("Pitch Out", 0, 15)
    { .name = "Pitch Out mode", .min = 0, .max = 1, .def = 1, .unit = kNT_unitOutputMode, .scaling = 0, .enumStrings = nullptr },
    { .name = "Mix", .min = 0, .max = kNumMixModes - 1, .def = kMixSum, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = mixModeStrings },
    { .name = "Sources", .min = 1, .max = 8, .def = 2, .unit = kNT_unitNone, .scaling = 0, .enumStrings = nullptr },
    { .name = "Scale On", .min = 0, .max = 1, .def = 1, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = kOffOnStrings },
    { .name = "Root Note", .min = 0, .max = 11, .def = 0, .unit = kNT_unitHasStrings, .scaling = 0, .enumStrings = nullptr },
    { .name = "Scale File", .min = 0, .max = 32767, .def = 0, .unit = kNT_unitConfirm, .scaling = 0, .enumStrings = nullptr },
};
static_assert(ARRAY_SIZE(mixParams) == kNumMixParams, "Mix param count mismatch");

static void mixCalculateRequirements(_NT_algorithmRequirements& req, const int32_t* specifications)
{
    (void)specifications;
    req.numParameters = kNumMixParams;
    req.sram = sizeof(NtSeqMix);
    req.dram = 0;
    req.dtc = 0;
    req.itc = 0;
}

static _NT_algorithm* mixConstruct(
    const _NT_algorithmMemoryPtrs& ptrs,
    const _NT_algorithmRequirements& req,
    const int32_t* specifications)
{
    (void)req;
    (void)specifications;

    NtSeqMix* alg = new (ptrs.sram) NtSeqMix();
    alg->scale.init();
    alg->cacheValid = false;
    alg->lastInput = 0.0f;
    alg->lastOutput = 0.0f;
    alg->lastMode = 0;
    alg->lastSources = 0;
    alg->lastRoot = 0;
    alg->lastScale = nullptr;

    memcpy(alg->paramDefs, mixParams, sizeof(mixParams));
    for (int i = 0; i < kNumMixParams; ++i)
        alg->pageIndices[i] = static_cast<uint8_t>(i);
    alg->pageDef = {
        .name = "Mix",
        .numParams = kNumMixParams,
        .group = 1,
        .unused = {0, 0},
        .params = alg->pageIndices
    };
    alg->pagesDef.numPages = 1;
    alg->pagesDef.pages = &alg->pageDef;
    alg->parameters = alg->paramDefs;
    alg->parameterPages = &alg->pagesDef;

    return static_cast<_NT_algorithm*>(alg);
}

static void mixParameterChanged(_NT_algorithm* self, int p)
{
    NtSeqMix* alg = static_cast<NtSeqMix*>(self);

    if (p == kMixParamScaleFile)
        alg->scale.requestScale(alg->v[kMixParamScaleFile]);
}

static void mixStep(_NT_algorithm* self, float* busFrames, int numFramesBy4)
{
    NtSeqMix* alg = static_cast<NtSeqMix*>(self);
    int numFrames = numFramesBy4 * 4;

    if (alg->scale.poll(self, alg->paramDefs[kMixParamScaleFile], kMixParamScaleFile))
        alg->cacheValid = false;

    int inBus = alg->v[kMixParamPitchIn];
    int outBus = alg->v[kMixParamPitchOut];
    if (inBus <= 0 || outBus <= 0)
        return;

    const float* in = busFrames + (inBus - 1) * numFrames;
    float* out = busFrames + (outBus - 1) * numFrames;
    bool replace = alg->v[kMixParamPitchOutMode] != 0;

    MixQuantizer::Mode mode = alg->v[kMixParamMode] == kMixAverage
        ? MixQuantizer::kAverage
        : MixQuantizer::kSum;
    int sources = alg->v[kMixParamSources];
    bool scaleOn = alg->v[kMixParamScaleOn] != 0;
    const ScaleQuantizer* scale = scaleOn && alg->scale.quantizer.isLoaded()
        ? &alg->scale.quantizer
        : nullptr;
    int root = alg->v[kMixParamRootNote];

    if (mode != alg->lastMode || sources != alg->lastSources
        || root != alg->lastRoot || scale != alg->lastScale) {
        alg->lastMode = mode;
        alg->lastSources = sources;
        alg->lastRoot = root;
        alg->lastScale = scale;
        alg->cacheValid = false;
    }

    for (int frame = 0; frame < numFrames; ++frame) {
        float input = in[frame];
        if (!alg->cacheValid || input != alg->lastInput) {
            alg->lastInput = input;
            alg->lastOutput = alg->mixer.process(input, mode, sources, scale, root);
            alg->cacheValid = true;
        }
        if (replace)
            out[frame] = alg->lastOutput;
        else
            out[frame] += alg->lastOutput;
    }
}

static int mixParameterString(_NT_algorithm* self, int p, int v, char* buff)
{
    (void)self;

    if (p == kMixParamScaleFile)
        return ScaleLoader::parameterString(v, buff);
    if (p == kMixParamRootNote)
        return rootNoteParameterString(v, buff);
    return 0;
}

const _NT_factory seqMixFactory = {
    .guid = NT_MULTICHAR('N', 's', 'M', 'x'),
    .name = "Seq Mix",
    .description = "Sum or average sequencer pitch busses, then quantize",
    .numSpecifications = 0,
    .specifications = nullptr,
    .calculateStaticRequirements = nullptr,
    .initialise = nullptr,
    .calculateRequirements = mixCalculateRequirements,
    .construct = mixConstruct,
    .parameterChanged = mixParameterChanged,
    .step = mixStep,
    .draw = nullptr,
    .midiRealtime = nullptr,
    .midiMessage = nullptr,
    .tags = kNT_tagUtility,
    .hasCustomUi = nullptr,
    .customUi = nullptr,
    .setupUi = nullptr,
    .serialise = nullptr,
    .deserialise = nullptr,
    .midiSysEx = nullptr,
    .parameterUiPrefix = nullptr,
    .parameterString = mixParameterString,
};
