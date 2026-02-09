// Unit tests for ThorpEngine: velocity, chain building, song mode,
// slot save/load, MIDI input, clockTick, and reset.
// Standalone compilation with NT API stubs — no NT runtime dependency.

#include "nt_stubs.h"

// Expose private/protected members for direct testing
#define private public
#define protected public

#include "../scale/ScaleQuantizer.h"
#include "../scale/ScaleQuantizer.cpp"
#include "../engines/ThorpEngine.h"
#include "../engines/ThorpEngine.cpp"

#undef private
#undef protected

static int failures = 0;
static int tests = 0;

#define ASSERT_TRUE(cond, msg) do { \
    tests++; \
    if (!(cond)) { \
        fprintf(stderr, "FAIL [%s:%d]: %s\n", __func__, __LINE__, msg); \
        failures++; \
    } \
} while (0)

#define ASSERT_EQ(a, b, msg) do { \
    tests++; \
    if ((a) != (b)) { \
        fprintf(stderr, "FAIL [%s:%d]: %s: expected %d, got %d\n", \
                __func__, __LINE__, msg, (int)(b), (int)(a)); \
        failures++; \
    } \
} while (0)

#define ASSERT_FLOAT_EQ(a, b, msg) do { \
    tests++; \
    if (fabsf((a) - (b)) > 0.001f) { \
        fprintf(stderr, "FAIL [%s:%d]: %s: expected %.4f, got %.4f\n", \
                __func__, __LINE__, msg, (double)(b), (double)(a)); \
        failures++; \
    } \
} while (0)

// ---------------------------------------------------------------------------
// Helper: create an engine with standard init, Jam mode, gateLen < 100
// ---------------------------------------------------------------------------
static ThorpEngine makeEngine()
{
    ThorpEngine e;
    e.init(48000);
    return e;
}

// ===== Velocity tests (ported from test_thorp_velocity.cpp) ================

static void test_constant_pattern()
{
    ThorpEngine engine = makeEngine();
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
    ASSERT_FLOAT_EQ(first, 2.5f, "Constant pattern velocity = 50 * 0.05 = 2.5V");
}

static void test_accent_pattern()
{
    ThorpEngine engine = makeEngine();
    engine.parameterChanged(ThorpEngine::kThorpVelPattern, 1); // Accent
    engine.noteOn(60, 100);

    float velocities[8];
    for (int i = 0; i < 8; i++) {
        EngineOutput eo = engine.clockTick(nullptr);
        velocities[i] = eo.velocity;
    }

    // Accent pattern: 75, 35, 40, 35, 75, 35, 40, 35
    // Expected: value * 0.05
    ASSERT_FLOAT_EQ(velocities[0], 3.75f, "Accent step 0 = 75*0.05");
    ASSERT_FLOAT_EQ(velocities[1], 1.75f, "Accent step 1 = 35*0.05");
    ASSERT_FLOAT_EQ(velocities[2], 2.00f, "Accent step 2 = 40*0.05");
    ASSERT_FLOAT_EQ(velocities[3], 1.75f, "Accent step 3 = 35*0.05");
    ASSERT_FLOAT_EQ(velocities[4], 3.75f, "Accent step 4 = 75*0.05");
    ASSERT_FLOAT_EQ(velocities[5], 1.75f, "Accent step 5 = 35*0.05");
    ASSERT_FLOAT_EQ(velocities[6], 2.00f, "Accent step 6 = 40*0.05");
    ASSERT_FLOAT_EQ(velocities[7], 1.75f, "Accent step 7 = 35*0.05");
}

static void test_pattern_change()
{
    ThorpEngine engine = makeEngine();
    engine.parameterChanged(ThorpEngine::kThorpVelPattern, 0); // Constant
    engine.noteOn(60, 100);

    EngineOutput eo1 = engine.clockTick(nullptr);
    ASSERT_FLOAT_EQ(eo1.velocity, 2.5f, "Constant pattern = 2.5V");

    // Switch to Accent mid-stream
    engine.parameterChanged(ThorpEngine::kThorpVelPattern, 1);
    EngineOutput eo2 = engine.clockTick(nullptr);
    // velStepCount is now 1, so velStepIndex=1, Accent[1]=35 -> 1.75
    ASSERT_FLOAT_EQ(eo2.velocity, 1.75f, "After switch to Accent, step 1 = 1.75V");
}

