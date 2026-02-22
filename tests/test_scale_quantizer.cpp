// Unit tests for ScaleQuantizer note weight methods.
// Compiles standalone — only needs distingNT API headers (no link dependency).

#include "../scale/ScaleQuantizer.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static int failures = 0;
static int tests = 0;

#define ASSERT_EQ(a, b, msg) do { \
    tests++; \
    if ((a) != (b)) { \
        fprintf(stderr, "FAIL: %s: expected %d, got %d\n", msg, (int)(b), (int)(a)); \
        failures++; \
    } \
} while (0)

#define ASSERT_NEAR(a, b, tol, msg) do { \
    tests++; \
    if (fabs((double)(a) - (double)(b)) > (double)(tol)) { \
        fprintf(stderr, "FAIL: %s: expected %.2f, got %.2f (tol=%.2f)\n", \
            msg, (double)(b), (double)(a), (double)(tol)); \
        failures++; \
    } \
} while (0)

// Helper: create a cents-based _NT_sclNote
static _NT_sclNote centNote(double cents)
{
    _NT_sclNote n;
    n.octaves = cents / 1200.0;
    return n;
}

// Helper: create a ratio-based _NT_sclNote
static _NT_sclNote ratioNote(int num, int den)
{
    _NT_sclNote n;
    n.numeratorValue = num;
    n.denominatorValue = -den; // negative denominator signals ratio
    return n;
}

// Build a 12-TET chromatic scale (12 notes, octave period)
static void make12TET(_NT_sclNote* notes)
{
    for (int i = 0; i < 12; ++i)
        notes[i] = centNote((i + 1) * 100.0);
}

// Build a Bohlen-Pierce scale (13 notes, period = 3/1 ~1902 cents)
static void makeBohlenPierce(_NT_sclNote* notes)
{
    // 13-equal division of 3:1
    double period = 1200.0 * log(3.0) / log(2.0); // ~1901.96
    for (int i = 0; i < 13; ++i)
        notes[i] = centNote((i + 1) * period / 13.0);
}

// Build a 7-note just intonation major scale
static void makeJustMajor(_NT_sclNote* notes)
{
    notes[0] = ratioNote(9, 8);    // 203.91 cents  (D, semitone ~2)
    notes[1] = ratioNote(5, 4);    // 386.31 cents  (E, semitone ~4)
    notes[2] = ratioNote(4, 3);    // 498.04 cents  (F, semitone ~5)
    notes[3] = ratioNote(3, 2);    // 701.96 cents  (G, semitone ~7)
    notes[4] = ratioNote(5, 3);    // 884.36 cents  (A, semitone ~9)
    notes[5] = ratioNote(15, 8);   // 1088.27 cents (B, semitone ~11)
    notes[6] = ratioNote(2, 1);    // 1200 cents    (octave)
}

// --- Tests ---

static void test_degreeCents_12TET()
{
    ScaleQuantizer sq;
    _NT_sclNote notes[12];
    make12TET(notes);
    sq.loadScale(notes, 12);

    ASSERT_NEAR(sq.degreeCents(0), 0.0f, 0.1f, "12TET degree 0 = 0 cents");
    ASSERT_NEAR(sq.degreeCents(1), 100.0f, 0.1f, "12TET degree 1 = 100 cents");
    ASSERT_NEAR(sq.degreeCents(7), 700.0f, 0.1f, "12TET degree 7 = 700 cents");
    ASSERT_NEAR(sq.degreeCents(12), 1200.0f, 0.1f, "12TET degree 12 = 1200 cents");
}

static void test_periodCents_12TET()
{
    ScaleQuantizer sq;
    _NT_sclNote notes[12];
    make12TET(notes);
    sq.loadScale(notes, 12);

    ASSERT_NEAR(sq.periodCents(), 1200.0f, 0.1f, "12TET period = 1200 cents");
}

static void test_periodCents_BP()
{
    ScaleQuantizer sq;
    _NT_sclNote notes[13];
    makeBohlenPierce(notes);
    sq.loadScale(notes, 13);

    ASSERT_NEAR(sq.periodCents(), 1901.96f, 1.0f, "BP period ~1902 cents");
}

