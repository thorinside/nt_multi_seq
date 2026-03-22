// Unit tests for FerromagneticEngine.
// Standalone compilation with NT API stubs.

#include "nt_stubs.h"

#include "../scale/ScaleQuantizer.cpp"
#include "../engines/FerromagneticEngine.cpp"

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

// --- 1. Lifecycle tests ---

static void test_constructor_defaults()
{
    FerromagneticEngine engine;
    ASSERT_EQ(engine.currentStep(), 0, "constructor: currentTick=0");
    ASSERT_EQ(engine.sequenceLength(), 16, "constructor: loopSteps=16");
}

static void test_init_seeds_rng()
{
    FerromagneticEngine engine;
    engine.init(48000);
    // After init, step is 0 (resetLoop called)
    ASSERT_EQ(engine.currentStep(), 0, "init: currentTick=0");
}

static void test_reset_clears_history()
{
    FerromagneticEngine engine;
    engine.parameterChanged(FerromagneticEngine::kFerroNoteDensity, 100);
    engine.parameterChanged(FerromagneticEngine::kFerroLoopSteps, 4);
    engine.parameterChanged(FerromagneticEngine::kFerroMaxLayers, 2);
    engine.init(48000);

    // Clock several ticks to build notes
    for (int i = 0; i < 8; i++)
        engine.clockTick(nullptr);

    engine.reset();
    ASSERT_EQ(engine.currentStep(), 0, "reset: currentTick=0");
}

// --- 2. Tick counting ---

static void test_tick_wraps_at_loop_length()
{
    FerromagneticEngine engine;
    engine.parameterChanged(FerromagneticEngine::kFerroLoopSteps, 4);
    engine.parameterChanged(FerromagneticEngine::kFerroNoteDensity, 100);
    engine.init(48000);

    // init: tick=0. clockTick advances first.
    engine.clockTick(nullptr);
    ASSERT_EQ(engine.currentStep(), 1, "tick 1");
    engine.clockTick(nullptr);
    ASSERT_EQ(engine.currentStep(), 2, "tick 2");
    engine.clockTick(nullptr);
    ASSERT_EQ(engine.currentStep(), 3, "tick 3");
    engine.clockTick(nullptr);
    ASSERT_EQ(engine.currentStep(), 0, "tick wraps to 0");
}

static void test_layer_increments_on_wrap()
{
    FerromagneticEngine engine;
    engine.parameterChanged(FerromagneticEngine::kFerroLoopSteps, 4);
    engine.parameterChanged(FerromagneticEngine::kFerroMaxLayers, 3);
    engine.parameterChanged(FerromagneticEngine::kFerroNoteDensity, 100);
    engine.init(48000);

    // First loop: layer 0 (ticks 1,2,3,0)
    for (int i = 0; i < 4; i++)
        engine.clockTick(nullptr);

    // After wrap, layer advances to 1
    // Next tick is tick 1 of layer 1
    EngineOutput out = engine.clockTick(nullptr);
    ASSERT_EQ(engine.currentStep(), 1, "layer 1, tick 1");

    // Complete layer 1 (ticks 2,3,0)
    for (int i = 0; i < 3; i++)
        engine.clockTick(nullptr);

    // After wrap, layer advances to 2
    out = engine.clockTick(nullptr);
    ASSERT_EQ(engine.currentStep(), 1, "layer 2, tick 1");
}

// --- 3. Loop Trigger role ---

