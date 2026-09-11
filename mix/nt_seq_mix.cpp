#include "mix/nt_seq_mix.h"
#include "common/ParamStrings.h"
#include <new>
#include <string.h>

static const char* const mixModeStrings[] = { "Sum", "Average", nullptr };
static const char* const mixGateOpStrings[] = { "OR", "AND", "XOR", nullptr };
static const char* const mixVelModeStrings[] = { "Sum", "Average", "Scale", nullptr };

static const _NT_parameter mixParams[] = {
    NT_PARAMETER_CV_INPUT("Pitch In", 0, 15)
    NT_PARAMETER_CV_OUTPUT("Pitch Out", 0, 15)
    { .name = "Pitch Out mode", .min = 0, .max = 1, .def = 1, .unit = kNT_unitOutputMode, .scaling = 0, .enumStrings = nullptr },
    { .name = "Mix", .min = 0, .max = kNumMixModes - 1, .def = kMixSum, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = mixModeStrings },
    { .name = "Sources", .min = 1, .max = 8, .def = 2, .unit = kNT_unitNone, .scaling = 0, .enumStrings = nullptr },
    { .name = "Scale On", .min = 0, .max = 1, .def = 1, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = kOffOnStrings },
    { .name = "Root Note", .min = 0, .max = 11, .def = 0, .unit = kNT_unitHasStrings, .scaling = 0, .enumStrings = nullptr },
    { .name = "Scale File", .min = 0, .max = 32767, .def = 0, .unit = kNT_unitConfirm, .scaling = 0, .enumStrings = nullptr },
    // v1.3.0 additions (appended to keep released indices stable)
    NT_PARAMETER_CV_INPUT("Gate In", 0, 14)
    NT_PARAMETER_CV_OUTPUT("Gate Out", 0, 14)
    { .name = "Gate Out mode", .min = 0, .max = 1, .def = 1, .unit = kNT_unitOutputMode, .scaling = 0, .enumStrings = nullptr },
    { .name = "Gate Op", .min = 0, .max = kNumMixGateOps - 1, .def = kMixGateOr, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = mixGateOpStrings },
    NT_PARAMETER_CV_INPUT("Velocity In", 0, 16)
    NT_PARAMETER_CV_OUTPUT("Velocity Out", 0, 16)
    { .name = "Velocity Out mode", .min = 0, .max = 1, .def = 1, .unit = kNT_unitOutputMode, .scaling = 0, .enumStrings = nullptr },
    { .name = "Vel Mix", .min = 0, .max = kNumMixVelModes - 1, .def = kMixVelSum, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = mixVelModeStrings },
    { .name = "Vel Scale", .min = 0, .max = 200, .def = 100, .unit = kNT_unitPercent, .scaling = 0, .enumStrings = nullptr },
    { .name = "S&H", .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = kOffOnStrings },
};
static_assert(ARRAY_SIZE(mixParams) == kNumMixParams, "Mix param count mismatch");

// Page layout, in .rodata. The first page is the pre-1.3.0 "Mix" page.
static const uint8_t mixPitchPage[] = {
    kMixParamPitchIn, kMixParamPitchOut, kMixParamPitchOutMode, kMixParamMode,
    kMixParamSources, kMixParamScaleOn, kMixParamRootNote, kMixParamScaleFile, kMixParamSampleHold,
};
static const uint8_t mixGatePage[] = {
    kMixParamGateIn, kMixParamGateOut, kMixParamGateOutMode, kMixParamGateOp,
};
static const uint8_t mixVelocityPage[] = {
    kMixParamVelIn, kMixParamVelOut, kMixParamVelOutMode, kMixParamVelMode, kMixParamVelScale,
};
static const _NT_parameterPage mixPages[kNumMixPages] = {
    { .name = "Pitch",    .numParams = ARRAY_SIZE(mixPitchPage),    .group = 1, .unused = {0, 0}, .params = mixPitchPage },
    { .name = "Gate",     .numParams = ARRAY_SIZE(mixGatePage),     .group = 2, .unused = {0, 0}, .params = mixGatePage },
    { .name = "Velocity", .numParams = ARRAY_SIZE(mixVelocityPage), .group = 3, .unused = {0, 0}, .params = mixVelocityPage },
};
static_assert(ARRAY_SIZE(mixPitchPage) + ARRAY_SIZE(mixGatePage) + ARRAY_SIZE(mixVelocityPage) == kNumMixParams,
    "Mix pages must cover every parameter");

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

    alg->heldPitch = 0.0f;
    alg->heldVelocity = 0.0f;
    alg->gateHigh = false;

    memcpy(alg->paramDefs, mixParams, sizeof(mixParams));
    alg->pagesDef.numPages = kNumMixPages;
    alg->pagesDef.pages = mixPages;
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