static void test_isOctaveBased_12TET()
{
    ScaleQuantizer sq;
    _NT_sclNote notes[12];
    make12TET(notes);
    sq.loadScale(notes, 12);

    ASSERT_EQ(sq.isOctaveBased(), true, "12TET is octave-based");
}

static void test_isOctaveBased_BP()
{
    ScaleQuantizer sq;
    _NT_sclNote notes[13];
    makeBohlenPierce(notes);
    sq.loadScale(notes, 13);

    ASSERT_EQ(sq.isOctaveBased(), false, "BP is not octave-based");
}

static void test_majorWeights_12TET()
{
    // 12-TET chromatic: degrees 0,2,4,5,7,9,11 are major (weight 1)
    // degrees 1,3,6,8,10 are non-major (weight 3)
    ScaleQuantizer sq;
    _NT_sclNote notes[12];
    make12TET(notes);
    sq.loadScale(notes, 12);

    float weights[12];
    sq.computeNoteWeights(weights, 12, ScaleQuantizer::kWeightMajor);

    // Major degrees get charWeight (3.0), non-major get 1.0
    // Characteristic notes attract: higher weight = larger warp zone
    ASSERT_NEAR(weights[0], 3.0f, 0.01f, "Major: degree 0 (C) = 3");
    ASSERT_NEAR(weights[2], 3.0f, 0.01f, "Major: degree 2 (D) = 3");
    ASSERT_NEAR(weights[4], 3.0f, 0.01f, "Major: degree 4 (E) = 3");
    ASSERT_NEAR(weights[5], 3.0f, 0.01f, "Major: degree 5 (F) = 3");
    ASSERT_NEAR(weights[7], 3.0f, 0.01f, "Major: degree 7 (G) = 3");
    ASSERT_NEAR(weights[9], 3.0f, 0.01f, "Major: degree 9 (A) = 3");
    ASSERT_NEAR(weights[11], 3.0f, 0.01f, "Major: degree 11 (B) = 3");

    // Non-major degrees get base weight 1.0
    ASSERT_NEAR(weights[1], 1.0f, 0.01f, "Major: degree 1 (C#) = 1");
    ASSERT_NEAR(weights[3], 1.0f, 0.01f, "Major: degree 3 (Eb) = 1");
    ASSERT_NEAR(weights[6], 1.0f, 0.01f, "Major: degree 6 (F#) = 1");
    ASSERT_NEAR(weights[8], 1.0f, 0.01f, "Major: degree 8 (Ab) = 1");
    ASSERT_NEAR(weights[10], 1.0f, 0.01f, "Major: degree 10 (Bb) = 1");
}

static void test_majorWeights_nonOctave_fallback()
{
    // Non-octave scales should fall through to Equal (all 1.0)
    ScaleQuantizer sq;
    _NT_sclNote notes[13];
    makeBohlenPierce(notes);
    sq.loadScale(notes, 13);

    float weights[13];
    sq.computeNoteWeights(weights, 13, ScaleQuantizer::kWeightMajor);

    for (int i = 0; i < 13; ++i)
        ASSERT_NEAR(weights[i], 1.0f, 0.01f, "Major non-octave: all equal");
}

static void test_harmonicWeights_justMajor()
{
    // Just intonation major: all degrees are simple ratios and should be consonant
    ScaleQuantizer sq;
    _NT_sclNote notes[7];
    makeJustMajor(notes);
    sq.loadScale(notes, 7);

    float weights[7];
    sq.computeNoteWeights(weights, 7, ScaleQuantizer::kWeightHarmonic);

    // Consonant degrees get charWeight (3.0), non-consonant get 1.0
    // Degree 0 = 1/1 (p*q=1, consonant)
    ASSERT_NEAR(weights[0], 3.0f, 0.01f, "Harmonic JI: degree 0 (1/1) consonant");
    // Degree 1 = 9/8 (p*q=72, NOT consonant — too complex)
    ASSERT_NEAR(weights[1], 1.0f, 0.01f, "Harmonic JI: degree 1 (9/8) non-consonant");
    // Degree 2 = 5/4 (p*q=20, NOT consonant by p*q<=18 rule)
    ASSERT_NEAR(weights[2], 1.0f, 0.01f, "Harmonic JI: degree 2 (5/4) non-consonant");
    // Degree 3 = 4/3 (p*q=12, consonant)
    ASSERT_NEAR(weights[3], 3.0f, 0.01f, "Harmonic JI: degree 3 (4/3) consonant");
    // Degree 4 = 3/2 (p*q=6, consonant)
    ASSERT_NEAR(weights[4], 3.0f, 0.01f, "Harmonic JI: degree 4 (3/2) consonant");
    // Degree 5 = 5/3 (p*q=15, consonant)
    ASSERT_NEAR(weights[5], 3.0f, 0.01f, "Harmonic JI: degree 5 (5/3) consonant");
    // Degree 6 = 15/8 (p*q=120, NOT consonant)
    // Nearest in [1,12]: 11/6=1.833 (38.9c), not within 20c. So non-consonant.
    ASSERT_NEAR(weights[6], 1.0f, 0.01f, "Harmonic JI: degree 6 (15/8) non-consonant");
}

