// Unit tests for SomaEngine.
// Standalone compilation with NT API stubs.

#include "nt_stubs.h"

#include "../scale/ScaleQuantizer.cpp"
#include "../engines/SomaEngine.cpp"

#include <stdio.h>
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

// --- Zero mutation preserves pattern ---

// With noteMutate=0 and gateMutate=0, rngFloat() < 0.0f is always false,
// so the pattern established at init never changes. Two consecutive cycles
// of the same length must produce identical (pitch, gate) sequences.
static void test_zero_mutation_repeats()
{
    SomaEngine engine;
    engine.parameterChanged(SomaEngine::kSomaNoteMutate, 0);
    engine.parameterChanged(SomaEngine::kSomaGateMutate, 0);
    engine.parameterChanged(SomaEngine::kSomaOctaveSpread, 0);
    engine.parameterChanged(SomaEngine::kSomaLength, 4);
    engine.init(48000);

    // First cycle: 4 ticks
    float pitchA[4], gateA[4];
    for (int i = 0; i < 4; i++) {
        EngineOutput out = engine.clockTick(nullptr);
        pitchA[i] = out.pitch;
        gateA[i] = out.gate;
    }

    // Second cycle: 4 ticks
    float pitchB[4], gateB[4];
    for (int i = 0; i < 4; i++) {
        EngineOutput out = engine.clockTick(nullptr);
        pitchB[i] = out.pitch;
        gateB[i] = out.gate;
    }

    for (int i = 0; i < 4; i++) {
        ASSERT_FLOAT_EQ(pitchA[i], pitchB[i], "zero mutation: pitch repeats");
        ASSERT_FLOAT_EQ(gateA[i], gateB[i], "zero mutation: gate repeats");
    }
}

// --- Length change ---

// Set length=8, clock 3 steps, then change length to 4.
// On subsequent ticks, currentStep() must not exceed 3.
static void test_length_change()
{
    SomaEngine engine;
    engine.parameterChanged(SomaEngine::kSomaNoteMutate, 0);
    engine.parameterChanged(SomaEngine::kSomaGateMutate, 0);
    engine.parameterChanged(SomaEngine::kSomaOctaveSpread, 0);
    engine.parameterChanged(SomaEngine::kSomaLength, 8);
    engine.init(48000);

    // Clock 3 steps: currentStep goes 1, 2, 3
    for (int i = 0; i < 3; i++)
        engine.clockTick(nullptr);

    ASSERT_EQ(engine.currentStep(), 3, "after 3 ticks, step=3");

    // Change length to 4
    engine.parameterChanged(SomaEngine::kSomaLength, 4);

    // Clock several more ticks and verify step stays within [0,3]
    for (int i = 0; i < 8; i++) {
        engine.clockTick(nullptr);
        int step = engine.currentStep();
        ASSERT_TRUE(step >= 0 && step <= 3,
            "after length change to 4, step must be 0-3");
    }
}

// --- Velocity output ---

static void test_velocity_0()
{
    SomaEngine engine;
    engine.parameterChanged(SomaEngine::kSomaVelocity, 0);
    engine.parameterChanged(SomaEngine::kSomaNoteMutate, 0);
    engine.parameterChanged(SomaEngine::kSomaGateMutate, 0);
    engine.parameterChanged(SomaEngine::kSomaOctaveSpread, 0);
    engine.init(48000);

    EngineOutput out = engine.clockTick(nullptr);
    ASSERT_FLOAT_EQ(out.velocity, 0.0f, "velocity=0 -> 0.0V");
}

static void test_velocity_100()
{
    SomaEngine engine;
    engine.parameterChanged(SomaEngine::kSomaVelocity, 100);
    engine.parameterChanged(SomaEngine::kSomaNoteMutate, 0);
    engine.parameterChanged(SomaEngine::kSomaGateMutate, 0);
    engine.parameterChanged(SomaEngine::kSomaOctaveSpread, 0);
    engine.init(48000);

    EngineOutput out = engine.clockTick(nullptr);
    ASSERT_FLOAT_EQ(out.velocity, 5.0f, "velocity=100 -> 5.0V");
}

