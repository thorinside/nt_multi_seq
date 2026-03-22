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

// --- 11. Edge case data-flow tests ---

static void test_status_text_small_buffer()
{
    // fmtInt must not overflow when maxLen is tiny
    FerromagneticEngine engine;
    engine.parameterChanged(FerromagneticEngine::kFerroRole, FerromagneticEngine::kRoleMelody);
    engine.init(48000);

    char buf[4];
    memset(buf, 'X', sizeof(buf));
    int len = engine.getStatusText(buf, 4);
    // Must not write past buf[3]
    ASSERT_TRUE(len <= 3, "status text: len fits in maxLen=4");
    ASSERT_TRUE(buf[len] == 0, "status text: null terminated");
}

static void test_status_text_zero_buffer()
{
    FerromagneticEngine engine;
    engine.init(48000);

    // maxLen=0 should return 0 without writing
    int len = engine.getStatusText(nullptr, 0);
    ASSERT_EQ(len, 0, "status text: maxLen=0 returns 0");
}

static void test_status_text_looptrig_small_buffer()
{
    FerromagneticEngine engine;
    engine.parameterChanged(FerromagneticEngine::kFerroRole, FerromagneticEngine::kRoleLoopTrig);
    engine.parameterChanged(FerromagneticEngine::kFerroLoopSteps, 128);
    engine.init(48000);

    char buf[6]; // "Trig:" = 5 chars + null = 6, no room for "128"
    memset(buf, 'X', sizeof(buf));
    int len = engine.getStatusText(buf, 6);
    ASSERT_TRUE(len <= 5, "status looptrig: fits in maxLen=6");
    ASSERT_TRUE(buf[len] == 0, "status looptrig: null terminated");
}

static void test_octave_spread_zero()
{
    // octaveSpread_=0 should produce no displacement
    FerromagneticEngine engine;
    engine.parameterChanged(FerromagneticEngine::kFerroNoteDensity, 100);
    engine.parameterChanged(FerromagneticEngine::kFerroLoopSteps, 4);
    engine.parameterChanged(FerromagneticEngine::kFerroMaxLayers, 2);
    engine.parameterChanged(FerromagneticEngine::kFerroHarmonyMode, FerromagneticEngine::kHarmonyGenerative);
    engine.parameterChanged(FerromagneticEngine::kFerroOctSpread, 0);
    engine.init(48000);

    // Run through 2 layers x 4 steps = 8 ticks
    // With no scale and numDegrees=7, all pitches should be < 7/12 = 0.583V
    bool allBounded = true;
    for (int i = 0; i < 8; i++) {
        EngineOutput out = engine.clockTick(nullptr);
        if (out.gate > 0.0f && out.pitch > 1.0f) {
            allBounded = false;
        }
    }
    ASSERT_TRUE(allBounded, "octave spread 0: pitches bounded within 1 octave");
}

static void test_octave_spread_max()
{
    // octaveSpread_=3: degree can be picked + 0..3 * numDegrees
    // With numDegrees=7, max degree = 6 + 3*7 = 27, pitch = 27/12 = 2.25V
    // All pitches should remain reasonable (no clamp to 254)
    FerromagneticEngine engine;
    engine.parameterChanged(FerromagneticEngine::kFerroNoteDensity, 100);
    engine.parameterChanged(FerromagneticEngine::kFerroLoopSteps, 16);
    engine.parameterChanged(FerromagneticEngine::kFerroMaxLayers, 4);
    engine.parameterChanged(FerromagneticEngine::kFerroHarmonyMode, FerromagneticEngine::kHarmonyGenerative);
    engine.parameterChanged(FerromagneticEngine::kFerroOctSpread, 3);
    engine.init(48000);

    bool allValid = true;
    for (int i = 0; i < 64; i++) {
        EngineOutput out = engine.clockTick(nullptr);
        // With no scale, pitch = degree/12. Degree <= 27 -> pitch <= 2.25
        // Allow some headroom but flag anything unreasonable
        if (out.gate > 0.0f && out.pitch > 3.0f) {
            allValid = false;
        }
    }
    ASSERT_TRUE(allValid, "octave spread 3: all pitches < 3.0V");
}

