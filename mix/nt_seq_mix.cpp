#include "mix/nt_seq_mix.h"
#include "common/ParamStrings.h"
#include <new>
#include <string.h>

static const char* const mixModeStrings[] = { "Sum", "Average", nullptr };
static const char* const mixGateOpStrings[] = { "OR", "AND", "XOR", nullptr };
static const char* const mixVelModeStrings[] = { "Sum", "Average", "Scale", nullptr };

static const _NT_specification mixSpecifications[] = {
    { .name = "Channels", .min = 1, .max = kMaxMixChannels, .def = 2, .type = kNT_typeGeneric },
};

// Shared parameters, in .rodata; copied to the instance so Scale File's range can be updated.
static const _NT_parameter mixSharedParams[] = {
    NT_PARAMETER_CV_OUTPUT("Pitch Out", 0, 15)
    { .name = "Pitch Out mode", .min = 0, .max = 1, .def = 1, .unit = kNT_unitOutputMode, .scaling = 0, .enumStrings = nullptr },
    { .name = "Mix", .min = 0, .max = kNumMixModes - 1, .def = kMixSum, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = mixModeStrings },
    { .name = "Scale On", .min = 0, .max = 1, .def = 1, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = kOffOnStrings },
    { .name = "Root Note", .min = 0, .max = 11, .def = 0, .unit = kNT_unitHasStrings, .scaling = 0, .enumStrings = nullptr },
    { .name = "Scale File", .min = 0, .max = 32767, .def = 0, .unit = kNT_unitConfirm, .scaling = 0, .enumStrings = nullptr },
    { .name = "S&H", .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = kOffOnStrings },
    NT_PARAMETER_CV_OUTPUT("Gate Out", 0, 14)
    { .name = "Gate Out mode", .min = 0, .max = 1, .def = 1, .unit = kNT_unitOutputMode, .scaling = 0, .enumStrings = nullptr },
    { .name = "Gate Op", .min = 0, .max = kNumMixGateOps - 1, .def = kMixGateOr, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = mixGateOpStrings },
    NT_PARAMETER_CV_OUTPUT("Velocity Out", 0, 16)
    { .name = "Velocity Out mode", .min = 0, .max = 1, .def = 1, .unit = kNT_unitOutputMode, .scaling = 0, .enumStrings = nullptr },
    { .name = "Vel Mix", .min = 0, .max = kNumMixVelModes - 1, .def = kMixVelSum, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = mixVelModeStrings },
    { .name = "Vel Scale", .min = 0, .max = 200, .def = 100, .unit = kNT_unitPercent, .scaling = 0, .enumStrings = nullptr },
};
static_assert(ARRAY_SIZE(mixSharedParams) == kNumMixSharedParams, "Mix shared param count mismatch");

// Per-channel input template. Name suffix and default bus are filled in per channel.
static const char* const mixChannelSuffix[kNumMixChannelParams] = { " Gate In", " Pitch In", " Vel In" };
static const int mixChannelFirstBus[kNumMixChannelParams] = { 14, 15, 16 };

static const uint8_t mixPitchPage[] = {
    kMixParamPitchOut, kMixParamPitchOutMode, kMixParamMode,
    kMixParamScaleOn, kMixParamRootNote, kMixParamScaleFile, kMixParamSampleHold,
};
static const uint8_t mixGatePage[] = {
    kMixParamGateOut, kMixParamGateOutMode, kMixParamGateOp,
};
static const uint8_t mixVelocityPage[] = {
    kMixParamVelOut, kMixParamVelOutMode, kMixParamVelMode, kMixParamVelScale,
};
static const _NT_parameterPage mixSharedPages[kNumMixSharedPages] = {
    { .name = "Pitch",    .numParams = ARRAY_SIZE(mixPitchPage),    .group = 1, .unused = {0, 0}, .params = mixPitchPage },
    { .name = "Gate",     .numParams = ARRAY_SIZE(mixGatePage),     .group = 1, .unused = {0, 0}, .params = mixGatePage },
    { .name = "Velocity", .numParams = ARRAY_SIZE(mixVelocityPage), .group = 1, .unused = {0, 0}, .params = mixVelocityPage },
};
static_assert(ARRAY_SIZE(mixPitchPage) + ARRAY_SIZE(mixGatePage) + ARRAY_SIZE(mixVelocityPage) == kNumMixSharedParams,
    "Mix shared pages must cover every shared parameter");

static int mixChannelsFromSpec(const int32_t* specifications)
{
    int channels = specifications ? static_cast<int>(specifications[0]) : mixSpecifications[0].def;
    if (channels < 1)
        channels = 1;
    if (channels > kMaxMixChannels)
        channels = kMaxMixChannels;
    return channels;
}