static void test_velocity_50()
{
    SomaEngine engine;
    engine.parameterChanged(SomaEngine::kSomaVelocity, 50);
    engine.parameterChanged(SomaEngine::kSomaNoteMutate, 0);
    engine.parameterChanged(SomaEngine::kSomaGateMutate, 0);
    engine.parameterChanged(SomaEngine::kSomaOctaveSpread, 0);
    engine.init(48000);

    EngineOutput out = engine.clockTick(nullptr);
    ASSERT_FLOAT_EQ(out.velocity, 2.5f, "velocity=50 -> 2.5V");
}

// --- No-scale fallback ---

// With clockTick(nullptr) the chromatic fallback is used:
//   pitch = degree / 12.0
//   midiNote = 60 + degree
// With 0 mutation and octaveSpread=0, the constructor-initialized
// pattern (all degree 0, all gate true) is never altered, so output
// is deterministic: pitch=0.0, gate=5.0, midiNote=60 on every step.
static void test_no_scale_fallback()
{
    SomaEngine engine;
    engine.parameterChanged(SomaEngine::kSomaNoteMutate, 0);
    engine.parameterChanged(SomaEngine::kSomaGateMutate, 0);
    engine.parameterChanged(SomaEngine::kSomaOctaveSpread, 0);
    engine.parameterChanged(SomaEngine::kSomaLength, 4);
    engine.init(48000);

    for (int i = 0; i < 4; i++) {
        EngineOutput out = engine.clockTick(nullptr);
        // Constructor initializes all notePattern_ to 0 (degree 0)
        // Chromatic fallback: pitch = degree / 12.0 = 0.0
        ASSERT_FLOAT_EQ(out.pitch, 0.0f, "no-scale fallback: pitch = degree/12.0");
        // Constructor initializes all gatePattern_ to true
        ASSERT_FLOAT_EQ(out.gate, 5.0f, "no-scale fallback: gate = 5.0V");
        // midiNote = 60 + degree = 60
        ASSERT_EQ((int)out.midiNote, 60, "no-scale fallback: midiNote = 60+degree");
    }
}

// --- Step counter ---

// With length=4, after 4 clockTicks the step wraps back to 0.
// clockTick increments step first: (0+1)%4=1, (1+1)%4=2, (2+1)%4=3, (3+1)%4=0.
static void test_step_wraps()
{
    SomaEngine engine;
    engine.parameterChanged(SomaEngine::kSomaNoteMutate, 0);
    engine.parameterChanged(SomaEngine::kSomaGateMutate, 0);
    engine.parameterChanged(SomaEngine::kSomaOctaveSpread, 0);
    engine.parameterChanged(SomaEngine::kSomaLength, 4);
    engine.init(48000);

    // init sets currentStep_ = 0
    ASSERT_EQ(engine.currentStep(), 0, "after init, step=0");

    engine.clockTick(nullptr);
    ASSERT_EQ(engine.currentStep(), 1, "after tick 1, step=1");

    engine.clockTick(nullptr);
    ASSERT_EQ(engine.currentStep(), 2, "after tick 2, step=2");

    engine.clockTick(nullptr);
    ASSERT_EQ(engine.currentStep(), 3, "after tick 3, step=3");

    engine.clockTick(nullptr);
    ASSERT_EQ(engine.currentStep(), 0, "after tick 4, step wraps to 0");
}

// reset() sets currentStep back to 0.
static void test_reset()
{
    SomaEngine engine;
    engine.parameterChanged(SomaEngine::kSomaNoteMutate, 0);
    engine.parameterChanged(SomaEngine::kSomaGateMutate, 0);
    engine.parameterChanged(SomaEngine::kSomaOctaveSpread, 0);
    engine.parameterChanged(SomaEngine::kSomaLength, 8);
    engine.init(48000);

    // Advance a few steps
    engine.clockTick(nullptr);
    engine.clockTick(nullptr);
    engine.clockTick(nullptr);
    ASSERT_EQ(engine.currentStep(), 3, "before reset, step=3");

    engine.reset();
    ASSERT_EQ(engine.currentStep(), 0, "after reset, step=0");
}

int main()
{
    test_zero_mutation_repeats();
    test_length_change();
    test_velocity_0();
    test_velocity_100();
    test_velocity_50();
    test_no_scale_fallback();
    test_step_wraps();
    test_reset();

    printf("%d tests, %d failures\n", tests, failures);
    return failures ? 1 : 0;
}