static void test_loop_trigger_gate()
{
    FerromagneticEngine engine;
    engine.parameterChanged(FerromagneticEngine::kFerroRole, FerromagneticEngine::kRoleLoopTrig);
    engine.parameterChanged(FerromagneticEngine::kFerroLoopSteps, 4);
    engine.init(48000);

    // Tick 1: no gate
    EngineOutput out = engine.clockTick(nullptr);
    ASSERT_FLOAT_EQ(out.gate, 0.0f, "trig: tick 1 gate=0");

    // Tick 2: no gate
    out = engine.clockTick(nullptr);
    ASSERT_FLOAT_EQ(out.gate, 0.0f, "trig: tick 2 gate=0");

    // Tick 3: no gate
    out = engine.clockTick(nullptr);
    ASSERT_FLOAT_EQ(out.gate, 0.0f, "trig: tick 3 gate=0");

    // Tick 0 (wrap): gate!
    out = engine.clockTick(nullptr);
    ASSERT_FLOAT_EQ(out.gate, 5.0f, "trig: tick 0 gate=5V");

    // Tick 1 again: no gate
    out = engine.clockTick(nullptr);
    ASSERT_FLOAT_EQ(out.gate, 0.0f, "trig: tick 1 again gate=0");
}

// --- 4. Record Gate role ---

static void test_rec_gate_building()
{
    FerromagneticEngine engine;
    engine.parameterChanged(FerromagneticEngine::kFerroRole, FerromagneticEngine::kRoleRecGate);
    engine.parameterChanged(FerromagneticEngine::kFerroLoopSteps, 4);
    engine.parameterChanged(FerromagneticEngine::kFerroMaxLayers, 2);
    engine.parameterChanged(FerromagneticEngine::kFerroNoteDensity, 100);
    engine.init(48000);

    // During building, gate should be high
    for (int i = 0; i < 4; i++) {
        EngineOutput out = engine.clockTick(nullptr);
        ASSERT_FLOAT_EQ(out.gate, 5.0f, "rec gate: building=5V");
    }
}

static void test_rec_gate_hold_completion()
{
    FerromagneticEngine engine;
    engine.parameterChanged(FerromagneticEngine::kFerroRole, FerromagneticEngine::kRoleRecGate);
    engine.parameterChanged(FerromagneticEngine::kFerroLoopSteps, 4);
    engine.parameterChanged(FerromagneticEngine::kFerroMaxLayers, 1);
    engine.parameterChanged(FerromagneticEngine::kFerroCompletion, FerromagneticEngine::kCompletionHold);
    engine.parameterChanged(FerromagneticEngine::kFerroNoteDensity, 100);
    engine.init(48000);

    // Build layer 0 (4 ticks)
    for (int i = 0; i < 4; i++)
        engine.clockTick(nullptr);

    // After completion, gate should be 0 (Hold mode)
    for (int i = 0; i < 4; i++) {
        EngineOutput out = engine.clockTick(nullptr);
        ASSERT_FLOAT_EQ(out.gate, 0.0f, "rec gate: hold=0V");
    }
}

// --- 5. Layer 0 melody: density ---

static void test_density_100_fills_all()
{
    FerromagneticEngine engine;
    engine.parameterChanged(FerromagneticEngine::kFerroNoteDensity, 100);
    engine.parameterChanged(FerromagneticEngine::kFerroLoopSteps, 8);
    engine.parameterChanged(FerromagneticEngine::kFerroMaxLayers, 1);
    engine.init(48000);

    int gateCount = 0;
    for (int i = 0; i < 8; i++) {
        EngineOutput out = engine.clockTick(nullptr);
        if (out.gate > 0.0f) gateCount++;
    }
    ASSERT_EQ(gateCount, 8, "density 100: all beats have gates");
}

static void test_density_0_fills_none()
{
    FerromagneticEngine engine;
    engine.parameterChanged(FerromagneticEngine::kFerroNoteDensity, 0);
    engine.parameterChanged(FerromagneticEngine::kFerroLoopSteps, 8);
    engine.parameterChanged(FerromagneticEngine::kFerroMaxLayers, 1);
    engine.init(48000);

    int gateCount = 0;
    for (int i = 0; i < 8; i++) {
        EngineOutput out = engine.clockTick(nullptr);
        if (out.gate > 0.0f) gateCount++;
    }
    ASSERT_EQ(gateCount, 0, "density 0: no gates");
}

// --- 6. Structured harmony ---

