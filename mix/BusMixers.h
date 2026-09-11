#ifndef BUS_MIXERS_H
#define BUS_MIXERS_H

// Pure combiners for the per-channel gate and velocity values Seq Mix reads
// from separate busses.

struct GateMixer {
    enum Op { kOr = 0, kAnd, kXor, kNumOps };

    static constexpr float kGateVolts = 5.0f;
    static constexpr float kHighVolts = 1.0f;   // a channel is high above this

    // gates:    one voltage per channel
    // channels: number of entries in gates (>= 0)
    // Returns kGateVolts when the boolean op is satisfied, else 0.
    static float process(const float* gates, int channels, Op op);
};

struct VelocityMixer {
    enum Mode { kSum = 0, kAverage, kScale, kNumModes };

    static constexpr float kMaxVolts = 10.0f;

    // velocities:   one voltage per channel (sequencers emit 0..5 V)
    // channels:     number of entries (>= 0)
    // scalePercent: gain applied to the sum by kScale (100 = unity)
    // Output is clamped to 0..kMaxVolts.
    static float process(const float* velocities, int channels, Mode mode, int scalePercent);
};

#endif // BUS_MIXERS_H
