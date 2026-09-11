#ifndef SCALE_LOADER_H
#define SCALE_LOADER_H

#include <cstddef>
#include <distingnt/api.h>
#include <distingnt/microtuning.h>
#include <distingnt/wav.h>
#include "scale/ScaleQuantizer.h"

// Owns the .scl request lifecycle shared by every algorithm in this plugin:
// SD-card mount tracking, asynchronous NT_readScl requests, and loading the
// received notes into a ScaleQuantizer.
struct ScaleLoader {
    static constexpr int kMaxNotes = 128;

    ScaleQuantizer quantizer;
    _NT_sclRequest request;
    _NT_sclNote notes[kMaxNotes];
    char name[22];
    char description[44];
    bool cardMounted;
    bool awaitingCallback;
    bool dirty;

    // Must be called after placement-new, once the object has its final address.
    void init();

    // Call from parameterChanged when the Scale File parameter changes.
    void requestScale(int index);

    // Call at the top of step(). Handles card mount/unmount, refreshes the
    // Scale File parameter's range, and loads a newly received scale.
    // Returns true when the quantizer was reloaded this call.
    bool poll(_NT_algorithm* self, _NT_parameter& scaleFileParam, int scaleFileParamIndex);
};

#endif // SCALE_LOADER_H