static void test_structured_triads()
{
    FerromagneticEngine engine;
    engine.parameterChanged(FerromagneticEngine::kFerroNoteDensity, 100);
    engine.parameterChanged(FerromagneticEngine::kFerroLoopSteps, 4);
    engine.parameterChanged(FerromagneticEngine::kFerroMaxLayers, 2);
    engine.parameterChanged(FerromagneticEngine::kFerroHarmonyMode, FerromagneticEngine::kHarmonyStructured);
    engine.parameterChanged(FerromagneticEngine::kFerroVoicing, FerromagneticEngine::kVoicingTriads);
    engine.init(48000);

    // Layer 0: record pitches (ticks 1,2,3,0)
    float layer0pitch[4];
    for (int i = 0; i < 4; i++) {
        EngineOutput out = engine.clockTick(nullptr);
        layer0pitch[i] = out.pitch;
    }

    // Layer 1: Triads voicing interval for layer 1 is +2 scale degrees
    // With chromatic fallback (no scale), each degree = 1/12 V
    float layer1pitch[4];
    for (int i = 0; i < 4; i++) {
        EngineOutput out = engine.clockTick(nullptr);
        layer1pitch[i] = out.pitch;
    }

    // Layer 1 pitch should be layer 0 pitch + 2/12 = layer 0 + 0.1667V
    for (int i = 0; i < 4; i++) {
        float expected = layer0pitch[i] + 2.0f / 12.0f;
        ASSERT_FLOAT_EQ(layer1pitch[i], expected, "structured triads: layer1 = layer0 + 2 degrees");
    }
}

// --- 7. Completion modes ---

static void test_hold_completion_silence()
{
    FerromagneticEngine engine;
    engine.parameterChanged(FerromagneticEngine::kFerroNoteDensity, 100);
    engine.parameterChanged(FerromagneticEngine::kFerroLoopSteps, 4);
    engine.parameterChanged(FerromagneticEngine::kFerroMaxLayers, 1);
    engine.parameterChanged(FerromagneticEngine::kFerroCompletion, FerromagneticEngine::kCompletionHold);
    engine.init(48000);

    // Build layer 0 (4 ticks)
    for (int i = 0; i < 4; i++)
        engine.clockTick(nullptr);

    // After completion, all should be silence
    for (int i = 0; i < 8; i++) {
        EngineOutput out = engine.clockTick(nullptr);
        ASSERT_FLOAT_EQ(out.gate, 0.0f, "hold: silence after completion");
    }
}

static void test_decay_rebuild_resets()
{
    FerromagneticEngine engine;
    engine.parameterChanged(FerromagneticEngine::kFerroNoteDensity, 100);
    engine.parameterChanged(FerromagneticEngine::kFerroLoopSteps, 4);
    engine.parameterChanged(FerromagneticEngine::kFerroMaxLayers, 1);
    engine.parameterChanged(FerromagneticEngine::kFerroCompletion, FerromagneticEngine::kCompletionDecayRebuild);
    engine.parameterChanged(FerromagneticEngine::kFerroRefreshRate, 2);
    engine.init(48000);

    // Build layer 0 (4 ticks)
    for (int i = 0; i < 4; i++)
        engine.clockTick(nullptr);

    // Silent loop 1 (4 ticks)
    for (int i = 0; i < 4; i++) {
        EngineOutput out = engine.clockTick(nullptr);
        ASSERT_FLOAT_EQ(out.gate, 0.0f, "decay: silent loop 1");
    }

    // Silent loop 2 (4 ticks) - after this wrap, resetLoop triggers
    for (int i = 0; i < 4; i++) {
        EngineOutput out = engine.clockTick(nullptr);
        ASSERT_FLOAT_EQ(out.gate, 0.0f, "decay: silent loop 2");
    }

    // After rebuild: should be generating notes again (density=100)
    int gateCount = 0;
    for (int i = 0; i < 4; i++) {
        EngineOutput out = engine.clockTick(nullptr);
        if (out.gate > 0.0f) gateCount++;
    }
    ASSERT_EQ(gateCount, 4, "decay: rebuild produces notes");
}

// --- 8. Edge cases ---

