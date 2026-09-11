// Unit tests for GateMixer and VelocityMixer: combine per-channel gate and
// velocity values read from separate busses.

#include "../mix/BusMixers.h"
#include <stdio.h>
#include <math.h>

static int failures = 0;
static int tests = 0;

#define ASSERT_NEAR(a, b, tol, msg) do { \
    tests++; \
    if (fabs((double)(a) - (double)(b)) > (double)(tol)) { \
        fprintf(stderr, "FAIL: %s: expected %.4f, got %.4f\n", msg, (double)(b), (double)(a)); \
        failures++; \
    } \
} while (0)

int main()
{
    // --- Gate: each channel is a gate voltage; high above 1 V ---
    const float none[] = { 0.0f, 0.0f };
    const float one[] = { 5.0f, 0.0f };
    const float both[] = { 5.0f, 5.0f };
    const float twoOfThree[] = { 5.0f, 0.0f, 5.0f };
    const float allThree[] = { 5.0f, 5.0f, 5.0f };

    ASSERT_NEAR(GateMixer::process(none, 2, GateMixer::kOr), 0.0f, 1e-6, "OR: no gates high -> 0 V");
    ASSERT_NEAR(GateMixer::process(one, 2, GateMixer::kOr), 5.0f, 1e-6, "OR: one of two high -> 5 V");
    ASSERT_NEAR(GateMixer::process(both, 2, GateMixer::kOr), 5.0f, 1e-6, "OR: both high -> 5 V");

    ASSERT_NEAR(GateMixer::process(one, 2, GateMixer::kAnd), 0.0f, 1e-6, "AND: one of two high -> 0 V");
    ASSERT_NEAR(GateMixer::process(both, 2, GateMixer::kAnd), 5.0f, 1e-6, "AND: both high -> 5 V");
    ASSERT_NEAR(GateMixer::process(allThree, 3, GateMixer::kAnd), 5.0f, 1e-6, "AND: all three high -> 5 V");
    ASSERT_NEAR(GateMixer::process(twoOfThree, 3, GateMixer::kAnd), 0.0f, 1e-6, "AND: two of three high -> 0 V");

    ASSERT_NEAR(GateMixer::process(one, 2, GateMixer::kXor), 5.0f, 1e-6, "XOR: exactly one high -> 5 V");
    ASSERT_NEAR(GateMixer::process(both, 2, GateMixer::kXor), 0.0f, 1e-6, "XOR: both high -> 0 V");
    ASSERT_NEAR(GateMixer::process(allThree, 3, GateMixer::kXor), 5.0f, 1e-6, "XOR: odd count (three) high -> 5 V");
    ASSERT_NEAR(GateMixer::process(twoOfThree, 3, GateMixer::kXor), 0.0f, 1e-6, "XOR: even count (two) high -> 0 V");

    const float lowish[] = { 1.5f };
    const float sub[] = { 0.9f };
    const float neg[] = { -5.0f };
    const float huge[] = { 1000.0f };
    ASSERT_NEAR(GateMixer::process(lowish, 1, GateMixer::kAnd), 5.0f, 1e-6, "1.5 V counts as high");
    ASSERT_NEAR(GateMixer::process(sub, 1, GateMixer::kOr), 0.0f, 1e-6, "0.9 V is not a gate");
    ASSERT_NEAR(GateMixer::process(neg, 1, GateMixer::kOr), 0.0f, 1e-6, "negative voltage is not a gate");
    ASSERT_NEAR(GateMixer::process(huge, 1, GateMixer::kAnd), 5.0f, 1e-6, "absurd voltage is just high");
    ASSERT_NEAR(GateMixer::process(none, 0, GateMixer::kAnd), 0.0f, 1e-6, "zero channels: AND is low");

    // --- Velocity: sequencers emit 0..5 V per channel ---
    const float v42[] = { 4.0f, 2.0f };
    const float v123[] = { 1.0f, 2.0f, 3.0f };
    const float v55[] = { 5.0f, 5.0f };
    const float vneg[] = { -3.0f, 1.0f };
    ASSERT_NEAR(VelocityMixer::process(v42, 2, VelocityMixer::kSum, 100), 6.0f, 1e-6, "sum adds the channels");
    ASSERT_NEAR(VelocityMixer::process(v42, 2, VelocityMixer::kAverage, 100), 3.0f, 1e-6, "average divides by channels");
    ASSERT_NEAR(VelocityMixer::process(v123, 3, VelocityMixer::kAverage, 100), 2.0f, 1e-6, "average divides by three");
    ASSERT_NEAR(VelocityMixer::process(v42, 0, VelocityMixer::kAverage, 100), 0.0f, 1e-6, "zero channels average to 0 V");
    ASSERT_NEAR(VelocityMixer::process(v42, 2, VelocityMixer::kScale, 50), 3.0f, 1e-6, "scale applies the percent to the sum");
    ASSERT_NEAR(VelocityMixer::process(v42, 2, VelocityMixer::kScale, 150), 9.0f, 1e-6, "scale can boost above the sum");
    ASSERT_NEAR(VelocityMixer::process(v42, 2, VelocityMixer::kSum, 50), 6.0f, 1e-6, "scale percent is ignored in sum mode");
    ASSERT_NEAR(VelocityMixer::process(v55, 2, VelocityMixer::kScale, 200), 10.0f, 1e-6, "output is clamped to 10 V");
    ASSERT_NEAR(VelocityMixer::process(vneg, 2, VelocityMixer::kSum, 100), 0.0f, 1e-6, "output is clamped to 0 V");

    printf("%d tests, %d failures\n", tests, failures);
    return failures > 0 ? 1 : 0;
}
