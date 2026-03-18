// Unit tests for SeqMarkovEngine.
// Verifies Markov matrix selection, emotion, rhythm, and reset behavior.
// Standalone compilation with NT API stubs.

#include "nt_stubs.h"

#include "../scale/ScaleQuantizer.h"
#include "../scale/ScaleQuantizer.cpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Expose private/protected members for white-box testing.
#define private public
#define protected public

#include "../engines/SeqMarkovEngine.h"
#include "../engines/SeqMarkovEngine.cpp"

#undef private
#undef protected

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

// -----------------------------------------------------------------------
// applyEmotion
// -----------------------------------------------------------------------

static void test_emotion_low_favors_descending()
{
    SeqMarkovEngine engine;
    engine.init(48000);
    engine.emotion_ = 0;

    float wDesc = engine.applyEmotion(1.0f, 5, 3);
    float wAsc  = engine.applyEmotion(1.0f, 3, 5);
    ASSERT_TRUE(wDesc > wAsc,
        "Emotion=0: descending (5->3) should weigh more than ascending (3->5)");
}

static void test_emotion_high_favors_ascending()
{
    SeqMarkovEngine engine;
    engine.init(48000);
    engine.emotion_ = 100;

    float wAsc  = engine.applyEmotion(1.0f, 3, 5);
    float wDesc = engine.applyEmotion(1.0f, 5, 3);
    ASSERT_TRUE(wAsc > wDesc,
        "Emotion=100: ascending (3->5) should weigh more than descending (5->3)");
}

static void test_emotion_neutral_no_bias()
{
    SeqMarkovEngine engine;
    engine.init(48000);
    engine.emotion_ = 50;

    float wAsc  = engine.applyEmotion(1.0f, 3, 5);
    float wDesc = engine.applyEmotion(1.0f, 5, 3);
    ASSERT_FLOAT_EQ(wAsc, 1.0f, "Emotion=50: ascending weight should be 1.0");
    ASSERT_FLOAT_EQ(wDesc, 1.0f, "Emotion=50: descending weight should be 1.0");
}

// -----------------------------------------------------------------------
// Degree mapping
// -----------------------------------------------------------------------

// Default 7-degree map should be identity.
static void test_default_degree_map()
{
    SeqMarkovEngine engine;
    engine.init(48000);

    for (int i = 0; i < 7; ++i) {
        ASSERT_EQ(engine.degreeMap_[i], i,
            "Default 7-degree map should be identity");
    }
}

// -----------------------------------------------------------------------
// ensureAtLeastOneActive
// -----------------------------------------------------------------------

static void test_all_inactive_forces_step0()
{
    SeqMarkovEngine engine;
    engine.init(48000);

    for (int i = 0; i < 8; ++i)
        engine.sequence_[i].active = false;

    engine.ensureAtLeastOneActive(8);
    ASSERT_TRUE(engine.sequence_[0].active,
        "All inactive: step 0 should be forced active");
}

static void test_has_active_unchanged()
{
    SeqMarkovEngine engine;
    engine.init(48000);

    for (int i = 0; i < 8; ++i)
        engine.sequence_[i].active = false;
    engine.sequence_[3].active = true;

    engine.ensureAtLeastOneActive(8);
    ASSERT_TRUE(engine.sequence_[3].active,
        "Step 3 should remain active");
    ASSERT_TRUE(!engine.sequence_[0].active,
        "Step 0 should remain inactive when step 3 is already active");
}

// -----------------------------------------------------------------------
// Regenerate / Randomize
// -----------------------------------------------------------------------

static void test_regenerate_resets_step()
{
    SeqMarkovEngine engine;
    engine.init(48000);
    engine.parameterChanged(SeqMarkovEngine::kMarkovMutation, 0);
    engine.parameterChanged(SeqMarkovEngine::kMarkovLength, 16);

    engine.clockTick(nullptr);
    engine.uiForceRegenerate();

    ASSERT_EQ(engine.currentStep_, 0,
        "uiForceRegenerate should reset currentStep_ to 0");
}