static void test_no_scale_fallback()
{
    FerromagneticEngine engine;
    engine.parameterChanged(FerromagneticEngine::kFerroNoteDensity, 100);
    engine.parameterChanged(FerromagneticEngine::kFerroLoopSteps, 4);
    engine.init(48000);

    // Should work without crashing
    for (int i = 0; i < 4; i++) {
        EngineOutput out = engine.clockTick(nullptr);
        ASSERT_FLOAT_EQ(out.gate, 5.0f, "no scale: gate=5V with density=100");
        ASSERT_TRUE(out.pitch >= 0.0f, "no scale: pitch >= 0");
    }
}

static void test_loop_length_change_resets()
{
    FerromagneticEngine engine;
    engine.parameterChanged(FerromagneticEngine::kFerroNoteDensity, 100);
    engine.parameterChanged(FerromagneticEngine::kFerroLoopSteps, 8);
    engine.init(48000);

    // Clock 3 ticks
    for (int i = 0; i < 3; i++)
        engine.clockTick(nullptr);

    // Change loop steps
    engine.parameterChanged(FerromagneticEngine::kFerroLoopSteps, 4);
    ASSERT_EQ(engine.currentStep(), 0, "loop change: tick reset to 0");
}

static void test_max_layers_reduction_resets()
{
    FerromagneticEngine engine;
    engine.parameterChanged(FerromagneticEngine::kFerroNoteDensity, 100);
    engine.parameterChanged(FerromagneticEngine::kFerroLoopSteps, 4);
    engine.parameterChanged(FerromagneticEngine::kFerroMaxLayers, 4);
    engine.init(48000);

    // Build through layer 0 (4 ticks), now on layer 1
    for (int i = 0; i < 4; i++)
        engine.clockTick(nullptr);

    // Build through layer 1 (4 ticks), now on layer 2
    for (int i = 0; i < 4; i++)
        engine.clockTick(nullptr);

    // Reduce max layers below current layer -> reset
    engine.parameterChanged(FerromagneticEngine::kFerroMaxLayers, 1);
    ASSERT_EQ(engine.currentStep(), 0, "layers reduced: tick reset to 0");
}

// --- 9. Focus UI ---

static void test_focus_bars()
{
    FerromagneticEngine engine;
    engine.parameterChanged(FerromagneticEngine::kFerroLoopSteps, 16);
    engine.parameterChanged(FerromagneticEngine::kFerroMaxLayers, 4);
    engine.init(48000);

    FocusBarInfo info;
    engine.getFocusBarInfo(info);
    ASSERT_EQ(info.numBars, 2, "focus: 2 bars");
    ASSERT_EQ(info.bars[0].length, 16, "focus: bar 1 length=loopSteps");
    ASSERT_EQ(info.bars[1].length, 4, "focus: bar 2 length=maxLayers");
}

// --- 10. Parameter clamping ---

static void test_param_clamping()
{
    FerromagneticEngine engine;

    engine.parameterChanged(FerromagneticEngine::kFerroLoopSteps, 0);
    ASSERT_EQ(engine.sequenceLength(), 2, "clamp: loopSteps min=2");

    engine.parameterChanged(FerromagneticEngine::kFerroLoopSteps, 200);
    ASSERT_EQ(engine.sequenceLength(), 128, "clamp: loopSteps max=128");

    engine.parameterChanged(FerromagneticEngine::kFerroNoteDensity, -10);
    // Can't directly check density, but shouldn't crash
    engine.parameterChanged(FerromagneticEngine::kFerroNoteDensity, 200);
    // Should clamp to 100
}

// --- Velocity output ---

static void test_velocity()
{
    FerromagneticEngine engine;
    engine.parameterChanged(FerromagneticEngine::kFerroNoteDensity, 100);
    engine.parameterChanged(FerromagneticEngine::kFerroVelocity, 100);
    engine.parameterChanged(FerromagneticEngine::kFerroLoopSteps, 4);
    engine.init(48000);

    EngineOutput out = engine.clockTick(nullptr);
    ASSERT_FLOAT_EQ(out.velocity, 5.0f, "velocity=100 -> 5.0V");

    engine.parameterChanged(FerromagneticEngine::kFerroVelocity, 0);
    out = engine.clockTick(nullptr);
    ASSERT_FLOAT_EQ(out.velocity, 0.0f, "velocity=0 -> 0.0V");
}