static void test_generative_all_degrees_used()
{
    // With maxLayers=8 and numDegrees=7 (no scale), layers 0-6 use all 7 degrees.
    // Layer 7 should get a rest (0xFF) since all degrees are zeroed.
    FerromagneticEngine engine;
    engine.parameterChanged(FerromagneticEngine::kFerroNoteDensity, 100);
    engine.parameterChanged(FerromagneticEngine::kFerroLoopSteps, 2);
    engine.parameterChanged(FerromagneticEngine::kFerroMaxLayers, 8);
    engine.parameterChanged(FerromagneticEngine::kFerroHarmonyMode, FerromagneticEngine::kHarmonyGenerative);
    engine.parameterChanged(FerromagneticEngine::kFerroOctSpread, 0);
    engine.init(48000);

    // Run through all 8 layers x 2 steps = 16 ticks
    // Should not crash even when all degrees are exhausted
    for (int i = 0; i < 16; i++) {
        engine.clockTick(nullptr);
    }
    ASSERT_TRUE(true, "generative 8-layer: no crash when degrees exhausted");
}

static void test_refresh_cycles_through_layers()
{
    // Refresh completion: after all layers built, refreshLayerIdx_ should
    // cycle through 0..maxLayers_-1 without going out of bounds
    FerromagneticEngine engine;
    engine.parameterChanged(FerromagneticEngine::kFerroNoteDensity, 100);
    engine.parameterChanged(FerromagneticEngine::kFerroLoopSteps, 2);
    engine.parameterChanged(FerromagneticEngine::kFerroMaxLayers, 2);
    engine.parameterChanged(FerromagneticEngine::kFerroCompletion, FerromagneticEngine::kCompletionRefresh);
    engine.parameterChanged(FerromagneticEngine::kFerroRefreshRate, 1);
    engine.init(48000);

    // Build 2 layers: 2 layers x 2 steps = 4 ticks
    for (int i = 0; i < 4; i++)
        engine.clockTick(nullptr);

    // Now allLayersComplete_=true. Run many refresh cycles.
    // Each loop wrap with silentLoops_ >= refreshRate_ advances refreshLayerIdx_.
    // Should produce notes without crashing for many cycles.
    bool gotGate = false;
    for (int i = 0; i < 20; i++) {
        EngineOutput out = engine.clockTick(nullptr);
        if (out.gate > 0.0f) gotGate = true;
    }
    ASSERT_TRUE(gotGate, "refresh: produces notes after completion");
}

static void test_single_layer_single_step()
{
    // Extreme minimum: 1 layer, 2 steps (minimum loopSteps)
    FerromagneticEngine engine;
    engine.parameterChanged(FerromagneticEngine::kFerroNoteDensity, 100);
    engine.parameterChanged(FerromagneticEngine::kFerroLoopSteps, 2);
    engine.parameterChanged(FerromagneticEngine::kFerroMaxLayers, 1);
    engine.init(48000);

    // 2 ticks builds layer 0, then complete
    EngineOutput out1 = engine.clockTick(nullptr);
    ASSERT_FLOAT_EQ(out1.gate, 5.0f, "1-layer/2-step: tick 1 has gate");
    EngineOutput out2 = engine.clockTick(nullptr);
    ASSERT_FLOAT_EQ(out2.gate, 5.0f, "1-layer/2-step: tick 2 has gate");
}