static void test_harmonicWeights_12TET()
{
    // 12-TET: some degrees approximate simple ratios
    ScaleQuantizer sq;
    _NT_sclNote notes[12];
    make12TET(notes);
    sq.loadScale(notes, 12);

    float weights[12];
    sq.computeNoteWeights(weights, 12, ScaleQuantizer::kWeightHarmonic);

    // Consonant degrees get charWeight (3.0), non-consonant get 1.0
    // Degree 0 = 0 cents = 1/1 (p*q=1) consonant
    ASSERT_NEAR(weights[0], 3.0f, 0.01f, "Harmonic 12TET: degree 0 (unison)");
    // Degree 5 = 500 cents ≈ 4/3 (498c, 2c off, p*q=12) consonant
    ASSERT_NEAR(weights[5], 3.0f, 0.01f, "Harmonic 12TET: degree 5 (fourth)");
    // Degree 7 = 700 cents ≈ 3/2 (702c, 2c off, p*q=6) consonant
    ASSERT_NEAR(weights[7], 3.0f, 0.01f, "Harmonic 12TET: degree 7 (fifth)");

    // All weights should be valid (no NaN, no negative)
    for (int i = 0; i < 12; ++i) {
        tests++;
        if (weights[i] != 1.0f && weights[i] != 3.0f) {
            fprintf(stderr, "FAIL: Harmonic 12TET degree %d: weight %.2f not 1 or 3\n",
                i, weights[i]);
            failures++;
        }
    }
}

static void test_harmonicWeights_BP()
{
    // Non-octave Bohlen-Pierce should still work (no crash, valid weights)
    ScaleQuantizer sq;
    _NT_sclNote notes[13];
    makeBohlenPierce(notes);
    sq.loadScale(notes, 13);

    float weights[13];
    sq.computeNoteWeights(weights, 13, ScaleQuantizer::kWeightHarmonic);

    for (int i = 0; i < 13; ++i) {
        tests++;
        if (weights[i] != 1.0f && weights[i] != 3.0f) {
            fprintf(stderr, "FAIL: Harmonic BP degree %d: weight %.2f not 1 or 3\n",
                i, weights[i]);
            failures++;
        }
    }
}

static void test_equalWeights()
{
    ScaleQuantizer sq;
    _NT_sclNote notes[12];
    make12TET(notes);
    sq.loadScale(notes, 12);

    float weights[12];
    sq.computeNoteWeights(weights, 12, ScaleQuantizer::kWeightEqual);

    for (int i = 0; i < 12; ++i)
        ASSERT_NEAR(weights[i], 1.0f, 0.01f, "Equal: all weights 1.0");
}

static void test_degreeRatio()
{
    ScaleQuantizer sq;
    _NT_sclNote notes[7];
    makeJustMajor(notes);
    sq.loadScale(notes, 7);

    ASSERT_NEAR(sq.degreeRatio(0), 1.0, 0.001, "degreeRatio(0) = 1.0");
    ASSERT_NEAR(sq.degreeRatio(1), 9.0/8.0, 0.001, "degreeRatio(1) = 9/8");
    ASSERT_NEAR(sq.degreeRatio(4), 3.0/2.0, 0.001, "degreeRatio(4) = 3/2");
    ASSERT_NEAR(sq.degreeRatio(7), 2.0, 0.001, "degreeRatio(7) = 2/1 (period)");
}