static void test_regenerate_resets_last_degree_before_generate()
{
    SeqMarkovEngine engine;
    engine.init(48000);
    engine.parameterChanged(SeqMarkovEngine::kMarkovLength, 16);

    // Advance to set lastDegree_ to something non-zero.
    engine.clockTick(nullptr);
    engine.lastDegree_ = 5;

    engine.uiForceRegenerate();
    // After regenerate, lastDegree_ should have been reset to 0 before generating,
    // then updated by the generate. We can't predict the final value, but
    // currentStep_ should be 0.
    ASSERT_EQ(engine.currentStep_, 0,
        "uiForceRegenerate should reset currentStep_ to 0");
}

static void test_randomize_keeps_rhythm()
{
    SeqMarkovEngine engine;
    engine.init(48000);
    engine.parameterChanged(SeqMarkovEngine::kMarkovMutation, 0);
    engine.parameterChanged(SeqMarkovEngine::kMarkovLength, 16);

    engine.clockTick(nullptr);

    bool rhythm1[16];
    for (int i = 0; i < 16; ++i)
        rhythm1[i] = engine.sequence_[i].active;

    engine.uiForceRandomize();

    bool rhythmPreserved = true;
    for (int i = 0; i < 16; ++i) {
        if (engine.sequence_[i].active != rhythm1[i])
            rhythmPreserved = false;
    }
    ASSERT_TRUE(rhythmPreserved,
        "uiForceRandomize should preserve rhythm pattern");
}

// -----------------------------------------------------------------------
// Scale change triggers regeneration
// -----------------------------------------------------------------------

static void test_scale_change_triggers_regenerate()
{
    SeqMarkovEngine engine;
    engine.init(48000);
    engine.parameterChanged(SeqMarkovEngine::kMarkovMutation, 0);
    engine.parameterChanged(SeqMarkovEngine::kMarkovLength, 8);

    engine.clockTick(nullptr);

    ASSERT_EQ(engine.numDegrees_, 7,
        "After first clockTick with nullptr, numDegrees_ should be 7");
    ASSERT_TRUE(!engine.needsRegenerate_,
        "needsRegenerate_ should be cleared after first clockTick");

    engine.numDegrees_ = 12;
    engine.clockTick(nullptr);

    ASSERT_TRUE(engine.regeneratePending_,
        "Scale size mismatch should set regeneratePending_=true");
}

// -----------------------------------------------------------------------
// Matrix-based pickNextDegree produces valid degrees
// -----------------------------------------------------------------------

static void test_pick_next_degree_in_range()
{
    SeqMarkovEngine engine;
    engine.init(48000);

    for (int style = 0; style < SeqMarkovEngine::kNumStyles; ++style) {
        engine.style_ = style;
        for (int trial = 0; trial < 50; ++trial) {
            int next = engine.pickNextDegree(0, 7);
            ASSERT_TRUE(next >= 0 && next < 7,
                "pickNextDegree should return valid degree for 7-note scale");
        }
    }
}

// -----------------------------------------------------------------------
// Reset
// -----------------------------------------------------------------------

static void test_reset()
{
    SeqMarkovEngine engine;
    engine.init(48000);
    engine.parameterChanged(SeqMarkovEngine::kMarkovLength, 16);

    for (int i = 0; i < 5; ++i)
        engine.clockTick(nullptr);

    ASSERT_TRUE(engine.currentStep_ > 0,
        "currentStep_ should have advanced after clock ticks");

    engine.reset();
    ASSERT_EQ(engine.currentStep_, 0,
        "After reset(), currentStep_ should be 0");
}

// -----------------------------------------------------------------------

int main()
{
    // applyEmotion
    test_emotion_low_favors_descending();
    test_emotion_high_favors_ascending();
    test_emotion_neutral_no_bias();

    // Degree mapping
    test_default_degree_map();

    // ensureAtLeastOneActive
    test_all_inactive_forces_step0();
    test_has_active_unchanged();

    // Regenerate / Randomize
    test_regenerate_resets_step();
    test_regenerate_resets_last_degree_before_generate();
    test_randomize_keeps_rhythm();

    // Scale change
    test_scale_change_triggers_regenerate();

    // Matrix-based degree selection
    test_pick_next_degree_in_range();

    // Reset
    test_reset();

    printf("%d tests, %d failures\n", tests, failures);
    return failures ? 1 : 0;
}
