// Unit tests for QuantumEngine (hierarchical composition architecture).
// Standalone compilation with NT API stubs.

#define private public
#define protected public

#include "nt_stubs.h"

#include "../scale/ScaleQuantizer.cpp"
#include "../engines/QuantumEngine.cpp"

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

// Helper to build a 12-TET scale
static ScaleQuantizer make12TET()
{
    ScaleQuantizer sq;
    _NT_sclNote notes[12];
    for (int i = 0; i < 12; ++i) {
        notes[i].octaves = (double)(i + 1) * 100.0 / 1200.0;
    }
    sq.loadScale(notes, 12);
    return sq;
}

// Helper to build a 7-note major scale
static ScaleQuantizer makeMajor()
{
    ScaleQuantizer sq;
    double cents[] = {200, 400, 500, 700, 900, 1100, 1200};
    _NT_sclNote notes[7];
    for (int i = 0; i < 7; ++i) {
        notes[i].octaves = cents[i] / 1200.0;
    }
    sq.loadScale(notes, 7);
    return sq;
}

// --- 1. Lifecycle ---

static void test_constructor_defaults()
{
    QuantumEngine engine;
    ASSERT_EQ(engine.currentStep(), 0, "constructor: step=0");
    ASSERT_EQ(engine.sequenceLength(), 8, "constructor: motifLen=8");
}

static void test_init_generates_motif()
{
    QuantumEngine engine;
    engine.init(48000);
    // Motif should be populated (not all zeros after contour generation)
    bool hasNonZero = false;
    for (int i = 0; i < 8; i++) {
        if (engine.motif_[i] != 0) { hasNonZero = true; break; }
    }
    // With Arch contour and range=5, middle notes should be non-zero
    ASSERT_TRUE(hasNonZero, "init: motif has non-zero notes");
}

static void test_reset_returns_to_start()
{
    QuantumEngine engine;
    engine.init(48000);
    for (int i = 0; i < 20; i++)
        engine.clockTick(nullptr);
    ASSERT_TRUE(engine.currentStep() > 0 || engine.motifCycles_ > 0, "pre-reset: advanced");
    engine.reset();
    ASSERT_EQ(engine.currentStep(), 0, "reset: step=0");
    ASSERT_EQ(engine.motifCycles_, 0, "reset: cycles=0");
    ASSERT_EQ(engine.transpose_, 0, "reset: transpose=0");
}

static void test_parameter_defs_count()
{
    QuantumEngine engine;
    _NT_parameter defs[32];
    int count = engine.getParameterDefs(defs);
    ASSERT_EQ(count, QuantumEngine::kNumQuantumParams, "getParameterDefs count");
    ASSERT_EQ(count, 10, "kNumQuantumParams == 10");
}

static void test_name()
{
    QuantumEngine engine;
    ASSERT_TRUE(strcmp(engine.name(), "Quantum") == 0, "name() == Quantum");
}

// --- 2. Motif playback ---

static void test_motif_loops()
{
    QuantumEngine engine;
    engine.init(48000);
    engine.parameterChanged(QuantumEngine::kQMotifLen, 4);
    engine.parameterChanged(QuantumEngine::kQMEvery, 8); // delay transforms

    // Clock 4 ticks = 1 full motif
    for (int i = 0; i < 4; i++)
        engine.clockTick(nullptr);
    ASSERT_EQ(engine.stepInMotif_, 0, "motif wraps after motifLen ticks");
    ASSERT_EQ(engine.motifCycles_, 1, "motif cycle incremented");
}

static void test_motif_repeats_same_notes()
{
    QuantumEngine engine;
    ScaleQuantizer sq = makeMajor();
    engine.init(48000);
    engine.parameterChanged(QuantumEngine::kQMotifLen, 4);
    engine.parameterChanged(QuantumEngine::kQGateDensity, 100); // all gates on
    engine.parameterChanged(QuantumEngine::kQMEvery, 8); // delay M transform

    // Record first cycle
    uint8_t firstCycle[4];
    for (int i = 0; i < 4; i++) {
        EngineOutput out = engine.clockTick(&sq);
        firstCycle[i] = out.midiNote;
    }

    // Record second cycle - should be identical (no transform yet)
    for (int i = 0; i < 4; i++) {
        EngineOutput out = engine.clockTick(&sq);
        ASSERT_EQ(out.midiNote, firstCycle[i], "motif repeats: same notes");
    }
}