static void test_focus_bar_max_layers_1()
{
    // getFocusBarInfo with maxLayers_=1 and count=1: level = 2+13=15
    FerromagneticEngine engine;
    engine.parameterChanged(FerromagneticEngine::kFerroNoteDensity, 100);
    engine.parameterChanged(FerromagneticEngine::kFerroLoopSteps, 4);
    engine.parameterChanged(FerromagneticEngine::kFerroMaxLayers, 1);
    engine.init(48000);

    // Build all notes for layer 0
    for (int i = 0; i < 4; i++)
        engine.clockTick(nullptr);

    FocusBarInfo info;
    engine.getFocusBarInfo(info);
    ASSERT_EQ(info.bars[1].length, 1, "focus bar: maxLayers=1 bar2.length=1");
    // Steps with gates: level = 2 + 1*13/1 = 15
    for (int i = 0; i < 4; i++) {
        ASSERT_TRUE(info.bars[0].levels[i] >= 1, "focus bar: all steps have level >= 1");
    }
}

static void test_refresh_layer_idx_clamp_on_maxlayers_reduce()
{
    // Bug: reducing maxLayers while allLayersComplete_=true and Refresh completion
    // left refreshLayerIdx_ pointing beyond the new maxLayers_.
    FerromagneticEngine engine;
    engine.parameterChanged(FerromagneticEngine::kFerroNoteDensity, 100);
    engine.parameterChanged(FerromagneticEngine::kFerroLoopSteps, 2);
    engine.parameterChanged(FerromagneticEngine::kFerroMaxLayers, 4);
    engine.parameterChanged(FerromagneticEngine::kFerroCompletion, FerromagneticEngine::kCompletionRefresh);
    engine.parameterChanged(FerromagneticEngine::kFerroRefreshRate, 1);
    engine.init(48000);

    // Build 4 layers (4 layers x 2 steps = 8 ticks)
    for (int i = 0; i < 8; i++)
        engine.clockTick(nullptr);

    // allLayersComplete_=true. Advance refreshLayerIdx_ to 3 by cycling through
    // several refresh wraps. Each wrap with silentLoops_ >= 1 advances refreshLayerIdx_.
    // After 1st wrap: refreshLayerIdx_ = 1
    // After 2nd wrap: refreshLayerIdx_ = 2
    // After 3rd wrap: refreshLayerIdx_ = 3
    for (int i = 0; i < 6; i++)
        engine.clockTick(nullptr);  // 3 wraps x 2 steps

    // Now reduce maxLayers to 2. refreshLayerIdx_ was 3, which is out of bounds.
    engine.parameterChanged(FerromagneticEngine::kFerroMaxLayers, 2);

    // The engine should NOT crash or read stale layer data.
    // refreshLayerIdx_ should be clamped to valid range [0, maxLayers_-1].
    bool gotGate = false;
    for (int i = 0; i < 4; i++) {
        EngineOutput out = engine.clockTick(nullptr);
        if (out.gate > 0.0f) gotGate = true;
    }
    ASSERT_TRUE(gotGate, "refresh layer idx clamp: produces notes after maxLayers reduce");
}

static void test_switch_to_new_inversion_while_complete()
{
    // Bug: switching to NewInversion while allLayersComplete_=true caused the
    // engine to get stuck forever (missing case in handleLoopWrap else branch).
    FerromagneticEngine engine;
    engine.parameterChanged(FerromagneticEngine::kFerroNoteDensity, 100);
    engine.parameterChanged(FerromagneticEngine::kFerroLoopSteps, 4);
    engine.parameterChanged(FerromagneticEngine::kFerroMaxLayers, 1);
    engine.parameterChanged(FerromagneticEngine::kFerroCompletion, FerromagneticEngine::kCompletionHold);
    engine.init(48000);

    // Build layer 0 (4 ticks), now allLayersComplete_=true
    for (int i = 0; i < 4; i++)
        engine.clockTick(nullptr);

    // Verify we're in Hold silence
    EngineOutput out = engine.clockTick(nullptr);
    ASSERT_FLOAT_EQ(out.gate, 0.0f, "new-inv switch: Hold silence");

    // Switch to NewInversion while complete
    engine.parameterChanged(FerromagneticEngine::kFerroCompletion, FerromagneticEngine::kCompletionNewInversion);

    // Run through enough ticks that handleLoopWrap fires on a wrap boundary.
    // The engine should resetLoop and start generating notes again.
    int gateCount = 0;
    for (int i = 0; i < 12; i++) {
        out = engine.clockTick(nullptr);
        if (out.gate > 0.0f) gateCount++;
    }
    ASSERT_TRUE(gateCount > 0, "new-inv switch: engine unstuck, generating notes");
}