// --- Warp tests ---

static void test_warpDegree_identity()
{
    // warpAmount=0 should be identity for all degrees
    ScaleQuantizer sq;
    _NT_sclNote notes[12];
    make12TET(notes);
    sq.loadScale(notes, 12);

    for (int d = 0; d < 12; ++d) {
        int result = sq.warpDegree(d, ScaleQuantizer::kWeightMajor, 0);
        ASSERT_EQ(result, d, "warpDegree identity (warpAmount=0)");
    }
}

static void test_warpDegree_equal_identity()
{
    // Equal mode: warp should always be identity regardless of warpAmount
    ScaleQuantizer sq;
    _NT_sclNote notes[12];
    make12TET(notes);
    sq.loadScale(notes, 12);

    for (int d = 0; d < 12; ++d) {
        int result = sq.warpDegree(d, ScaleQuantizer::kWeightEqual, 100);
        ASSERT_EQ(result, d, "warpDegree Equal mode always identity");
    }
}

static void test_warpDegree_major_75()
{
    // 12-TET Major, warpAmount=75: charWeight = 1+0.75*4 = 4.0
    // Major degrees (0,2,4,5,7,9,11) weight 4, non-major (1,3,6,8,10) weight 1
    // Total weight = 7*4 + 5*1 = 33
    // Expected: major notes should never shift (they sit in large zones)
    // Some non-major notes should shift toward major neighbors
    ScaleQuantizer sq;
    _NT_sclNote notes[12];
    make12TET(notes);
    sq.loadScale(notes, 12);

    int results[12];
    for (int d = 0; d < 12; ++d)
        results[d] = sq.warpDegree(d, ScaleQuantizer::kWeightMajor, 75);

    // Major notes should stay (their zones are large)
    ASSERT_EQ(results[0], 0, "warp75: C stays");
    ASSERT_EQ(results[2], 2, "warp75: D stays");
    ASSERT_EQ(results[4], 4, "warp75: E stays");
    ASSERT_EQ(results[5], 5, "warp75: F stays");
    ASSERT_EQ(results[7], 7, "warp75: G stays");
    ASSERT_EQ(results[9], 9, "warp75: A stays");
    ASSERT_EQ(results[11], 11, "warp75: B stays");

    // At least some non-major notes should shift toward major neighbors
    int shifts = 0;
    for (int d = 0; d < 12; ++d) {
        if (results[d] != d) shifts++;
    }
    tests++;
    if (shifts == 0) {
        fprintf(stderr, "FAIL: warp75 Major should shift at least some notes\n");
        failures++;
    }
}

static void test_warpDegree_major_100()
{
    // Maximum warp: charWeight = 5.0
    // Should shift more notes than warp 75
    ScaleQuantizer sq;
    _NT_sclNote notes[12];
    make12TET(notes);
    sq.loadScale(notes, 12);

    int results75[12], results100[12];
    int shifts75 = 0, shifts100 = 0;
    for (int d = 0; d < 12; ++d) {
        results75[d] = sq.warpDegree(d, ScaleQuantizer::kWeightMajor, 75);
        results100[d] = sq.warpDegree(d, ScaleQuantizer::kWeightMajor, 100);
        if (results75[d] != d) shifts75++;
        if (results100[d] != d) shifts100++;
    }
    tests++;
    if (shifts100 < shifts75) {
        fprintf(stderr, "FAIL: warp100 should shift >= warp75 (%d < %d)\n", shifts100, shifts75);
        failures++;
    }
}

static void test_findNearestDegree_roundtrip()
{
    // quantize(d, oct, 0) -> findNearestDegree() should recover same d and oct
    ScaleQuantizer sq;
    _NT_sclNote notes[12];
    make12TET(notes);
    sq.loadScale(notes, 12);

    for (int oct = -2; oct <= 2; ++oct) {
        for (int d = 0; d < 12; ++d) {
            float vOct = sq.quantize(d, oct, 0);
            int outOctave;
            int foundDeg = sq.findNearestDegree(vOct, outOctave);
            ASSERT_EQ(foundDeg, d, "findNearestDegree round-trip degree");
            ASSERT_EQ(outOctave, oct, "findNearestDegree round-trip octave");
        }
    }
}