static void test_gate_density()
{
    QuantumEngine engine;
    engine.init(48000);
    engine.parameterChanged(QuantumEngine::kQMotifLen, 8);
    engine.parameterChanged(QuantumEngine::kQGateDensity, 50);

    // Count gates in the motif
    int gateCount = 0;
    for (int i = 0; i < 8; i++) {
        if (engine.gates_[i]) gateCount++;
    }
    // 50% of 8 = 4 gates
    ASSERT_EQ(gateCount, 4, "gate density 50% = 4/8 gates");
}

static void test_step_0_always_has_gate()
{
    QuantumEngine engine;
    engine.init(48000);
    engine.parameterChanged(QuantumEngine::kQMotifLen, 8);
    engine.parameterChanged(QuantumEngine::kQGateDensity, 25); // minimum

    // Step 0 (downbeat) should always have a gate
    ASSERT_TRUE(engine.gates_[0], "downbeat always has gate");
}

// --- 3. Contour shapes ---

static void test_contour_arch()
{
    QuantumEngine engine;
    engine.init(48000);
    engine.parameterChanged(QuantumEngine::kQContour, QuantumEngine::kContourArch);
    engine.parameterChanged(QuantumEngine::kQRange, 7);
    engine.parameterChanged(QuantumEngine::kQMotifLen, 8);

    // Regenerate
    engine.generateMotif(nullptr);

    // Arch: middle notes should be higher than endpoints
    int startEnd = engine.motif_[0] + engine.motif_[7];
    int middle = engine.motif_[3] + engine.motif_[4];
    ASSERT_TRUE(middle >= startEnd, "arch: middle >= endpoints");
}

static void test_contour_ascend()
{
    QuantumEngine engine;
    engine.init(48000);
    engine.parameterChanged(QuantumEngine::kQContour, QuantumEngine::kContourAscend);
    engine.parameterChanged(QuantumEngine::kQRange, 8);
    engine.parameterChanged(QuantumEngine::kQMotifLen, 8);

    engine.generateMotif(nullptr);

    // Ascending: last note should generally be higher than first
    ASSERT_TRUE(engine.motif_[7] >= engine.motif_[0], "ascend: end >= start");
}

// --- 4. M Layer transformations ---

static void test_m_transpose()
{
    QuantumEngine engine;
    ScaleQuantizer sq = makeMajor();
    engine.init(48000);
    engine.parameterChanged(QuantumEngine::kQMotifLen, 4);
    engine.parameterChanged(QuantumEngine::kQMEvery, 1); // transform every cycle
    engine.parameterChanged(QuantumEngine::kQMTransform, QuantumEngine::kMTranspose);
    engine.parameterChanged(QuantumEngine::kQLEvery, 16); // delay L

    // Play one cycle
    for (int i = 0; i < 4; i++)
        engine.clockTick(&sq);

    // After first cycle, M should transpose
    ASSERT_TRUE(engine.transpose_ != 0, "M transpose: offset changed");
}

static void test_m_invert()
{
    QuantumEngine engine;
    engine.init(48000);
    engine.parameterChanged(QuantumEngine::kQMotifLen, 4);
    engine.parameterChanged(QuantumEngine::kQRange, 7);
    engine.parameterChanged(QuantumEngine::kQMEvery, 1);
    engine.parameterChanged(QuantumEngine::kQMTransform, QuantumEngine::kMInvert);
    engine.parameterChanged(QuantumEngine::kQLEvery, 16);

    // Record original motif
    int original[4];
    for (int i = 0; i < 4; i++)
        original[i] = engine.motif_[i];

    // Play one cycle to trigger M
    for (int i = 0; i < 4; i++)
        engine.clockTick(nullptr);

    // After invert, notes should be mirrored: motif[i] = range-1 - original[i]
    for (int i = 0; i < 4; i++) {
        ASSERT_EQ(engine.motif_[i], 6 - original[i], "M invert: mirrored");
    }
}