// --- 13. Edge case boundary tests ---

static void test_max_steps_128_boundary()
{
    // loopSteps=128 means step indices 0-127. step>>5 gives 0-3 for the
    // 4-element layerGates_ bitmask. step=127: 127>>5 = 3, 127&31 = 31.
    // Verify no out-of-bounds access.
    FerromagneticEngine engine;
    engine.parameterChanged(FerromagneticEngine::kFerroNoteDensity, 100);
    engine.parameterChanged(FerromagneticEngine::kFerroLoopSteps, 128);
    engine.parameterChanged(FerromagneticEngine::kFerroMaxLayers, 1);
    engine.init(48000);

    // Run through full 128-step loop
    int gateCount = 0;
    for (int i = 0; i < 128; i++) {
        EngineOutput out = engine.clockTick(nullptr);
        if (out.gate > 0.0f) gateCount++;
    }
    // With density=100, all 128 steps should have gates
    ASSERT_EQ(gateCount, 128, "128 steps: all gated with density=100");
}

static void test_status_text_maxlen_1()
{
    // maxLen=1: should write only the null terminator
    FerromagneticEngine engine;
    engine.parameterChanged(FerromagneticEngine::kFerroRole, FerromagneticEngine::kRoleMelody);
    engine.init(48000);

    char buf[2] = {'X', 'X'};
    int len = engine.getStatusText(buf, 1);
    ASSERT_EQ(len, 0, "status text maxLen=1: returns 0");
    ASSERT_EQ(buf[0], 0, "status text maxLen=1: null terminated");
    // buf[1] should be untouched
    ASSERT_EQ(buf[1], 'X', "status text maxLen=1: doesn't overwrite past buffer");
}

static void test_status_text_melody_maxlen_2()
{
    // maxLen=2: room for 'L' + null
    FerromagneticEngine engine;
    engine.parameterChanged(FerromagneticEngine::kFerroRole, FerromagneticEngine::kRoleMelody);
    engine.init(48000);

    char buf[3] = {'X', 'X', 'X'};
    int len = engine.getStatusText(buf, 2);
    ASSERT_TRUE(len <= 1, "status melody maxLen=2: len <= 1");
    ASSERT_TRUE(buf[len] == 0, "status melody maxLen=2: null terminated");
    ASSERT_EQ(buf[2], 'X', "status melody maxLen=2: no overwrite past buffer");
}

static void test_refresh_single_layer()
{
    // Refresh with maxLayers=1: refreshLayerIdx_ = (0+1)%1 = 0, always layer 0
    FerromagneticEngine engine;
    engine.parameterChanged(FerromagneticEngine::kFerroNoteDensity, 100);
    engine.parameterChanged(FerromagneticEngine::kFerroLoopSteps, 4);
    engine.parameterChanged(FerromagneticEngine::kFerroMaxLayers, 1);
    engine.parameterChanged(FerromagneticEngine::kFerroCompletion, FerromagneticEngine::kCompletionRefresh);
    engine.parameterChanged(FerromagneticEngine::kFerroRefreshRate, 1);
    engine.init(48000);

    // Build 1 layer (4 ticks)
    for (int i = 0; i < 4; i++)
        engine.clockTick(nullptr);

    // Now allLayersComplete_=true. Refresh should cycle through layer 0 only.
    // Should output notes (density=100) without crashing.
    int gateCount = 0;
    for (int i = 0; i < 16; i++) {
        EngineOutput out = engine.clockTick(nullptr);
        if (out.gate > 0.0f) gateCount++;
    }
    ASSERT_TRUE(gateCount > 0, "refresh single layer: produces notes");
}