static void test_global_velocity_scaling()
{
    ThorpEngine engine = makeEngine();
    engine.parameterChanged(ThorpEngine::kThorpVelPattern, 0); // Constant (50)
    engine.parameterChanged(ThorpEngine::kThorpGlobalVelocity, 50); // 50%
    engine.noteOn(60, 100);

    EngineOutput eo = engine.clockTick(nullptr);
    // 50 * 50 / 100 = 25, 25 * 0.05 = 1.25
    ASSERT_FLOAT_EQ(eo.velocity, 1.25f, "Global vel 50% halves the output");
}

static void test_velocity_across_notes()
{
    ThorpEngine engine = makeEngine();
    engine.parameterChanged(ThorpEngine::kThorpVelPattern, 1); // Accent
    engine.parameterChanged(ThorpEngine::kThorpGateLen, 50);
    engine.noteOn(60, 100);
    engine.noteOn(64, 100);

    float velocities[16];
    for (int i = 0; i < 16; i++) {
        EngineOutput eo = engine.clockTick(nullptr);
        velocities[i] = eo.velocity;
    }

    // Accent pattern repeats every 8 steps
    for (int i = 0; i < 8; i++) {
        ASSERT_FLOAT_EQ(velocities[i], velocities[i + 8],
            "Velocity pattern should repeat after 8 steps");
    }

    // Verify variation exists
    bool varies = false;
    for (int i = 1; i < 8; i++) {
        if (fabsf(velocities[i] - velocities[0]) > 0.001f) {
            varies = true;
            break;
        }
    }
    ASSERT_TRUE(varies, "Accent pattern should vary across 16 notes");
}

// ===== Chain building ======================================================

static void test_chain_build()
{
    ThorpEngine engine = makeEngine();

    // Build chain [A=3, B=7, C=12] (1-indexed slot numbers)
    // Step 1: chainLen=1, set arpSlot=3 -> writes slot 2 into chain_[0]
    engine.parameterChanged(ThorpEngine::kThorpChainLen, 1);
    engine.parameterChanged(ThorpEngine::kThorpArpSlot, 3);

    // Step 2: chainLen=2, set arpSlot=7 -> writes slot 6 into chain_[1]
    engine.parameterChanged(ThorpEngine::kThorpChainLen, 2);
    engine.parameterChanged(ThorpEngine::kThorpArpSlot, 7);

    // Step 3: chainLen=3, set arpSlot=12 -> writes slot 11 into chain_[2]
    engine.parameterChanged(ThorpEngine::kThorpChainLen, 3);
    engine.parameterChanged(ThorpEngine::kThorpArpSlot, 12);

    // uiChainSlotAt returns 1-indexed slot numbers
    ASSERT_EQ(engine.uiChainLength(), 3, "chain length = 3");
    ASSERT_EQ(engine.uiChainSlotAt(0), 3, "chain[0] = slot 3");
    ASSERT_EQ(engine.uiChainSlotAt(1), 7, "chain[1] = slot 7");
    ASSERT_EQ(engine.uiChainSlotAt(2), 12, "chain[2] = slot 12");
}

// ===== Song mode Seq advancement ===========================================