static void test_m_rotate()
{
    QuantumEngine engine;
    engine.init(48000);
    engine.parameterChanged(QuantumEngine::kQMotifLen, 4);
    engine.parameterChanged(QuantumEngine::kQMEvery, 1);
    engine.parameterChanged(QuantumEngine::kQMTransform, QuantumEngine::kMRotate);
    engine.parameterChanged(QuantumEngine::kQLEvery, 16);

    int original[4];
    for (int i = 0; i < 4; i++)
        original[i] = engine.motif_[i];

    // Play one cycle
    for (int i = 0; i < 4; i++)
        engine.clockTick(nullptr);

    // Motif should be rotated (shifted by 1 or 2)
    // Check that it's different but contains same elements
    bool changed = false;
    for (int i = 0; i < 4; i++) {
        if (engine.motif_[i] != original[i]) { changed = true; break; }
    }
    ASSERT_TRUE(changed, "M rotate: pattern changed");
}

// --- 5. L Layer actions ---

static void test_l_new_motif()
{
    QuantumEngine engine;
    engine.init(48000);
    engine.parameterChanged(QuantumEngine::kQMotifLen, 4);
    engine.parameterChanged(QuantumEngine::kQMEvery, 1);
    engine.parameterChanged(QuantumEngine::kQLEvery, 2);
    engine.parameterChanged(QuantumEngine::kQLAction, QuantumEngine::kLNewMotif);

    int original[4];
    for (int i = 0; i < 4; i++)
        original[i] = engine.motif_[i];

    // Play 2 M cycles to trigger L (mEvery=1, lEvery=2 → after 2 motif cycles)
    for (int i = 0; i < 8; i++)
        engine.clockTick(nullptr);

    // Transpose should be reset
    ASSERT_EQ(engine.transpose_, 0, "L new motif: transpose reset");
}

static void test_l_octave_shift()
{
    QuantumEngine engine;
    engine.init(48000);
    engine.parameterChanged(QuantumEngine::kQMotifLen, 4);
    engine.parameterChanged(QuantumEngine::kQMEvery, 1);
    engine.parameterChanged(QuantumEngine::kQLEvery, 2);
    engine.parameterChanged(QuantumEngine::kQLAction, QuantumEngine::kLOctaveShift);

    // Play enough for L to trigger
    for (int i = 0; i < 8; i++)
        engine.clockTick(nullptr);

    ASSERT_TRUE(engine.octaveShift_ != 0, "L octave shift: shifted");
}

static void test_l_reverse()
{
    QuantumEngine engine;
    engine.init(48000);
    engine.parameterChanged(QuantumEngine::kQMotifLen, 4);
    engine.parameterChanged(QuantumEngine::kQMEvery, 1);
    engine.parameterChanged(QuantumEngine::kQLEvery, 2);
    engine.parameterChanged(QuantumEngine::kQLAction, QuantumEngine::kLReverse);
    engine.parameterChanged(QuantumEngine::kQMTransform, QuantumEngine::kMPermute); // mild M

    int original[4];
    for (int i = 0; i < 4; i++)
        original[i] = engine.motif_[i];

    // Play to trigger L
    for (int i = 0; i < 8; i++)
        engine.clockTick(nullptr);

    // After reverse + possible permute, check it changed
    bool changed = false;
    for (int i = 0; i < 4; i++) {
        if (engine.motif_[i] != original[i]) { changed = true; break; }
    }
    ASSERT_TRUE(changed, "L reverse: motif changed");
}

// --- 6. Scale integration ---

static void test_scale_quantization()
{
    QuantumEngine engine;
    ScaleQuantizer sq = makeMajor();
    engine.init(48000);
    engine.parameterChanged(QuantumEngine::kQGateDensity, 100);

    EngineOutput out = engine.clockTick(&sq);
    // With major scale, MIDI note should be valid
    ASSERT_TRUE(out.midiNote > 0 && out.midiNote < 127, "scale: valid MIDI");
}

static void test_no_scale_chromatic()
{
    QuantumEngine engine;
    engine.init(48000);
    engine.parameterChanged(QuantumEngine::kQGateDensity, 100);

    EngineOutput out = engine.clockTick(nullptr);
    ASSERT_TRUE(out.midiNote >= 0 && out.midiNote <= 127, "no-scale: valid MIDI");
}

