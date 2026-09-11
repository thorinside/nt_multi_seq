#include "mix/BusMixers.h"

float GateMixer::process(const float* gates, int channels, Op op)
{
    int high = 0;
    for (int i = 0; i < channels; ++i)
        if (gates[i] > kHighVolts)
            ++high;

    bool on;
    switch (op) {
    case kAnd: on = channels > 0 && high == channels; break;
    case kXor: on = (high & 1) != 0;                  break;
    case kOr:
    default:   on = high >= 1;                        break;
    }
    return on ? kGateVolts : 0.0f;
}

float VelocityMixer::process(const float* velocities, int channels, Mode mode, int scalePercent)
{
    float sum = 0.0f;
    for (int i = 0; i < channels; ++i)
        sum += velocities[i];

    float out = sum;
    if (mode == kAverage)
        out = channels > 0 ? sum / static_cast<float>(channels) : 0.0f;
    else if (mode == kScale)
        out = sum * static_cast<float>(scalePercent) / 100.0f;

    if (out < 0.0f)
        out = 0.0f;
    if (out > kMaxVolts)
        out = kMaxVolts;
    return out;
}
