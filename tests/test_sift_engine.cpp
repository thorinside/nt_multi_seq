// Unit tests for SiftEngine.
// Standalone compilation with NT API stubs.

#include "nt_stubs.h"

// Expose private/protected members for direct testing
#define private public
#define protected public

#include "../engines/SiftEngine.h"
#include "../engines/SiftEngine.cpp"
#include "../scale/ScaleQuantizer.cpp"

#undef private
#undef protected

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int failures = 0;
static int tests = 0;

#define ASSERT_TRUE(cond, msg) do { \
    tests++; \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s\n", msg); \
        failures++; \
    } \
} while (0)

#define ASSERT_EQ(a, b, msg) do { \
    tests++; \
    if ((a) != (b)) { \
        fprintf(stderr, "FAIL: %s: expected %d, got %d\n", msg, (int)(b), (int)(a)); \
        failures++; \
    } \
} while (0)

#define ASSERT_FLOAT_EQ(a, b, msg) do { \
    tests++; \
    if (fabsf((a) - (b)) > 0.001f) { \
        fprintf(stderr, "FAIL: %s: expected %.4f, got %.4f\n", msg, (double)(b), (double)(a)); \
        failures++; \
    } \
} while (0)

// --- sequenceRaw determinism ---

static void test_sequence_raw_deterministic()
{
    SiftEngine engine;
    engine.init(48000);

    // Same (seed, step) must always return the same value
    uint32_t seed = engine.voltSeqs_[0].seed;
    for (int step = 0; step < 32; step++) {
        int16_t a = engine.sequenceRaw(seed, step);
        int16_t b = engine.sequenceRaw(seed, step);
        ASSERT_EQ(a, b, "sequenceRaw deterministic for same seed+step");
    }
}

static void test_sequence_raw_varies()
{
    SiftEngine engine;
    engine.init(48000);

    uint32_t seed = engine.voltSeqs_[0].seed;

    // Collect values for several steps; they should not all be identical
    int16_t first = engine.sequenceRaw(seed, 0);
    bool allSame = true;
    for (int step = 1; step < 32; step++) {
        int16_t val = engine.sequenceRaw(seed, step);
        if (val != first) {
            allSame = false;
            break;
        }
    }
    ASSERT_TRUE(!allSame, "sequenceRaw should produce varying values across steps");
}

// --- Bit depth quantization ---

static void test_bit_depth_2()
{
    SiftEngine engine;
    engine.init(48000);

    // bitDepth=2 -> levels = (1<<2)-1 = 3, giving 4 quantization points: 0/3, 1/3, 2/3, 3/3
    engine.parameterChanged(SiftEngine::kSiftBitDepth, 2);
    engine.parameterChanged(SiftEngine::kSiftPolarity, SiftEngine::kPolarityPositive);
    engine.parameterChanged(SiftEngine::kSiftMinCv, 0);
    engine.parameterChanged(SiftEngine::kSiftMaxCv, 10); // 1.0V

    float unique[4];
    int numUnique = 0;
    for (int i = 0; i < 32; i++) {
        EngineOutput out = engine.clockTick(nullptr);
        float rounded = roundf(out.pitch * 10000.0f) / 10000.0f;
        bool found = false;
        for (int j = 0; j < numUnique; j++) {
            if (fabsf(unique[j] - rounded) < 0.0001f) { found = true; break; }
        }
        if (!found && numUnique < 4) unique[numUnique++] = rounded;
    }

    // levels=3 means 4 quantization points (indices 0,1,2,3)
    ASSERT_EQ(numUnique, 4,
        "bitDepth=2 should produce exactly 4 distinct pitch levels");
}

// --- Polarity modes ---

static void test_polarity_positive()
{
    SiftEngine engine;
    engine.init(48000);

    engine.parameterChanged(SiftEngine::kSiftPolarity, SiftEngine::kPolarityPositive);
    engine.parameterChanged(SiftEngine::kSiftMinCv, 0);
    engine.parameterChanged(SiftEngine::kSiftMaxCv, 50); // 5.0V

    for (int i = 0; i < 100; i++) {
        EngineOutput out = engine.clockTick(nullptr);
        ASSERT_TRUE(out.pitch >= -0.001f,
            "Positive polarity should produce non-negative pitch");
    }
}

static void test_polarity_negative()
{
    SiftEngine engine;
    engine.init(48000);

    engine.parameterChanged(SiftEngine::kSiftPolarity, SiftEngine::kPolarityNegative);
    engine.parameterChanged(SiftEngine::kSiftMinCv, -50); // -5.0V
    engine.parameterChanged(SiftEngine::kSiftMaxCv, 0);

    for (int i = 0; i < 100; i++) {
        EngineOutput out = engine.clockTick(nullptr);
        ASSERT_TRUE(out.pitch <= 0.001f,
            "Negative polarity should produce non-positive pitch");
    }
}

static void test_polarity_bipolar()
{
    SiftEngine engine;
    engine.init(48000);

    engine.parameterChanged(SiftEngine::kSiftPolarity, SiftEngine::kPolarityBipolar);
    engine.parameterChanged(SiftEngine::kSiftMinCv, -50); // -5.0V
    engine.parameterChanged(SiftEngine::kSiftMaxCv, 50);  // +5.0V

    bool hasPositive = false;
    bool hasNegative = false;
    for (int i = 0; i < 200; i++) {
        EngineOutput out = engine.clockTick(nullptr);
        if (out.pitch > 0.01f) hasPositive = true;
        if (out.pitch < -0.01f) hasNegative = true;
    }
    ASSERT_TRUE(hasPositive, "Bipolar polarity should produce some positive values");
    ASSERT_TRUE(hasNegative, "Bipolar polarity should produce some negative values");
}

