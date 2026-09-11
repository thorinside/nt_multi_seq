#ifndef NT_SEQ_MIX_H
#define NT_SEQ_MIX_H

#include <distingnt/api.h>
#include "mix/MixQuantizer.h"
#include "mix/BusMixers.h"
#include "scale/ScaleLoader.h"

// Seq Mix takes a "Channels" specification (1..kMaxMixChannels). Each channel
// has its own Gate In, Pitch In, and Velocity In; the shared parameters come
// first so their indices do not move with the channel count.
static constexpr int kMaxMixChannels = 8;

enum MixParam {
    kMixParamPitchOut = 0,
    kMixParamPitchOutMode,
    kMixParamMode,
    kMixParamScaleOn,
    kMixParamRootNote,
    kMixParamScaleFile,
    kMixParamSampleHold,
    kMixParamGateOut,
    kMixParamGateOutMode,
    kMixParamGateOp,
    kMixParamVelOut,
    kMixParamVelOutMode,
    kMixParamVelMode,
    kMixParamVelScale,
    kNumMixSharedParams
};

enum MixChannelParam {
    kMixChanGateIn = 0,
    kMixChanPitchIn,
    kMixChanVelIn,
    kNumMixChannelParams
};

static constexpr int kMaxMixParams = kNumMixSharedParams + kNumMixChannelParams * kMaxMixChannels;

inline int mixChannelParam(int channel, int which)
{
    return kNumMixSharedParams + channel * kNumMixChannelParams + which;
}

enum MixMode {
    kMixSum = 0,
    kMixAverage,
    kNumMixModes
};

enum MixGateOp {
    kMixGateOr = GateMixer::kOr,
    kMixGateAnd = GateMixer::kAnd,
    kMixGateXor = GateMixer::kXor,
    kNumMixGateOps = GateMixer::kNumOps
};

enum MixVelMode {
    kMixVelSum = VelocityMixer::kSum,
    kMixVelAverage = VelocityMixer::kAverage,
    kMixVelScale = VelocityMixer::kScale,
    kNumMixVelModes = VelocityMixer::kNumModes
};

enum MixPage {
    kMixPagePitch = 0,
    kMixPageGate,
    kMixPageVelocity,
    kNumMixSharedPages
};

struct NtSeqMix : public _NT_algorithm {
    NtSeqMix() {}
    ~NtSeqMix() {}

    int channels;

    _NT_parameter paramDefs[kMaxMixParams];
    char channelParamNames[kMaxMixChannels][kNumMixChannelParams][16];
    char channelPageNames[kMaxMixChannels][6];
    _NT_parameterPages pagesDef;
    _NT_parameterPage pageDefs[kNumMixSharedPages + kMaxMixChannels];
    uint8_t channelPageIndices[kMaxMixChannels][kNumMixChannelParams];

    MixQuantizer mixer;
    ScaleLoader scale;

    // Cache: quantization only reruns when the summed pitch or settings change.
    float lastInput;
    float lastOutput;
    int lastMode;
    int lastSources;
    int lastRoot;
    const ScaleQuantizer* lastScale;
    bool cacheValid;

    // Sample and hold of pitch and velocity, clocked by the combined gate.
    float heldPitch;
    float heldVelocity;
    bool gateHigh;
};

extern const _NT_factory seqMixFactory;

#endif // NT_SEQ_MIX_H
