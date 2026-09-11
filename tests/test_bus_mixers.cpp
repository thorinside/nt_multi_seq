// Unit tests for GateMixer and VelocityMixer: combine summed gate and
// velocity busses from several sequencers.

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
    // --- Gate: bus carries 5 V per high gate ---
    ASSERT_NEAR(GateMixer::process(0.0f, GateMixer::kOr, 2), 0.0f, 1e-6, "OR: no gates high -> 0 V");
    ASSERT_NEAR(GateMixer::process(5.0f, GateMixer::kOr, 2), 5.0f, 1e-6, "OR: one of two high -> 5 V");
    ASSERT_NEAR(GateMixer::process(10.0f, GateMixer::kOr, 2), 5.0f, 1e-6, "OR: both high -> 5 V, not 10 V");

    ASSERT_NEAR(GateMixer::process(5.0f, GateMixer::kAnd, 2), 0.0f, 1e-6, "AND: one of two high -> 0 V");
    ASSERT_NEAR(GateMixer::process(10.0f, GateMixer::kAnd, 2), 5.0f, 1e-6, "AND: both high -> 5 V");
    ASSERT_NEAR(GateMixer::process(15.0f, GateMixer::kAnd, 3), 5.0f, 1e-6, "AND: all three high -> 5 V");
    ASSERT_NEAR(GateMixer::process(10.0f, GateMixer::kAnd, 3), 0.0f, 1e-6, "AND: two of three high -> 0 V");

    ASSERT_NEAR(GateMixer::process(5.0f, GateMixer::kXor, 2), 5.0f, 1e-6, "XOR: exactly one high -> 5 V");
    ASSERT_NEAR(GateMixer::process(10.0f, GateMixer::kXor, 2), 0.0f, 1e-6, "XOR: both high -> 0 V");
    ASSERT_NEAR(GateMixer::process(15.0f, GateMixer::kXor, 3), 5.0f, 1e-6, "XOR: odd count (three) high -> 5 V");

    // Gates are not always exactly 5 V; count is rounded to the nearest 5 V step.
    ASSERT_NEAR(GateMixer::process(4.6f, GateMixer::kAnd, 1), 5.0f, 1e-6, "slightly low gate still counts as high");
    ASSERT_NEAR(GateMixer::process(1.0f, GateMixer::kOr, 2), 0.0f, 1e-6, "sub-threshold voltage is not a gate");
    ASSERT_NEAR(GateMixer::process(-5.0f, GateMixer::kOr, 2), 0.0f, 1e-6, "negative voltage is not a gate");
    ASSERT_NEAR(GateMixer::process(100.0f, GateMixer::kAnd, 2), 5.0f, 1e-6, "absurd voltage is clamped, still satisfies AND");

    // --- Velocity: sequencers emit 0..5 V ---
    ASSERT_NEAR(VelocityMixer::process(6.0f, VelocityMixer::kSum, 2, 100), 6.0f, 1e-6, "sum passes the bus through");
    ASSERT_NEAR(VelocityMixer::process(6.0f, VelocityMixer::kAverage, 2, 100), 3.0f, 1e-6, "average divides by sources");
    ASSERT_NEAR(VelocityMixer::process(6.0f, VelocityMixer::kAverage, 3, 100), 2.0f, 1e-6, "average divides by three");
    ASSERT_NEAR(VelocityMixer::process(6.0f, VelocityMixer::kAverage, 0, 100), 6.0f, 1e-6, "average with zero sources does not divide");
    ASSERT_NEAR(VelocityMixer::process(6.0f, VelocityMixer::kScale, 2, 50), 3.0f, 1e-6, "scale applies the percent");
    ASSERT_NEAR(VelocityMixer::process(2.0f, VelocityMixer::kScale, 2, 150), 3.0f, 1e-6, "scale can boost above the sum");
    ASSERT_NEAR(VelocityMixer::process(6.0f, VelocityMixer::kSum, 2, 50), 6.0f, 1e-6, "scale percent is ignored in sum mode");
    ASSERT_NEAR(VelocityMixer::process(9.0f, VelocityMixer::kScale, 1, 200), 10.0f, 1e-6, "output is clamped to 10 V");
    ASSERT_NEAR(VelocityMixer::process(-3.0f, VelocityMixer::kSum, 1, 100), 0.0f, 1e-6, "output is clamped to 0 V");

    printf("%d tests, %d failures\n", tests, failures);
    return failures > 0 ? 1 : 0;
}
