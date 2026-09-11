#ifndef BUS_MIXERS_H
#define BUS_MIXERS_H

// Pure combiners for gate and velocity busses shared by several sequencers.
// Each sequencer writes in Add mode, so the bus carries the sum of their
// outputs; these turn that sum back into one gate or one velocity.

struct GateMixer {
    enum Op { kOr = 0, kAnd, kXor, kNumOps };

    static constexpr float kGateVolts = 5.0f;

    // sum:     bus voltage, ~5 V per sequencer whose gate is high
    // sources: number of sequencers feeding the bus (>= 1); used by AND
    // Returns kGateVolts when the boolean op is satisfied, else 0.
    static float process(float sum, Op op, int sources);
};

struct VelocityMixer {
    enum Mode { kSum = 0, kAverage, kScale, kNumModes };

    static constexpr float kMaxVolts = 10.0f;

    // sum:          bus voltage (sequencers emit 0..5 V each)
    // sources:      number of sequencers feeding the bus; used by kAverage
    // scalePercent: gain applied by kScale (100 = unity)
    // Output is clamped to 0..kMaxVolts.
    static float process(float sum, Mode mode, int sources, int scalePercent);
};

#endif // BUS_MIXERS_H