static void test_song_seq_mode()
{
    ThorpEngine engine = makeEngine();

    // Build a 3-slot chain [1,2,3]
    engine.parameterChanged(ThorpEngine::kThorpChainLen, 1);
    engine.parameterChanged(ThorpEngine::kThorpArpSlot, 1);
    engine.parameterChanged(ThorpEngine::kThorpChainLen, 2);
    engine.parameterChanged(ThorpEngine::kThorpArpSlot, 2);
    engine.parameterChanged(ThorpEngine::kThorpChainLen, 3);
    engine.parameterChanged(ThorpEngine::kThorpArpSlot, 3);

    // Add notes so clockTick produces output
    engine.noteOn(60, 100);

    // Switch to Song mode (resets chainPos to 0, localStep to 0)
    engine.parameterChanged(ThorpEngine::kThorpPlayMode, ThorpEngine::kPlaySong);
    engine.parameterChanged(ThorpEngine::kThorpSequenceMode, ThorpEngine::kModeSeq);

    // Default slot length is 8. Clock through and track chain positions.
    // chainPos is observed via uiPlayingChainPos(). The position changes
    // AFTER the last step of a slot completes (advanceSongChain called
    // when localStep_ wraps).
    //
    // Ticks 0-7: chainPos=0 (slot length 8)
    // Ticks 8-15: chainPos=1
    // Ticks 16-23: chainPos=2
    // Ticks 24-31: chainPos=0 (wraps)

    for (int tick = 0; tick < 8; tick++) {
        ASSERT_EQ(engine.uiPlayingChainPos(), 0, "ticks 0-7: chainPos=0");
        engine.clockTick(nullptr);
    }
    for (int tick = 0; tick < 8; tick++) {
        ASSERT_EQ(engine.uiPlayingChainPos(), 1, "ticks 8-15: chainPos=1");
        engine.clockTick(nullptr);
    }
    for (int tick = 0; tick < 8; tick++) {
        ASSERT_EQ(engine.uiPlayingChainPos(), 2, "ticks 16-23: chainPos=2");
        engine.clockTick(nullptr);
    }
    // Should have wrapped back to 0
    ASSERT_EQ(engine.uiPlayingChainPos(), 0, "tick 24: chainPos wraps to 0");
}

static void test_song_single_slot()
{
    ThorpEngine engine = makeEngine();

    engine.parameterChanged(ThorpEngine::kThorpChainLen, 1);
    engine.parameterChanged(ThorpEngine::kThorpArpSlot, 1);
    engine.noteOn(60, 100);
    engine.parameterChanged(ThorpEngine::kThorpPlayMode, ThorpEngine::kPlaySong);

    // With only 1 slot in the chain, chainPos should stay 0 forever
    for (int tick = 0; tick < 24; tick++) {
        engine.clockTick(nullptr);
        ASSERT_EQ(engine.uiPlayingChainPos(), 0, "single slot: chainPos stays 0");
    }
}

// ===== Song mode PingPong ==================================================

static void test_song_pingpong()
{
    ThorpEngine engine = makeEngine();

    // Build chain [1,2,3]
    engine.parameterChanged(ThorpEngine::kThorpChainLen, 1);
    engine.parameterChanged(ThorpEngine::kThorpArpSlot, 1);
    engine.parameterChanged(ThorpEngine::kThorpChainLen, 2);
    engine.parameterChanged(ThorpEngine::kThorpArpSlot, 2);
    engine.parameterChanged(ThorpEngine::kThorpChainLen, 3);
    engine.parameterChanged(ThorpEngine::kThorpArpSlot, 3);

    engine.noteOn(60, 100);
    engine.parameterChanged(ThorpEngine::kThorpPlayMode, ThorpEngine::kPlaySong);
    engine.parameterChanged(ThorpEngine::kThorpSequenceMode, ThorpEngine::kModePingPong);

    // PingPong with 3 positions: 0 -> 1 -> 2 -> 1 -> 0 -> 1 -> ...
    // Each slot has default length 8.
    // The advance happens when localStep_ wraps at the end of a slot.
    int expected[] = { 0, 1, 2, 1, 0, 1 };

    for (int slot = 0; slot < 6; slot++) {
        ASSERT_EQ(engine.uiPlayingChainPos(), expected[slot],
                  "pingpong chain position");
        for (int tick = 0; tick < 8; tick++)
            engine.clockTick(nullptr);
    }
}

// ===== Slot save/load ======================================================

static void test_slot_save_load()
{
    ThorpEngine engine = makeEngine();

    // On slot 1 (default), set pattern to 5
    engine.parameterChanged(ThorpEngine::kThorpPattern, 5);

    // Switch to slot 2
    engine.parameterChanged(ThorpEngine::kThorpArpSlot, 2);

    // Check that slot 2 has default pattern (0)
    int16_t pat, vp, len, off, rev;
    engine.getLoadedSlotParams(pat, vp, len, off, rev);
    ASSERT_EQ(pat, 0, "slot 2 has default pattern 0");

    // Switch back to slot 1
    engine.parameterChanged(ThorpEngine::kThorpArpSlot, 1);
    engine.getLoadedSlotParams(pat, vp, len, off, rev);
    ASSERT_EQ(pat, 5, "slot 1 pattern restored to 5");
}