static inline float* mixBus(float* busFrames, int bus, int numFrames)
{
    return bus > 0 ? busFrames + (bus - 1) * numFrames : nullptr;
}

static inline void mixWrite(float* out, int frame, bool replace, float value)
{
    if (replace)
        out[frame] = value;
    else
        out[frame] += value;
}

static void mixStep(_NT_algorithm* self, float* busFrames, int numFramesBy4)
{
    NtSeqMix* alg = static_cast<NtSeqMix*>(self);
    int numFrames = numFramesBy4 * 4;

    if (alg->scale.poll(self, alg->paramDefs[kMixParamScaleFile], kMixParamScaleFile))
        alg->cacheValid = false;

    // Pitch stage settings
    const float* pitchIn = mixBus(busFrames, alg->v[kMixParamPitchIn], numFrames);
    float* pitchOut = mixBus(busFrames, alg->v[kMixParamPitchOut], numFrames);
    bool pitchReplace = alg->v[kMixParamPitchOutMode] != 0;
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

    // Gate stage settings
    const float* gateIn = mixBus(busFrames, alg->v[kMixParamGateIn], numFrames);
    float* gateOut = mixBus(busFrames, alg->v[kMixParamGateOut], numFrames);
    bool gateReplace = alg->v[kMixParamGateOutMode] != 0;
    int gateOpValue = alg->v[kMixParamGateOp];
    if (gateOpValue < 0 || gateOpValue >= kNumMixGateOps)
        gateOpValue = kMixGateOr;
    GateMixer::Op gateOp = static_cast<GateMixer::Op>(gateOpValue);

    // Velocity stage settings
    const float* velIn = mixBus(busFrames, alg->v[kMixParamVelIn], numFrames);
    float* velOut = mixBus(busFrames, alg->v[kMixParamVelOut], numFrames);
    bool velReplace = alg->v[kMixParamVelOutMode] != 0;
    int velModeValue = alg->v[kMixParamVelMode];
    if (velModeValue < 0 || velModeValue >= kNumMixVelModes)
        velModeValue = kMixVelSum;
    VelocityMixer::Mode velMode = static_cast<VelocityMixer::Mode>(velModeValue);
    int velScale = alg->v[kMixParamVelScale];

    // Sample and hold is clocked by the mixed gate; without a gate input it is bypassed.
    bool sampleHold = alg->v[kMixParamSampleHold] != 0 && gateIn != nullptr;

    bool doPitch = pitchIn && pitchOut;
    bool doVel = velIn && velOut;

    for (int frame = 0; frame < numFrames; ++frame) {
        bool sampleNow = true;
        if (gateIn) {
            float gate = GateMixer::process(gateIn[frame], gateOp, sources);
            bool high = gate > 0.0f;
            bool rising = high && !alg->gateHigh;
            alg->gateHigh = high;
            if (gateOut)
                mixWrite(gateOut, frame, gateReplace, gate);
            if (sampleHold)
                sampleNow = rising;
        }

        if (doPitch) {
            if (sampleNow) {
                float input = pitchIn[frame];
                if (!alg->cacheValid || input != alg->lastInput) {
                    alg->lastInput = input;
                    alg->lastOutput = alg->mixer.process(input, mode, sources, scale, root);
                    alg->cacheValid = true;
                }
                alg->heldPitch = alg->lastOutput;
            }
            mixWrite(pitchOut, frame, pitchReplace, alg->heldPitch);
        }

        if (doVel) {
            if (sampleNow)
                alg->heldVelocity = VelocityMixer::process(velIn[frame], velMode, sources, velScale);
            mixWrite(velOut, frame, velReplace, alg->heldVelocity);
        }
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
