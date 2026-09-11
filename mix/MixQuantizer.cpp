#include "MixQuantizer.h"

static const float kMaxVolts = 10.0f;

float MixQuantizer::process(float input, Mode mode, int sources, const ScaleQuantizer* scale, int root) const
{
    float pitch = input;
    if (mode == kAverage && sources > 1)
        pitch /= static_cast<float>(sources);

    // Keep the pitch within the Eurorack range so scale lookup always terminates.
    if (pitch > kMaxVolts)
        pitch = kMaxVolts;
    else if (pitch < -kMaxVolts)
        pitch = -kMaxVolts;

    if (!scale || !scale->isLoaded())
        return pitch;

    float rootOffset = static_cast<float>(root) / 12.0f;
    int octave;
    int degree = scale->findNearestDegree(pitch - rootOffset, octave);
    return scale->quantize(degree, octave, 0) + rootOffset;
}