static void test_rec_gate_decay_rebuild()
{
    // RecGate with DecayRebuild: gate drops to 0 after completion,
    // then goes back to 5V after resetLoop.
    FerromagneticEngine engine;
    engine.parameterChanged(FerromagneticEngine::kFerroRole, FerromagneticEngine::kRoleRecGate);
    engine.parameterChanged(FerromagneticEngine::kFerroLoopSteps, 2);
    engine.parameterChanged(FerromagneticEngine::kFerroMaxLayers, 1);
    engine.parameterChanged(FerromagneticEngine::kFerroCompletion, FerromagneticEngine::kCompletionDecayRebuild);
    engine.parameterChanged(FerromagneticEngine::kFerroRefreshRate, 1);
    engine.init(48000);

    // Build phase: 2 ticks (1 layer x 2 steps)
    EngineOutput out = engine.clockTick(nullptr);
    ASSERT_FLOAT_EQ(out.gate, 5.0f, "rec decay: building gate=5V");
    out = engine.clockTick(nullptr);
    ASSERT_FLOAT_EQ(out.gate, 5.0f, "rec decay: building tick 2 gate=5V");

    // Silent loop 1: gate=0V (2 ticks)
    out = engine.clockTick(nullptr);
    ASSERT_FLOAT_EQ(out.gate, 0.0f, "rec decay: silent gate=0V");
    out = engine.clockTick(nullptr);
    ASSERT_FLOAT_EQ(out.gate, 0.0f, "rec decay: silent tick 2 gate=0V");

    // After 1 silent loop (refreshRate=1), resetLoop fires. Next tick: building again.
    out = engine.clockTick(nullptr);
    ASSERT_FLOAT_EQ(out.gate, 5.0f, "rec decay: rebuild gate=5V");
}

static void test_generative_all_degrees_used_rest_output()
{
    // With maxLayers=8 and numDegrees=7, layer 7 should get a rest
    // because all 7 degrees are exhausted by layers 0-6.
    FerromagneticEngine engine;
    engine.parameterChanged(FerromagneticEngine::kFerroNoteDensity, 100);
    engine.parameterChanged(FerromagneticEngine::kFerroLoopSteps, 2);
    engine.parameterChanged(FerromagneticEngine::kFerroMaxLayers, 8);
    engine.parameterChanged(FerromagneticEngine::kFerroHarmonyMode, FerromagneticEngine::kHarmonyGenerative);
    engine.parameterChanged(FerromagneticEngine::kFerroOctSpread, 0);
    engine.init(48000);

    // Run through 7 layers (14 ticks) -- layers 0-6 use all degrees
    for (int i = 0; i < 14; i++)
        engine.clockTick(nullptr);

    // Layer 7 (2 ticks): should output rests (gate=0) since all degrees used
    int restCount = 0;
    for (int i = 0; i < 2; i++) {
        EngineOutput out = engine.clockTick(nullptr);
        if (out.gate == 0.0f) restCount++;
    }
    ASSERT_EQ(restCount, 2, "generative exhausted: layer 7 all rests");
}

static void test_maxlayers_increase_after_completion()
{
    // After completion, increasing maxLayers shouldn't crash in Refresh mode.
    // Unbuilt layers have gates=0 (from resetLoop), so outputNote returns gate=0.
    FerromagneticEngine engine;
    engine.parameterChanged(FerromagneticEngine::kFerroNoteDensity, 100);
    engine.parameterChanged(FerromagneticEngine::kFerroLoopSteps, 2);
    engine.parameterChanged(FerromagneticEngine::kFerroMaxLayers, 2);
    engine.parameterChanged(FerromagneticEngine::kFerroCompletion, FerromagneticEngine::kCompletionRefresh);
    engine.parameterChanged(FerromagneticEngine::kFerroRefreshRate, 1);
    engine.init(48000);

    // Build 2 layers (4 ticks)
    for (int i = 0; i < 4; i++)
        engine.clockTick(nullptr);

    // Increase maxLayers to 6. Layers 2-5 were never built.
    engine.parameterChanged(FerromagneticEngine::kFerroMaxLayers, 6);

    // Refresh should cycle through layers 0-5 without crashing.
    // Layers 2-5 have no gates, so those ticks output silence.
    for (int i = 0; i < 24; i++)
        engine.clockTick(nullptr);
    ASSERT_TRUE(true, "maxLayers increase after completion: no crash");
}

