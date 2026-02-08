// Unit test: verify ThorpEngine velocity patterns produce varying output.
// Standalone compilation with NT API stubs.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Stub for NT_intToString (used by ThorpEngine::getStatusText)
extern "C" int NT_intToString(char* buffer, int value) {
    return sprintf(buffer, "%d", value);
}

#include "../engines/ThorpEngine.h"
#include "../engines/ThorpEngine.cpp"

static int failures = 0;
static int tests = 0;

#define ASSERT_TRUE(cond, msg) do { \
    tests++; \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s\n", msg); \
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

// Test: Constant velocity pattern produces uniform velocity
static void test_constant_pattern()
{
    ThorpEngine engine;
    engine.init(48000);
    engine.parameterChanged(ThorpEngine::kThorpVelPattern, 0); // Constant
    engine.noteOn(60, 100);

    float first = -1.0f;
    bool allSame = true;
    for (int i = 0; i < 8; i++) {
        EngineOutput eo = engine.clockTick(nullptr);
        if (i == 0) first = eo.velocity;
        else if (fabsf(eo.velocity - first) > 0.001f) allSame = false;
    }
    ASSERT_TRUE(allSame, "Constant pattern should produce uniform velocity");
}

// Test: Accent velocity pattern produces varying velocity
static void test_accent_pattern()
{
    ThorpEngine engine;
    engine.init(48000);
    engine.parameterChanged(ThorpEngine::kThorpVelPattern, 1); // Accent
    engine.noteOn(60, 100);

    float velocities[8];
    for (int i = 0; i < 8; i++) {
        EngineOutput eo = engine.clockTick(nullptr);
        velocities[i] = eo.velocity;
    }

    // Accent pattern: 75, 35, 40, 35, 75, 35, 40, 35
    // Expected velocities: 3.75, 1.75, 2.0, 1.75, 3.75, 1.75, 2.0, 1.75
    bool hasVariation = false;
    for (int i = 1; i < 8; i++) {
        if (fabsf(velocities[i] - velocities[0]) > 0.001f) {
            hasVariation = true;
            break;
        }
    }
    ASSERT_TRUE(hasVariation, "Accent pattern should produce varying velocity");

    // Check specific expected values
    ASSERT_FLOAT_EQ(velocities[0], 3.75f, "Accent step 0 = 75 * 0.05");
    ASSERT_FLOAT_EQ(velocities[1], 1.75f, "Accent step 1 = 35 * 0.05");
    ASSERT_FLOAT_EQ(velocities[2], 2.00f, "Accent step 2 = 40 * 0.05");
    ASSERT_FLOAT_EQ(velocities[3], 1.75f, "Accent step 3 = 35 * 0.05");
}

// Test: Changing velocity pattern mid-stream takes effect
static void test_pattern_change()
{
    ThorpEngine engine;
    engine.init(48000);
    engine.parameterChanged(ThorpEngine::kThorpVelPattern, 0); // Constant
    engine.noteOn(60, 100);

    EngineOutput eo1 = engine.clockTick(nullptr);
    ASSERT_FLOAT_EQ(eo1.velocity, 2.5f, "Constant pattern = 2.5V");

    // Switch to Accent
    engine.parameterChanged(ThorpEngine::kThorpVelPattern, 1);
    EngineOutput eo2 = engine.clockTick(nullptr);
    // Step 1 of Accent = 35 * 0.05 = 1.75
    ASSERT_FLOAT_EQ(eo2.velocity, 1.75f, "After switch to Accent, step 1 = 1.75V");
}

// Test: Global velocity scales the pattern
static void test_global_velocity_scaling()
{
    ThorpEngine engine;
    engine.init(48000);
    engine.parameterChanged(ThorpEngine::kThorpVelPattern, 0); // Constant (50)
    engine.parameterChanged(ThorpEngine::kThorpGlobalVelocity, 50); // 50%
    engine.noteOn(60, 100);

    EngineOutput eo = engine.clockTick(nullptr);
    // 50 * 50 / 100 = 25, 25 * 0.05 = 1.25
    ASSERT_FLOAT_EQ(eo.velocity, 1.25f, "Global vel 50% halves the output");
}

// Test: velocity varies across multiple notes (not just first tick)
static void test_velocity_across_notes()
{
    ThorpEngine engine;
    engine.init(48000);
    engine.parameterChanged(ThorpEngine::kThorpVelPattern, 1); // Accent
    engine.parameterChanged(ThorpEngine::kThorpGateLen, 50);   // Non-legato
    engine.noteOn(60, 100);
    engine.noteOn(64, 100);

    float velocities[16];
    for (int i = 0; i < 16; i++) {
        EngineOutput eo = engine.clockTick(nullptr);
        velocities[i] = eo.velocity;
    }

    // Verify the pattern repeats after 8 steps
    for (int i = 0; i < 8; i++) {
        ASSERT_FLOAT_EQ(velocities[i], velocities[i + 8],
            "Velocity pattern should repeat after 8 steps");
    }

    // Verify it's not all the same
    bool varies = false;
    for (int i = 1; i < 8; i++) {
        if (fabsf(velocities[i] - velocities[0]) > 0.001f) {
            varies = true;
            break;
        }
    }
    ASSERT_TRUE(varies, "Accent pattern should vary across 16 notes");
}

int main()
{
    test_constant_pattern();
    test_accent_pattern();
    test_pattern_change();
    test_global_velocity_scaling();
    test_velocity_across_notes();

    printf("%d tests, %d failures\n", tests, failures);
    return failures ? 1 : 0;
}