static void test_slot_notes_persist()
{
    ThorpEngine engine = makeEngine();

    // Slot 1: add notes
    engine.parameterChanged(ThorpEngine::kThorpArpSlot, 1);
    engine.noteOn(60, 100);
    engine.noteOn(64, 100);

    // Switch to slot 2, then back
    engine.parameterChanged(ThorpEngine::kThorpArpSlot, 2);
    engine.parameterChanged(ThorpEngine::kThorpArpSlot, 1);

    // clockTick should still produce output (latched notes restored from slot)
    EngineOutput eo = engine.clockTick(nullptr);
    ASSERT_TRUE(eo.gate > 0.0f, "notes persist through slot switch — gate fires");
}

// ===== MIDI input ==========================================================

static void test_noteon_forces_jam()
{
    ThorpEngine engine = makeEngine();

    // Start in Song mode
    engine.parameterChanged(ThorpEngine::kThorpPlayMode, ThorpEngine::kPlaySong);

    // noteOn forces Jam mode
    engine.noteOn(60, 100);

    // clockTick in Jam mode should produce output
    EngineOutput eo = engine.clockTick(nullptr);
    ASSERT_TRUE(eo.gate > 0.0f, "noteOn forces Jam — gate fires");
}

static void test_noteoff_keeps_latched()
{
    ThorpEngine engine = makeEngine();

    engine.noteOn(60, 100);
    engine.noteOn(64, 100);
    engine.noteOff(60);
    engine.noteOff(64);

    // All fingers lifted, but latched notes persist. Clock should still fire.
    EngineOutput eo = engine.clockTick(nullptr);
    ASSERT_TRUE(eo.gate > 0.0f, "noteOff keeps latched — gate fires after all released");
}

static void test_first_noteon_clears_latched()
{
    ThorpEngine engine = makeEngine();

    // First phrase: press 60 and 64
    engine.noteOn(60, 100);
    engine.noteOn(64, 100);
    engine.noteOff(60);
    engine.noteOff(64);
    // numActiveNotes_ is now 0, latched has [60, 64]

    // New phrase: press 72 — should clear old latched, start fresh
    engine.noteOn(72, 100);

    // Clock a few ticks. All output should be note 72 only.
    // With ascending pattern (default, pattern 0) and 1 note, every step
    // maps to the same note.
    for (int i = 0; i < 8; i++) {
        EngineOutput eo = engine.clockTick(nullptr);
        if (eo.gate > 0.0f) {
            ASSERT_EQ((int)eo.midiNote, 72,
                      "first noteOn clears old latched — only note 72");
        }
    }
}

static void test_add_unique_no_duplicates()
{
    ThorpEngine engine = makeEngine();

    // Press same note twice — dedup should keep numLatchedNotes_ at 1
    engine.noteOn(60, 100);
    ASSERT_EQ(engine.numLatchedNotes_, 1, "one noteOn -> 1 latched note");

    engine.noteOn(60, 100);
    ASSERT_EQ(engine.numLatchedNotes_, 1, "duplicate noteOn -> still 1 latched note");

    // Press a different note — should go to 2, not 3
    engine.noteOn(64, 100);
    ASSERT_EQ(engine.numLatchedNotes_, 2, "new note after dup -> 2 latched notes");
}

// ===== clockTick Jam mode ==================================================

static void test_jam_ascending_all_gate()
{
    ThorpEngine engine = makeEngine();
    engine.parameterChanged(ThorpEngine::kThorpPattern, 0); // Ascending
    engine.parameterChanged(ThorpEngine::kThorpLength, 8);
    engine.parameterChanged(ThorpEngine::kThorpGateProb, 100);
    engine.parameterChanged(ThorpEngine::kThorpGateLen, 50); // < 100
    engine.noteOn(60, 100);

    // Ascending pattern {1,2,3,4,5,6,7,8} — no rests (all > 0).
    // With 1 note, every step maps to note 60.
    // gateLen=50 < 100, so gateActive_ resets each tick => isNewNote always true.
    int gateCount = 0;
    for (int i = 0; i < 8; i++) {
        EngineOutput eo = engine.clockTick(nullptr);
        if (eo.gate > 0.0f) gateCount++;
    }
    ASSERT_EQ(gateCount, 8, "ascending + 1 note + gateProb 100% = all gates fire");
}

