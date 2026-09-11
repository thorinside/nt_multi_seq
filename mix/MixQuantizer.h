#ifndef MIX_QUANTIZER_H
#define MIX_QUANTIZER_H

#include "scale/ScaleQuantizer.h"

// Combines a summed pitch bus from several sequencers into one pitch,
// optionally averaging over the number of sources, then quantizes to a scale.
class MixQuantizer {
public:
    enum Mode { kSum = 0, kAverage };

    // input:   V/oct voltage read from the shared bus (already summed by Add-mode writers)
    // mode:    kSum passes the sum through, kAverage divides by sources
    // sources: number of sequencers feeding the bus (>= 1)
    // scale:   nullptr to bypass quantization
    // root:    root note semitone offset (0-11)
    float process(float input, Mode mode, int sources, const ScaleQuantizer* scale, int root) const;
};

#endif // MIX_QUANTIZER_H