static void mixCalculateRequirements(_NT_algorithmRequirements& req, const int32_t* specifications)
{
    int channels = mixChannelsFromSpec(specifications);
    req.numParameters = kNumMixSharedParams + kNumMixChannelParams * channels;
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

    NtSeqMix* alg = new (ptrs.sram) NtSeqMix();
    alg->channels = mixChannelsFromSpec(specifications);
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

    memcpy(alg->paramDefs, mixSharedParams, sizeof(mixSharedParams));
    memcpy(alg->pageDefs, mixSharedPages, sizeof(mixSharedPages));

    for (int ch = 0; ch < alg->channels; ++ch) {
        char* pageName = alg->channelPageNames[ch];
        strcpy(pageName, "Ch ");
        NT_intToString(pageName + 3, ch + 1);

        for (int which = 0; which < kNumMixChannelParams; ++which) {
            int index = mixChannelParam(ch, which);
            char* name = alg->channelParamNames[ch][which];
            strcpy(name, pageName);
            strcat(name, mixChannelSuffix[which]);

            int bus = mixChannelFirstBus[which] + ch * kNumMixChannelParams;
            if (bus > kNT_lastBus)
                bus = 0;
            alg->paramDefs[index] = {
                .name = name, .min = 0, .max = kNT_lastBus, .def = static_cast<int16_t>(bus),
                .unit = kNT_unitCvInput, .scaling = 0, .enumStrings = nullptr
            };
            alg->channelPageIndices[ch][which] = static_cast<uint8_t>(index);
        }

        alg->pageDefs[kNumMixSharedPages + ch] = {
            .name = pageName,
            .numParams = kNumMixChannelParams,
            .group = 2,
            .unused = {0, 0},
            .params = alg->channelPageIndices[ch]
        };
    }

    alg->pagesDef.numPages = static_cast<uint32_t>(kNumMixSharedPages + alg->channels);
    alg->pagesDef.pages = alg->pageDefs;
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
    return bus > 0 && bus <= kNT_lastBus ? busFrames + (bus - 1) * numFrames : nullptr;
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

    // Resolve the connected input busses once per block.
    const float* gateIn[kMaxMixChannels];
    const float* pitchIn[kMaxMixChannels];
    const float* velIn[kMaxMixChannels];
    int numGates = 0;
    int numPitches = 0;
    int numVels = 0;
    for (int ch = 0; ch < alg->channels; ++ch) {
        const float* g = mixBus(busFrames, alg->v[mixChannelParam(ch, kMixChanGateIn)], numFrames);
        const float* p = mixBus(busFrames, alg->v[mixChannelParam(ch, kMixChanPitchIn)], numFrames);
        const float* v = mixBus(busFrames, alg->v[mixChannelParam(ch, kMixChanVelIn)], numFrames);
        if (g) gateIn[numGates++] = g;
        if (p) pitchIn[numPitches++] = p;
        if (v) velIn[numVels++] = v;
    }

    // Pitch stage settings
    float* pitchOut = mixBus(busFrames, alg->v[kMixParamPitchOut], numFrames);
    bool pitchReplace = alg->v[kMixParamPitchOutMode] != 0;
    MixQuantizer::Mode mode = alg->v[kMixParamMode] == kMixAverage
        ? MixQuantizer::kAverage
        : MixQuantizer::kSum;
    bool scaleOn = alg->v[kMixParamScaleOn] != 0;
    const ScaleQuantizer* scale = scaleOn && alg->scale.quantizer.isLoaded()
        ? &alg->scale.quantizer
        : nullptr;
    int root = alg->v[kMixParamRootNote];

    if (mode != alg->lastMode || numPitches != alg->lastSources
        || root != alg->lastRoot || scale != alg->lastScale) {
        alg->lastMode = mode;
        alg->lastSources = numPitches;
        alg->lastRoot = root;
        alg->lastScale = scale;
        alg->cacheValid = false;
    }

    // Gate stage settings
    float* gateOut = mixBus(busFrames, alg->v[kMixParamGateOut], numFrames);
    bool gateReplace = alg->v[kMixParamGateOutMode] != 0;
    int gateOpValue = alg->v[kMixParamGateOp];
    if (gateOpValue < 0 || gateOpValue >= kNumMixGateOps)
        gateOpValue = kMixGateOr;
    GateMixer::Op gateOp = static_cast<GateMixer::Op>(gateOpValue);

    // Velocity stage settings
    float* velOut = mixBus(busFrames, alg->v[kMixParamVelOut], numFrames);
    bool velReplace = alg->v[kMixParamVelOutMode] != 0;
    int velModeValue = alg->v[kMixParamVelMode];
    if (velModeValue < 0 || velModeValue >= kNumMixVelModes)
        velModeValue = kMixVelSum;
    VelocityMixer::Mode velMode = static_cast<VelocityMixer::Mode>(velModeValue);
    int velScale = alg->v[kMixParamVelScale];

    // Sample and hold is clocked by the combined gate; with no gate inputs it is bypassed.
    bool sampleHold = alg->v[kMixParamSampleHold] != 0 && numGates > 0;
    bool doPitch = pitchOut && numPitches > 0;
    bool doVel = velOut && numVels > 0;

    float gates[kMaxMixChannels];
    float vels[kMaxMixChannels];

    for (int frame = 0; frame < numFrames; ++frame) {
        bool sampleNow = true;
        if (numGates > 0) {
            for (int i = 0; i < numGates; ++i)
                gates[i] = gateIn[i][frame];
            float gate = GateMixer::process(gates, numGates, gateOp);
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
                float sum = 0.0f;
                for (int i = 0; i < numPitches; ++i)
                    sum += pitchIn[i][frame];
                if (!alg->cacheValid || sum != alg->lastInput) {
                    alg->lastInput = sum;
                    alg->lastOutput = alg->mixer.process(sum, mode, numPitches, scale, root);
                    alg->cacheValid = true;
                }
                alg->heldPitch = alg->lastOutput;
            }
            mixWrite(pitchOut, frame, pitchReplace, alg->heldPitch);
        }

        if (doVel) {
            if (sampleNow) {
                for (int i = 0; i < numVels; ++i)
                    vels[i] = velIn[i][frame];
                alg->heldVelocity = VelocityMixer::process(vels, numVels, velMode, velScale);
            }
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
    .numSpecifications = ARRAY_SIZE(mixSpecifications),
    .specifications = mixSpecifications,
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