static void test_jam_syncopated_has_rests()
{
    ThorpEngine engine = makeEngine();
    engine.parameterChanged(ThorpEngine::kThorpPattern, 19); // Syncopated
    engine.parameterChanged(ThorpEngine::kThorpLength, 8);
    engine.parameterChanged(ThorpEngine::kThorpGateProb, 100);
    engine.parameterChanged(ThorpEngine::kThorpGateLen, 50);
    engine.noteOn(60, 100);

    // Syncopated: {1,0,2,0,3,0,4,0} — positions 1,3,5,7 are rests (value 0)
    int gateCount = 0;
    int restCount = 0;
    for (int i = 0; i < 8; i++) {
        EngineOutput eo = engine.clockTick(nullptr);
        if (eo.gate > 0.0f)
            gateCount++;
        else
            restCount++;
    }
    ASSERT_EQ(gateCount, 4, "syncopated: 4 gates (steps 0,2,4,6)");
    ASSERT_EQ(restCount, 4, "syncopated: 4 rests (steps 1,3,5,7)");
}

static void test_no_notes_no_output()
{
    ThorpEngine engine = makeEngine();
    engine.parameterChanged(ThorpEngine::kThorpPattern, 0);
    engine.parameterChanged(ThorpEngine::kThorpGateProb, 100);

    // No notes latched — should produce no gate output
    for (int i = 0; i < 8; i++) {
        EngineOutput eo = engine.clockTick(nullptr);
        ASSERT_FLOAT_EQ(eo.gate, 0.0f, "no latched notes — no gate");
    }
}

// ===== Reset ===============================================================

static void test_reset_preserves_notes()
{
    ThorpEngine engine = makeEngine();
    engine.noteOn(60, 100);

    // Verify gate fires before reset
    EngineOutput eo1 = engine.clockTick(nullptr);
    ASSERT_TRUE(eo1.gate > 0.0f, "gate fires before reset");

    // Reset
    engine.reset();

    // Latched notes should survive reset — clock should still fire
    EngineOutput eo2 = engine.clockTick(nullptr);
    ASSERT_TRUE(eo2.gate > 0.0f, "latched notes survive reset — gate fires after");
}

static void test_reset_state()
{
    ThorpEngine engine = makeEngine();
    engine.noteOn(60, 100);

    // Clock a few times to advance state
    for (int i = 0; i < 5; i++)
        engine.clockTick(nullptr);
    ASSERT_TRUE(engine.currentStep() >= 0, "step advanced after clocking");

    // Reset should bring currentStep back to -1
    engine.reset();
    ASSERT_EQ(engine.currentStep(), -1, "currentStep = -1 after reset");

    // Chain position also resets (observable via uiPlayingChainPos)
    ASSERT_EQ(engine.uiPlayingChainPos(), 0, "chainPos = 0 after reset");
}

// ===== Main ================================================================

int main()
{
    // Velocity
    test_constant_pattern();
    test_accent_pattern();
    test_pattern_change();
    test_global_velocity_scaling();
    test_velocity_across_notes();

    // Chain building
    test_chain_build();

    // Song mode Seq advancement
    test_song_seq_mode();
    test_song_single_slot();

    // Song mode PingPong
    test_song_pingpong();

    // Slot save/load
    test_slot_save_load();
    test_slot_notes_persist();

    // MIDI input
    test_noteon_forces_jam();
    test_noteoff_keeps_latched();
    test_first_noteon_clears_latched();
    test_add_unique_no_duplicates();

    // clockTick Jam mode
    test_jam_ascending_all_gate();
    test_jam_syncopated_has_rests();
    test_no_notes_no_output();

    // Reset
    test_reset_preserves_notes();
    test_reset_state();

    printf("%d tests, %d failures\n", tests, failures);
    return failures ? 1 : 0;
}
