#ifndef NT_SEQ_MIX_H
#define NT_SEQ_MIX_H

#include <distingnt/api.h>
#include "mix/MixQuantizer.h"
#include "scale/ScaleLoader.h"

// Parameter indices for Seq Mix
enum MixParam {
    kMixParamPitchIn = 0,
    kMixParamPitchOut,
    kMixParamPitchOutMode,
    kMixParamMode,
    kMixParamSources,
    kMixParamScaleOn,
    kMixParamRootNote,
    kMixParamScaleFile,
    kNumMixParams
};

enum MixMode {
    kMixSum = 0,
    kMixAverage,
    kNumMixModes
};

struct NtSeqMix : public _NT_algorithm {
    NtSeqMix() {}
    ~NtSeqMix() {}

    _NT_parameter paramDefs[kNumMixParams];
    _NT_parameterPages pagesDef;
    _NT_parameterPage pageDef;
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
};

extern const _NT_factory seqMixFactory;

#endif // NT_SEQ_MIX_H