static void test_scale_change_regenerates()
{
    QuantumEngine engine;
    ScaleQuantizer sq = make12TET();
    engine.init(48000);
    engine.clockTick(nullptr);
    ASSERT_TRUE(!engine.scaleLoaded_, "before scale: not loaded");
    engine.clockTick(&sq);
    ASSERT_TRUE(engine.scaleLoaded_, "after scale: loaded");
}

// --- 7. Gate and timed gate ---

static void test_uses_timed_gate()
{
    QuantumEngine engine;
    ASSERT_TRUE(engine.usesTimedGate(), "usesTimedGate returns true");
}

static void test_gate_length_param()
{
    QuantumEngine engine;
    engine.parameterChanged(QuantumEngine::kQGateLen, 60);
    ASSERT_EQ(engine.gateLengthPercent(), 60, "gateLengthPercent matches");
}

static void test_gate_follows_pattern()
{
    QuantumEngine engine;
    engine.init(48000);
    engine.parameterChanged(QuantumEngine::kQMotifLen, 4);
    engine.parameterChanged(QuantumEngine::kQGateDensity, 50);

    // Count gates vs rests over one cycle
    int gates = 0, rests = 0;
    for (int i = 0; i < 4; i++) {
        EngineOutput out = engine.clockTick(nullptr);
        if (out.gate > 0.0f) gates++;
        else rests++;
    }
    ASSERT_EQ(gates, 2, "50% density: 2 gates in 4 steps");
    ASSERT_EQ(rests, 2, "50% density: 2 rests in 4 steps");
}

// --- 8. Focus display ---

static void test_focus_bar()
{
    QuantumEngine engine;
    engine.init(48000);

    FocusBarInfo info;
    engine.getFocusBarInfo(info);
    ASSERT_EQ(info.numBars, 2, "focus: 2 bars");
    ASSERT_EQ(info.bars[0].length, 8, "focus bar 1 length = motifLen");
    ASSERT_EQ(info.bars[0].playhead, 0, "focus bar 1 playhead at 0");
    ASSERT_EQ(info.bars[1].length, 4, "focus bar 2 length = mEvery default");
    ASSERT_EQ(info.bars[1].playhead, -1, "focus bar 2 no playhead");
}

static void test_focus_detail()
{
    QuantumEngine engine;
    engine.init(48000);
    engine.clockTick(nullptr);

    FocusDetail detail;
    engine.getFocusDetail(detail);
    ASSERT_TRUE(detail.lines[0].len > 0, "focus line 1 has content");
    ASSERT_TRUE(detail.lines[1].len > 0, "focus line 2 has content");
}

// --- 9. Size ---

static void test_sizeof_under_2048()
{
    ASSERT_TRUE(sizeof(QuantumEngine) <= 2048, "sizeof <= 2048");
    printf("    sizeof(QuantumEngine) = %zu bytes\n", sizeof(QuantumEngine));
}

// --- Main ---

int main()
{
    printf("=== QuantumEngine Unit Tests ===\n");

    // 1. Lifecycle
    test_constructor_defaults();
    test_init_generates_motif();
    test_reset_returns_to_start();
    test_parameter_defs_count();
    test_name();

    // 2. Motif playback
    test_motif_loops();
    test_motif_repeats_same_notes();
    test_gate_density();
    test_step_0_always_has_gate();

    // 3. Contour
    test_contour_arch();
    test_contour_ascend();

    // 4. M Layer
    test_m_transpose();
    test_m_invert();
    test_m_rotate();

    // 5. L Layer
    test_l_new_motif();
    test_l_octave_shift();
    test_l_reverse();

    // 6. Scale
    test_scale_quantization();
    test_no_scale_chromatic();
    test_scale_change_regenerates();

    // 7. Gate
    test_uses_timed_gate();
    test_gate_length_param();
    test_gate_follows_pattern();

    // 8. Focus
    test_focus_bar();
    test_focus_detail();

    // 9. Size
    test_sizeof_under_2048();

    printf("\n%d tests, %d failures\n", tests, failures);
    return failures ? 1 : 0;
}