static void test_findNearestDegree_negative()
{
    // Test with negative pitches (octaves below 0)
    ScaleQuantizer sq;
    _NT_sclNote notes[12];
    make12TET(notes);
    sq.loadScale(notes, 12);

    // quantize degree 5, octave -3 -> should recover
    float vOct = sq.quantize(5, -3, 0);
    int outOctave;
    int foundDeg = sq.findNearestDegree(vOct, outOctave);
    ASSERT_EQ(foundDeg, 5, "findNearestDegree negative pitch degree");
    ASSERT_EQ(outOctave, -3, "findNearestDegree negative pitch octave");
}

static void test_findNearestDegree_justMajor()
{
    // Round-trip with a 7-note just intonation scale
    ScaleQuantizer sq;
    _NT_sclNote notes[7];
    makeJustMajor(notes);
    sq.loadScale(notes, 7);

    for (int oct = -1; oct <= 1; ++oct) {
        for (int d = 0; d < 7; ++d) {
            float vOct = sq.quantize(d, oct, 0);
            int outOctave;
            int foundDeg = sq.findNearestDegree(vOct, outOctave);
            ASSERT_EQ(foundDeg, d, "findNearestDegree JI round-trip degree");
            ASSERT_EQ(outOctave, oct, "findNearestDegree JI round-trip octave");
        }
    }
}

static void test_warpDegree_BP()
{
    // Bohlen-Pierce (non-octave): Major mode falls back to Equal, so warp = identity
    // Harmonic mode should still produce valid results
    ScaleQuantizer sq;
    _NT_sclNote notes[13];
    makeBohlenPierce(notes);
    sq.loadScale(notes, 13);

    // Major mode: falls back to equal, so warp is identity
    for (int d = 0; d < 13; ++d) {
        int result = sq.warpDegree(d, ScaleQuantizer::kWeightMajor, 75);
        ASSERT_EQ(result, d, "warpDegree BP Major = identity");
    }

    // Harmonic mode: should produce valid degrees (0..12)
    for (int d = 0; d < 13; ++d) {
        int result = sq.warpDegree(d, ScaleQuantizer::kWeightHarmonic, 75);
        tests++;
        if (result < 0 || result >= 13) {
            fprintf(stderr, "FAIL: warpDegree BP Harmonic degree %d -> %d out of range\n", d, result);
            failures++;
        }
    }
}

static void test_computeNoteWeights_custom_charWeight()
{
    // Verify the overload with custom charWeight works
    ScaleQuantizer sq;
    _NT_sclNote notes[12];
    make12TET(notes);
    sq.loadScale(notes, 12);

    float weights[12];
    sq.computeNoteWeights(weights, 12, ScaleQuantizer::kWeightMajor, 5.0f);

    // Major degrees should be 5.0 (charWeight), non-major should be 1.0
    ASSERT_NEAR(weights[0], 5.0f, 0.01f, "custom charWeight: C = 5");
    ASSERT_NEAR(weights[1], 1.0f, 0.01f, "custom charWeight: C# = 1");
    ASSERT_NEAR(weights[7], 5.0f, 0.01f, "custom charWeight: G = 5");
    ASSERT_NEAR(weights[6], 1.0f, 0.01f, "custom charWeight: F# = 1");
}

int main()
{
    test_degreeCents_12TET();
    test_periodCents_12TET();
    test_periodCents_BP();
    test_isOctaveBased_12TET();
    test_isOctaveBased_BP();
    test_majorWeights_12TET();
    test_majorWeights_nonOctave_fallback();
    test_harmonicWeights_justMajor();
    test_harmonicWeights_12TET();
    test_harmonicWeights_BP();
    test_equalWeights();
    test_degreeRatio();

    // Warp tests
    test_warpDegree_identity();
    test_warpDegree_equal_identity();
    test_warpDegree_major_75();
    test_warpDegree_major_100();
    test_findNearestDegree_roundtrip();
    test_findNearestDegree_negative();
    test_findNearestDegree_justMajor();
    test_warpDegree_BP();
    test_computeNoteWeights_custom_charWeight();

    printf("%d tests, %d failures\n", tests, failures);
    return failures > 0 ? 1 : 0;
}
