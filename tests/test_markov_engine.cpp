// Unit tests for SeqMarkovEngine.
// Verifies Markov weight computation, emotion, drift, density, and reset behavior.
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
// computeWeight behavioral properties
// -----------------------------------------------------------------------

// Self-transition weight = 1.0 + selfBoost for the active style.
// kStyleTonal has selfBoost=1.0, so weight should be 2.0.
static void test_self_transition_weight()
{
    SeqMarkovEngine engine;
    engine.init(48000);
    engine.appliedStyle_ = SeqMarkovEngine::kStyleTonal;

    float w = engine.computeWeight(3, 3, 7);
    ASSERT_FLOAT_EQ(w, 2.0f, "self-transition with Tonal (selfBoost=1.0) should be 2.0");
}

// Stepwise style: adjacent transitions should have higher weight than distant ones.
static void test_stepwise_adjacent_higher()
{
    SeqMarkovEngine engine;
    engine.init(48000);
    engine.appliedStyle_ = SeqMarkovEngine::kStyleStepwise;

    float wAdjacent = engine.computeWeight(3, 4, 7);
    float wDistant  = engine.computeWeight(3, 6, 7);
    ASSERT_TRUE(wAdjacent > wDistant,
        "Stepwise: adjacent (3->4) should weigh more than distant (3->6)");
}

// Leaping style: distant transitions should have higher weight than adjacent.
static void test_leaping_distant_higher()
{
    SeqMarkovEngine engine;
    engine.init(48000);
    engine.appliedStyle_ = SeqMarkovEngine::kStyleLeaping;

    float wDistant  = engine.computeWeight(0, 3, 7);
    float wAdjacent = engine.computeWeight(0, 1, 7);
    ASSERT_TRUE(wDistant > wAdjacent,
        "Leaping: distant (0->3) should weigh more than adjacent (0->1)");
}

// Home gravity: degree 0 (tonic) should get a boost. kStyleTonal homeGravity=0.8.
static void test_home_gravity()
{
    SeqMarkovEngine engine;
    engine.init(48000);
    engine.appliedStyle_ = SeqMarkovEngine::kStyleTonal;

    float wHome  = engine.computeWeight(3, 0, 7);
    float wOther = engine.computeWeight(3, 2, 7);
    ASSERT_TRUE(wHome > wOther,
        "Tonal: transition to tonic (deg 0) should weigh more than to deg 2");
}

// Fifth gravity: fifthDeg for 7 degrees = (7*7+6)/12 = 4.
// kStyleTonal fifthGravity=0.6.
static void test_fifth_gravity()
{
    SeqMarkovEngine engine;
    engine.init(48000);
    engine.appliedStyle_ = SeqMarkovEngine::kStyleTonal;

    // fifthDeg = (7*7+6)/12 = 55/12 = 4
    float wFifth = engine.computeWeight(0, 4, 7);
    float wOther = engine.computeWeight(0, 2, 7);
    ASSERT_TRUE(wFifth > wOther,
        "Tonal: transition to fifth (deg 4) should weigh more than to deg 2");
}

// -----------------------------------------------------------------------
// applyEmotion
// -----------------------------------------------------------------------

// Low emotion (0) should favor descending intervals.
static void test_emotion_low_favors_descending()
{
    SeqMarkovEngine engine;
    engine.init(48000);
    engine.appliedEmotion_ = 0;

    float wDesc = engine.applyEmotion(1.0f, 5, 3); // descending
    float wAsc  = engine.applyEmotion(1.0f, 3, 5); // ascending
    ASSERT_TRUE(wDesc > wAsc,
        "Emotion=0: descending (5->3) should weigh more than ascending (3->5)");
}

// High emotion (100) should favor ascending intervals.
static void test_emotion_high_favors_ascending()
{
    SeqMarkovEngine engine;
    engine.init(48000);
    engine.appliedEmotion_ = 100;

    float wAsc  = engine.applyEmotion(1.0f, 3, 5); // ascending
    float wDesc = engine.applyEmotion(1.0f, 5, 3); // descending
    ASSERT_TRUE(wAsc > wDesc,
        "Emotion=100: ascending (3->5) should weigh more than descending (5->3)");
}

// Neutral emotion (50) should apply no directional bias.
static void test_emotion_neutral_no_bias()
{
    SeqMarkovEngine engine;
    engine.init(48000);
    engine.appliedEmotion_ = 50;

    float wAsc  = engine.applyEmotion(1.0f, 3, 5);
    float wDesc = engine.applyEmotion(1.0f, 5, 3);
    ASSERT_FLOAT_EQ(wAsc, 1.0f, "Emotion=50: ascending weight should be 1.0");
    ASSERT_FLOAT_EQ(wDesc, 1.0f, "Emotion=50: descending weight should be 1.0");
}

// -----------------------------------------------------------------------
// ensureAtLeastOneActive
// -----------------------------------------------------------------------

// All steps inactive: step 0 should be forced active.
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

// If any step is already active, no changes should be made.
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
// driftTowardTargets
// -----------------------------------------------------------------------

// Style drifts by 1 per call; 3 calls should close a gap of 3.
static void test_drift_converges()
{
    SeqMarkovEngine engine;
    engine.init(48000);
    engine.style_ = 3;
    engine.appliedStyle_ = 0;

    engine.driftTowardTargets();
    engine.driftTowardTargets();
    engine.driftTowardTargets();

    ASSERT_EQ(engine.appliedStyle_, 3,
        "After 3 drift calls, appliedStyle_ should reach target 3");
}

