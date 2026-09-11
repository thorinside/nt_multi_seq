#include "mix/BusMixers.h"

float GateMixer::process(float sum, Op op, int sources)
{
    if (sum < 0.0f)
        sum = 0.0f;
    if (sum > 16.0f * kGateVolts)
        sum = 16.0f * kGateVolts;
    int high = static_cast<int>(sum / kGateVolts + 0.5f);
    if (sources < 1)
        sources = 1;

    bool on;
    switch (op) {
    case kAnd: on = high >= sources; break;
    case kXor: on = (high & 1) != 0; break;
    case kOr:
    default:   on = high >= 1;       break;
    }
    return on ? kGateVolts : 0.0f;
}

float VelocityMixer::process(float sum, Mode mode, int sources, int scalePercent)
{
    float out = sum;
    if (mode == kAverage && sources > 1)
        out = sum / static_cast<float>(sources);
    else if (mode == kScale)
        out = sum * static_cast<float>(scalePercent) / 100.0f;

    if (out < 0.0f)
        out = 0.0f;
    if (out > kMaxVolts)
        out = kMaxVolts;
    return out;
}
