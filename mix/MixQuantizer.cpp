#include "MixQuantizer.h"

float MixQuantizer::process(float input, Mode mode, int sources, const ScaleQuantizer* scale, int root) const
{
    float pitch = input;
    if (mode == kAverage && sources > 1)
        pitch /= static_cast<float>(sources);

    if (!scale || !scale->isLoaded())
        return pitch;

    float rootOffset = static_cast<float>(root) / 12.0f;
    int octave;
    int degree = scale->findNearestDegree(pitch - rootOffset, octave);
    return scale->quantize(degree, octave, 0) + rootOffset;
}