static void test_loop_trigger_velocity()
{
    // LoopTrig velocity should always be 5.0V
    FerromagneticEngine engine;
    engine.parameterChanged(FerromagneticEngine::kFerroRole, FerromagneticEngine::kRoleLoopTrig);
    engine.parameterChanged(FerromagneticEngine::kFerroLoopSteps, 4);
    engine.init(48000);

    for (int i = 0; i < 4; i++) {
        EngineOutput out = engine.clockTick(nullptr);
        ASSERT_FLOAT_EQ(out.velocity, 5.0f, "loop trig: velocity always 5V");
    }
}

static void test_focus_bar_playhead_tracks_tick()
{
    FerromagneticEngine engine;
    engine.parameterChanged(FerromagneticEngine::kFerroLoopSteps, 8);
    engine.init(48000);

    // After 3 ticks, currentTick_ should be 3
    for (int i = 0; i < 3; i++)
        engine.clockTick(nullptr);

    FocusBarInfo info;
    engine.getFocusBarInfo(info);
    ASSERT_EQ(info.bars[0].playhead, 3, "focus bar: playhead=3 after 3 ticks");
}

// --- 14. Scale-loaded tests ---

// Helper: create a cents-based _NT_sclNote
static _NT_sclNote centNote(double cents)
{
    _NT_sclNote n;
    n.octaves = cents / 1200.0;
    return n;
}

// Build a 7-note major scale (cents-based)
static void makeMajorScale(_NT_sclNote* notes)
{
    // Major scale intervals in cents: 200, 400, 500, 700, 900, 1100, 1200
    static const double cents[] = { 200.0, 400.0, 500.0, 700.0, 900.0, 1100.0, 1200.0 };
    for (int i = 0; i < 7; ++i)
        notes[i] = centNote(cents[i]);
}

// Build a 5-note pentatonic scale (cents-based)
static void makePentatonicScale(_NT_sclNote* notes)
{
    // Major pentatonic: 200, 400, 700, 900, 1200 cents
    static const double cents[] = { 200.0, 400.0, 700.0, 900.0, 1200.0 };
    for (int i = 0; i < 5; ++i)
        notes[i] = centNote(cents[i]);
}

static void test_scale_loaded_melody_pitches()
{
    // With a loaded scale, pitches should come from scale->quantize(),
    // not the chromatic fallback (degree/12.0).
    FerromagneticEngine engine;
    engine.parameterChanged(FerromagneticEngine::kFerroNoteDensity, 100);
    engine.parameterChanged(FerromagneticEngine::kFerroLoopSteps, 4);
    engine.parameterChanged(FerromagneticEngine::kFerroMaxLayers, 1);
    engine.init(48000);

    _NT_sclNote notes[7];
    makeMajorScale(notes);
    ScaleQuantizer scale;
    scale.loadScale(notes, 7);

    // Generate a full loop with scale
    for (int i = 0; i < 4; i++) {
        EngineOutput out = engine.clockTick(&scale);
        ASSERT_FLOAT_EQ(out.gate, 5.0f, "scale melody: gate=5V");
        // With a 7-note major scale, degrees are 0-6.
        // Pitch should match scale->quantize(degree, 0, 0) for some degree.
        // Just verify pitch is non-negative and gate is present.
        ASSERT_TRUE(out.pitch >= 0.0f, "scale melody: pitch >= 0");
    }
}