// Emotion drifts up to 5 per call.
static void test_drift_emotion()
{
    SeqMarkovEngine engine;
    engine.init(48000);
    engine.emotion_ = 100;
    engine.appliedEmotion_ = 0;

    engine.driftTowardTargets();
    ASSERT_EQ(engine.appliedEmotion_, 5,
        "After 1 drift call, appliedEmotion_ should be 5");

    // 19 more calls: total 20 calls * 5/call = 100
    for (int i = 0; i < 19; ++i)
        engine.driftTowardTargets();

    ASSERT_EQ(engine.appliedEmotion_, 100,
        "After 20 drift calls, appliedEmotion_ should reach target 100");
}

// -----------------------------------------------------------------------
// Density filtering
// -----------------------------------------------------------------------

// Density 100: all rhythm-active steps should remain active after generate.
static void test_density_100_all_active()
{
    SeqMarkovEngine engine;
    engine.init(48000);
    engine.parameterChanged(SeqMarkovEngine::kMarkovDensity, 100);
    engine.parameterChanged(SeqMarkovEngine::kMarkovMutation, 0);
    engine.parameterChanged(SeqMarkovEngine::kMarkovLength, 8);
    engine.appliedDensity_ = 100;

    // Force rhythm initialization so we have a known active pattern.
    engine.initializeRhythm();

    // Record which steps the rhythm set active.
    bool rhythmActive[8];
    for (int i = 0; i < 8; ++i)
        rhythmActive[i] = engine.sequence_[i].active;

    // Generate with density=100 and numDegrees=7.
    engine.generateSequence(7);

    // Every step that was active in the rhythm should still be active.
    bool allPreserved = true;
    for (int i = 0; i < 8; ++i) {
        if (rhythmActive[i] && !engine.sequence_[i].active)
            allPreserved = false;
    }
    ASSERT_TRUE(allPreserved,
        "Density=100: all rhythm-active steps should remain active after generate");
}

// Density 1: over many generations, gate count should be much lower than density 100.
static void test_density_1_mostly_inactive()
{
    SeqMarkovEngine engine;
    engine.init(48000);
    engine.parameterChanged(SeqMarkovEngine::kMarkovDensity, 1);
    engine.parameterChanged(SeqMarkovEngine::kMarkovMutation, 0);
    engine.parameterChanged(SeqMarkovEngine::kMarkovLength, 16);
    engine.appliedDensity_ = 1;

    engine.initializeRhythm();

    // Count active gates across many regenerations.
    int activeCount = 0;
    int totalSteps = 0;
    for (int gen = 0; gen < 50; ++gen) {
        engine.generateSequence(7);
        for (int i = 0; i < 16; ++i) {
            totalSteps++;
            if (engine.sequence_[i].active) activeCount++;
        }
    }

    // With density=1, roughly 1% of rhythm-active steps should pass through,
    // plus ensureAtLeastOneActive guarantees at least 1 per generation.
    // With density=100 nearly all rhythm-active steps pass.
    // Just check that fewer than half are active (very conservative).
    ASSERT_TRUE(activeCount < totalSteps / 2,
        "Density=1: should have far fewer active steps than total");
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

    // First clockTick triggers initial generation (needsRegenerate_=true).
    engine.clockTick(nullptr); // uses numDeg=7 (no scale)

    // Now numDegrees_ should be 7 and needsRegenerate_ cleared.
    ASSERT_EQ(engine.numDegrees_, 7,
        "After first clockTick with nullptr, numDegrees_ should be 7");
    ASSERT_TRUE(!engine.needsRegenerate_,
        "needsRegenerate_ should be cleared after first clockTick");

    // Manually set numDegrees_ to 12 to simulate a previous different scale.
    engine.numDegrees_ = 12;

    // Next clockTick with nullptr gives numDeg=7, which mismatches 12.
    engine.clockTick(nullptr);

    // The mismatch should have set regeneratePending_=true
    // (it fires on the next wrap).
    ASSERT_TRUE(engine.regeneratePending_,
        "Scale size mismatch should set regeneratePending_=true");
}

// -----------------------------------------------------------------------
// Reset
// -----------------------------------------------------------------------

static void test_reset()
{
    SeqMarkovEngine engine;
    engine.init(48000);
    engine.parameterChanged(SeqMarkovEngine::kMarkovLength, 16);

    // Advance several clock ticks so currentStep_ moves forward.
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
    // computeWeight behavioral properties
    test_self_transition_weight();
    test_stepwise_adjacent_higher();
    test_leaping_distant_higher();
    test_home_gravity();
    test_fifth_gravity();

    // applyEmotion
    test_emotion_low_favors_descending();
    test_emotion_high_favors_ascending();
    test_emotion_neutral_no_bias();

    // ensureAtLeastOneActive
    test_all_inactive_forces_step0();
    test_has_active_unchanged();

    // driftTowardTargets
    test_drift_converges();
    test_drift_emotion();

    // Density filtering
    test_density_100_all_active();
    test_density_1_mostly_inactive();

    // Scale change
    test_scale_change_triggers_regenerate();

    // Reset
    test_reset();

    printf("%d tests, %d failures\n", tests, failures);
    return failures ? 1 : 0;
}
