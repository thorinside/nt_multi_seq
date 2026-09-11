#ifndef NT_SEQ_MIX_H
#define NT_SEQ_MIX_H

#include <cstddef>
#include <distingnt/api.h>
#include <distingnt/microtuning.h>
#include <distingnt/wav.h>
#include "mix/MixQuantizer.h"
#include "scale/ScaleQuantizer.h"

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

constexpr int kMixMaxSclNotes = 128;

struct NtSeqMix : public _NT_algorithm {
    NtSeqMix() {}
    ~NtSeqMix() {}

    _NT_parameter paramDefs[kNumMixParams];
    _NT_parameterPages pagesDef;
    _NT_parameterPage pageDef;
    uint8_t pageIndices[kNumMixParams];

    MixQuantizer mixer;
    ScaleQuantizer scaleQuantizer;
    _NT_sclRequest sclRequest;
    _NT_sclNote sclNotes[kMixMaxSclNotes];
    char sclName[22];
    char sclDescription[44];
    bool cardMounted;
    bool awaitingCallback;
    bool scaleDirty;

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