static void test_scale_loaded_structured_intervals()
{
    // Structured harmony with a loaded scale should produce pitches that
    // differ by the voicing interval applied through the scale quantizer.
    FerromagneticEngine engine;
    engine.parameterChanged(FerromagneticEngine::kFerroNoteDensity, 100);
    engine.parameterChanged(FerromagneticEngine::kFerroLoopSteps, 4);
    engine.parameterChanged(FerromagneticEngine::kFerroMaxLayers, 2);
    engine.parameterChanged(FerromagneticEngine::kFerroHarmonyMode, FerromagneticEngine::kHarmonyStructured);
    engine.parameterChanged(FerromagneticEngine::kFerroVoicing, FerromagneticEngine::kVoicingTriads);
    engine.init(48000);

    _NT_sclNote notes[7];
    makeMajorScale(notes);
    ScaleQuantizer scale;
    scale.loadScale(notes, 7);

    // Layer 0
    float layer0pitch[4];
    for (int i = 0; i < 4; i++) {
        EngineOutput out = engine.clockTick(&scale);
        layer0pitch[i] = out.pitch;
    }

    // Layer 1 (Triads: +2 scale degrees above layer 0)
    float layer1pitch[4];
    for (int i = 0; i < 4; i++) {
        EngineOutput out = engine.clockTick(&scale);
        layer1pitch[i] = out.pitch;
    }

    // Layer 1 should differ from layer 0 (interval of 2 scale degrees)
    for (int i = 0; i < 4; i++) {
        ASSERT_TRUE(layer1pitch[i] > layer0pitch[i],
            "scale structured: layer 1 pitch > layer 0 pitch");
    }
}

static void test_scale_loaded_generative()
{
    // Generative mode with a loaded scale should produce valid pitches.
    FerromagneticEngine engine;
    engine.parameterChanged(FerromagneticEngine::kFerroNoteDensity, 100);
    engine.parameterChanged(FerromagneticEngine::kFerroLoopSteps, 4);
    engine.parameterChanged(FerromagneticEngine::kFerroMaxLayers, 3);
    engine.parameterChanged(FerromagneticEngine::kFerroHarmonyMode, FerromagneticEngine::kHarmonyGenerative);
    engine.parameterChanged(FerromagneticEngine::kFerroOctSpread, 0);
    engine.init(48000);

    _NT_sclNote notes[7];
    makeMajorScale(notes);
    ScaleQuantizer scale;
    scale.loadScale(notes, 7);

    // Build all 3 layers (12 ticks total)
    for (int i = 0; i < 12; i++) {
        EngineOutput out = engine.clockTick(&scale);
        if (out.gate > 0.0f) {
            ASSERT_TRUE(out.pitch >= 0.0f, "scale generative: pitch >= 0");
            ASSERT_TRUE(out.midiNote <= 127, "scale generative: midiNote <= 127");
        }
    }
}

static void test_scale_triggers_reset()
{
    // When a scale first becomes available, the engine should reset.
    FerromagneticEngine engine;
    engine.parameterChanged(FerromagneticEngine::kFerroNoteDensity, 100);
    engine.parameterChanged(FerromagneticEngine::kFerroLoopSteps, 8);
    engine.parameterChanged(FerromagneticEngine::kFerroMaxLayers, 2);
    engine.init(48000);

    // Clock a few ticks without scale
    for (int i = 0; i < 3; i++)
        engine.clockTick(nullptr);
    ASSERT_EQ(engine.currentStep(), 3, "pre-scale: tick=3");

    // Now provide a scale -- should trigger reset
    _NT_sclNote notes[7];
    makeMajorScale(notes);
    ScaleQuantizer scale;
    scale.loadScale(notes, 7);

    engine.clockTick(&scale);
    // After reset, tick advances from 0 to 1
    ASSERT_EQ(engine.currentStep(), 1, "scale load: reset then tick=1");
}

static void test_pentatonic_scale_degrees()
{
    // A 5-note scale should use numDegrees_=5, not the default 7.
    FerromagneticEngine engine;
    engine.parameterChanged(FerromagneticEngine::kFerroNoteDensity, 100);
    engine.parameterChanged(FerromagneticEngine::kFerroLoopSteps, 4);
    engine.parameterChanged(FerromagneticEngine::kFerroMaxLayers, 1);
    engine.init(48000);

    _NT_sclNote notes[5];
    makePentatonicScale(notes);
    ScaleQuantizer scale;
    scale.loadScale(notes, 5);

    // Should not crash and should produce valid output
    for (int i = 0; i < 4; i++) {
        EngineOutput out = engine.clockTick(&scale);
        ASSERT_FLOAT_EQ(out.gate, 5.0f, "pentatonic: gate=5V");
        ASSERT_TRUE(out.pitch >= 0.0f, "pentatonic: pitch >= 0");
    }
}

