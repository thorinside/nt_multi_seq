// Unit tests for MixQuantizer: sum/average a pitch voltage and quantize to a scale.

#include "../mix/MixQuantizer.h"
#include <stdio.h>
#include <math.h>

static int failures = 0;
static int tests = 0;

#define ASSERT_NEAR(a, b, tol, msg) do { \
    tests++; \
    if (fabs((double)(a) - (double)(b)) > (double)(tol)) { \
        fprintf(stderr, "FAIL: %s: expected %.4f, got %.4f\n", msg, (double)(b), (double)(a)); \
        failures++; \
    } \
} while (0)

static _NT_sclNote centNote(double cents)
{
    _NT_sclNote n;
    n.octaves = cents / 1200.0;
    return n;
}

// C major as a 7-note .scl (D E F G A B C')
static void makeMajor(ScaleQuantizer& q)
{
    static const double cents[] = { 200, 400, 500, 700, 900, 1100, 1200 };
    _NT_sclNote notes[7];
    for (int i = 0; i < 7; ++i)
        notes[i] = centNote(cents[i]);
    q.loadScale(notes, 7);
}

int main()
{
    MixQuantizer mix;
    ScaleQuantizer major;
    makeMajor(major);

    // Sum mode, no scale: input passes straight through.
    ASSERT_NEAR(mix.process(1.25f, MixQuantizer::kSum, 3, nullptr, 0), 1.25f, 1e-6,
        "sum mode without scale passes input through");

    // Average mode divides the summed bus by the number of sources.
    ASSERT_NEAR(mix.process(3.0f, MixQuantizer::kAverage, 2, nullptr, 0), 1.5f, 1e-6,
        "average mode divides by two sources");
    ASSERT_NEAR(mix.process(3.0f, MixQuantizer::kAverage, 0, nullptr, 0), 3.0f, 1e-6,
        "average mode treats fewer than one source as one");

    // Quantize: 0.55 V (6.6 semitones) snaps to G (7 semitones) in C major.
    ASSERT_NEAR(mix.process(0.55f, MixQuantizer::kSum, 1, &major, 0), 7.0f / 12.0f, 1e-4,
        "sum mode quantizes to nearest major-scale note");

    // Average then quantize: (0.6 + 0.6) / 2 = 0.6 V = 7.2 semitones -> G.
    ASSERT_NEAR(mix.process(1.2f, MixQuantizer::kAverage, 2, &major, 0), 7.0f / 12.0f, 1e-4,
        "average mode quantizes after dividing");

    // Root D (2): 0.25 V (3 semitones, Eb) is not in D major; nearest is E (4) or D (2).
    // 3 semitones above C = 1 semitone above D, which rounds to D itself.
    ASSERT_NEAR(mix.process(0.25f, MixQuantizer::kSum, 1, &major, 2), 2.0f / 12.0f, 1e-4,
        "root note shifts the scale before quantizing");

    // Negative voltages quantize in the octave below.
    ASSERT_NEAR(mix.process(-0.45f, MixQuantizer::kSum, 1, &major, 0), -5.0f / 12.0f, 1e-4,
        "negative pitch snaps to G below C");

    printf("%d tests, %d failures\n", tests, failures);
    return failures > 0 ? 1 : 0;
}
