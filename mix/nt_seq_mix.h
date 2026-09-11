#ifndef NT_SEQ_MIX_H
#define NT_SEQ_MIX_H

#include <distingnt/api.h>
#include "mix/MixQuantizer.h"
#include "mix/BusMixers.h"
#include "scale/ScaleLoader.h"

// Parameter indices for Seq Mix. Released indices are fixed; append only.
enum MixParam {
    kMixParamPitchIn = 0,
    kMixParamPitchOut,
    kMixParamPitchOutMode,
    kMixParamMode,
    kMixParamSources,
    kMixParamScaleOn,
    kMixParamRootNote,
    kMixParamScaleFile,
    // v1.3.0
    kMixParamGateIn,
    kMixParamGateOut,
    kMixParamGateOutMode,
    kMixParamGateOp,
    kMixParamVelIn,
    kMixParamVelOut,
    kMixParamVelOutMode,
    kMixParamVelMode,
    kMixParamVelScale,
    kMixParamSampleHold,
    kNumMixParams
};

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
    kNumMixPages
};

struct NtSeqMix : public _NT_algorithm {
    NtSeqMix() {}
    ~NtSeqMix() {}

    _NT_parameter paramDefs[kNumMixParams];
    _NT_parameterPages pagesDef;
    _NT_parameterPage pageDefs[kNumMixPages];
    uint8_t pageIndices[kNumMixParams];

    MixQuantizer mixer;
    ScaleLoader scale;

    // Cache: quantization only reruns when the input voltage or settings change.
    float lastInput;
    float lastOutput;
    int lastMode;
    int lastSources;
    int lastRoot;
    const ScaleQuantizer* lastScale;
    bool cacheValid;

    // Sample and hold of pitch and velocity, clocked by the mixed gate.
    float heldPitch;
    float heldVelocity;
    bool gateHigh;
};

extern const _NT_factory seqMixFactory;

#endif // NT_SEQ_MIX_H