static void test_scale_new_inversion_with_scale()
{
    // New Inversion with a loaded scale should cycle through inversions
    // without crashing or producing extreme pitches.
    FerromagneticEngine engine;
    engine.parameterChanged(FerromagneticEngine::kFerroNoteDensity, 100);
    engine.parameterChanged(FerromagneticEngine::kFerroLoopSteps, 2);
    engine.parameterChanged(FerromagneticEngine::kFerroMaxLayers, 1);
    engine.parameterChanged(FerromagneticEngine::kFerroCompletion, FerromagneticEngine::kCompletionNewInversion);
    engine.init(48000);

    _NT_sclNote notes[7];
    makeMajorScale(notes);
    ScaleQuantizer scale;
    scale.loadScale(notes, 7);

    // Run through 20 inversion cycles (40 ticks)
    for (int cycle = 0; cycle < 20; cycle++) {
        for (int t = 0; t < 2; t++) {
            EngineOutput out = engine.clockTick(&scale);
            if (out.gate > 0.0f) {
                // Pitch should stay reasonable (within a few octaves)
                ASSERT_TRUE(out.pitch < 5.0f, "scale inversion: pitch < 5V");
                ASSERT_TRUE(out.pitch >= -1.0f, "scale inversion: pitch >= -1V");
            }
        }
    }
}

static void test_scale_loaded_midi_notes()
{
    // With a loaded scale, midiNote should come from scaleDegreeToMidi.
    FerromagneticEngine engine;
    engine.parameterChanged(FerromagneticEngine::kFerroNoteDensity, 100);
    engine.parameterChanged(FerromagneticEngine::kFerroLoopSteps, 4);
    engine.parameterChanged(FerromagneticEngine::kFerroMaxLayers, 1);
    engine.init(48000);

    _NT_sclNote notes[7];
    makeMajorScale(notes);
    ScaleQuantizer scale;
    scale.loadScale(notes, 7);

    for (int i = 0; i < 4; i++) {
        EngineOutput out = engine.clockTick(&scale);
        ASSERT_TRUE(out.midiNote <= 127, "scale midi: note <= 127");
        ASSERT_TRUE(out.midiNote >= 0, "scale midi: note >= 0");
    }
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

    // 11. Edge case data-flow tests
    test_status_text_small_buffer();
    test_status_text_zero_buffer();
    test_status_text_looptrig_small_buffer();
    test_octave_spread_zero();
    test_octave_spread_max();
    test_generative_all_degrees_used();
    test_refresh_cycles_through_layers();
    test_single_layer_single_step();
    test_focus_bar_max_layers_1();

    // 12. Completion mode switching
    test_refresh_layer_idx_clamp_on_maxlayers_reduce();
    test_switch_to_new_inversion_while_complete();

    // 13. Edge case boundary tests
    test_max_steps_128_boundary();
    test_status_text_maxlen_1();
    test_status_text_melody_maxlen_2();
    test_refresh_single_layer();
    test_rec_gate_decay_rebuild();
    test_generative_all_degrees_used_rest_output();
    test_maxlayers_increase_after_completion();
    test_loop_trigger_velocity();
    test_focus_bar_playhead_tracks_tick();

    // 14. Scale-loaded tests
    test_scale_loaded_melody_pitches();
    test_scale_loaded_structured_intervals();
    test_scale_loaded_generative();
    test_scale_triggers_reset();
    test_pentatonic_scale_degrees();
    test_scale_new_inversion_with_scale();
    test_scale_loaded_midi_notes();

    printf("%d tests, %d failures\n", tests, failures);
    return failures ? 1 : 0;
}