// --- Gate threshold ---

static void test_threshold_low()
{
    SiftEngine engine;
    engine.init(48000);

    engine.parameterChanged(SiftEngine::kSiftThreshold, 1); // very low threshold

    int gateCount = 0;
    for (int i = 0; i < 100; i++) {
        EngineOutput out = engine.clockTick(nullptr);
        if (out.gate > 0.0f) gateCount++;
    }
    // With threshold=1, gateNorm >= 1 fires. gateNorm maps 0..100 from the
    // full int16_t range, so all values pass (deterministic seeded RNG).
    ASSERT_EQ(gateCount, 100,
        "threshold=1 should fire all 100 gates");
}

static void test_threshold_high()
{
    SiftEngine engine;
    engine.init(48000);

    engine.parameterChanged(SiftEngine::kSiftThreshold, 100); // very high threshold

    int gateCount = 0;
    for (int i = 0; i < 100; i++) {
        EngineOutput out = engine.clockTick(nullptr);
        if (out.gate > 0.0f) gateCount++;
    }
    // With threshold=100, only gateNorm==100 fires — none do with default seeds.
    ASSERT_EQ(gateCount, 0,
        "threshold=100 should fire no gates");
}

// --- Step counter independence ---

static void test_step_counter_independence()
{
    SiftEngine engine;
    engine.init(48000);

    engine.parameterChanged(SiftEngine::kSiftCvSteps, 4);
    engine.parameterChanged(SiftEngine::kSiftGateSteps, 8);

    // Clock 4 times: CV step wraps to 0, gate step is at 4
    for (int i = 0; i < 4; i++) {
        engine.clockTick(nullptr);
    }
    // After 4 clocks with cvSteps=4, CV step wraps: 0->1->2->3->0
    ASSERT_EQ(engine.currentStep(), 0,
        "CV step should wrap after cvSteps clocks");
    // Gate step should be at 4 (independent counter, 8 steps, not yet wrapped)
    ASSERT_EQ((int)engine.gateSeqs_[engine.gateSeq_].currentStep, 4,
        "Gate step should be at 4 (not wrapped yet)");

    // Clock 4 more times (total 8): CV wraps again, gate wraps once
    for (int i = 0; i < 4; i++) {
        engine.clockTick(nullptr);
    }
    ASSERT_EQ(engine.currentStep(), 0,
        "CV step should wrap again after 8 total clocks");
    ASSERT_EQ((int)engine.gateSeqs_[engine.gateSeq_].currentStep, 0,
        "Gate step should wrap after gateSteps=8 clocks");
}

// --- Reset ---

static void test_reset_reproduces_sequence()
{
    SiftEngine engine;
    engine.init(48000);

    // Use specific settings for determinism
    engine.parameterChanged(SiftEngine::kSiftCvSteps, 8);
    engine.parameterChanged(SiftEngine::kSiftGateSteps, 8);

    static const int N = 16;
    float pitches1[N];
    float gates1[N];

    // Record N steps
    for (int i = 0; i < N; i++) {
        EngineOutput out = engine.clockTick(nullptr);
        pitches1[i] = out.pitch;
        gates1[i] = out.gate;
    }

    // Reset step counters (seeds are preserved) and replay
    engine.reset();

    for (int i = 0; i < N; i++) {
        EngineOutput out = engine.clockTick(nullptr);
        ASSERT_FLOAT_EQ(out.pitch, pitches1[i],
            "pitch should match after reset");
        ASSERT_TRUE(fabsf(out.gate - gates1[i]) < 0.001f,
            "gate should match after reset");
    }
}

// --- Velocity ---

static void test_velocity_output()
{
    SiftEngine engine;
    engine.init(48000);

    // velocity=0 -> 0 * 0.05 = 0.0V
    engine.parameterChanged(SiftEngine::kSiftVelocity, 0);
    EngineOutput out0 = engine.clockTick(nullptr);
    ASSERT_FLOAT_EQ(out0.velocity, 0.0f, "velocity=0 -> 0.0V");

    // velocity=100 -> 100 * 0.05 = 5.0V
    engine.parameterChanged(SiftEngine::kSiftVelocity, 100);
    EngineOutput out100 = engine.clockTick(nullptr);
    ASSERT_FLOAT_EQ(out100.velocity, 5.0f, "velocity=100 -> 5.0V");

    // velocity=50 -> 50 * 0.05 = 2.5V
    engine.parameterChanged(SiftEngine::kSiftVelocity, 50);
    EngineOutput out50 = engine.clockTick(nullptr);
    ASSERT_FLOAT_EQ(out50.velocity, 2.5f, "velocity=50 -> 2.5V");
}

int main()
{
    test_sequence_raw_deterministic();
    test_sequence_raw_varies();
    test_bit_depth_2();
    test_polarity_positive();
    test_polarity_negative();
    test_polarity_bipolar();
    test_threshold_low();
    test_threshold_high();
    test_step_counter_independence();
    test_reset_reproduces_sequence();
    test_velocity_output();

    printf("%d tests, %d failures\n", tests, failures);
    return failures ? 1 : 0;
}