// --- Timed gate ---

static void test_timed_gate_melody()
{
    FerromagneticEngine engine;
    engine.parameterChanged(FerromagneticEngine::kFerroRole, FerromagneticEngine::kRoleMelody);
    engine.parameterChanged(FerromagneticEngine::kFerroGateLength, 50);
    ASSERT_TRUE(engine.usesTimedGate(), "melody uses timed gate");
    ASSERT_EQ(engine.gateLengthPercent(), 50, "gate length = 50%");
}

static void test_timed_gate_looptrig()
{
    FerromagneticEngine engine;
    engine.parameterChanged(FerromagneticEngine::kFerroRole, FerromagneticEngine::kRoleLoopTrig);
    ASSERT_TRUE(!engine.usesTimedGate(), "loop trig: no timed gate");
}

// --- Inversion offset wrapping ---

static void test_new_inversion_offset_wraps()
{
    // After many New Inversion cycles, the melody should still produce
    // meaningful degrees (not pinned to 254 due to unbounded offset).
    FerromagneticEngine engine;
    engine.parameterChanged(FerromagneticEngine::kFerroNoteDensity, 100);
    engine.parameterChanged(FerromagneticEngine::kFerroLoopSteps, 2);
    engine.parameterChanged(FerromagneticEngine::kFerroMaxLayers, 1);
    engine.parameterChanged(FerromagneticEngine::kFerroCompletion, FerromagneticEngine::kCompletionNewInversion);
    engine.init(48000);

    // Run through many full inversion cycles.
    // Each cycle: 1 layer x 2 steps = 2 ticks, then handleLoopWrap increments
    // inversionOffset_ and resets. After 50 cycles, inversionOffset_ = 50.
    // With numDegrees_=7, degree would have been 50 + pick(0..6) = 50..56
    // without the modulo fix, eventually clamping to 254.
    for (int cycle = 0; cycle < 50; cycle++) {
        for (int t = 0; t < 2; t++)
            engine.clockTick(nullptr);
    }

    // The next cycle should still produce pitches with degrees < numDegrees*2
    // (base 0..6 + wrapped offset 0..6 = at most 12, well below 254).
    bool allReasonable = true;
    for (int t = 0; t < 2; t++) {
        EngineOutput out = engine.clockTick(nullptr);
        // Without scale, pitch = degree / 12.0. Degree should be < 14 (7+7).
        // Pitch < 14/12 = 1.167
        if (out.gate > 0.0f && out.pitch > 2.0f) {
            allReasonable = false;
        }
    }
    ASSERT_TRUE(allReasonable, "new inversion: degrees stay bounded after many cycles");
}

int main()
{
    // 1. Lifecycle
    test_constructor_defaults();
    test_init_seeds_rng();
    test_reset_clears_history();

    // 2. Tick counting
    test_tick_wraps_at_loop_length();
    test_layer_increments_on_wrap();

    // 3. Loop Trigger
    test_loop_trigger_gate();

    // 4. Record Gate
    test_rec_gate_building();
    test_rec_gate_hold_completion();

    // 5. Density
    test_density_100_fills_all();
    test_density_0_fills_none();

    // 6. Structured harmony
    test_structured_triads();

    // 7. Completion
    test_hold_completion_silence();
    test_decay_rebuild_resets();

    // 8. Edge cases
    test_no_scale_fallback();
    test_loop_length_change_resets();
    test_max_layers_reduction_resets();

    // 9. Focus UI
    test_focus_bars();

    // 10. Params
    test_param_clamping();

    // Extras
    test_velocity();
    test_timed_gate_melody();
    test_timed_gate_looptrig();
    test_new_inversion_offset_wraps();

    printf("%d tests, %d failures\n", tests, failures);
    return failures ? 1 : 0;
}
