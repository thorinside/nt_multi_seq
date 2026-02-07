#include "ScaleQuantizer.h"
#include <math.h>

// log2 not available on disting NT runtime
static inline double log2_impl(double x) {
    return log(x) * 1.4426950408889634;  // 1/ln(2)
}

ScaleQuantizer::ScaleQuantizer()
    : numNotes_(0)
{
    for (int i = 0; i < 128; ++i)
        ratios_[i] = 1.0;
}

double ScaleQuantizer::noteToRatio(const _NT_sclNote& note) const
{
    if (note.isRatio()) {
        // Ratio representation: numerator/denominator
        return (double)note.numerator() / (double)note.denominator();
    } else {
        // Cents representation stored as octaves (cents/1200)
        return pow(2.0, note.octaves);
    }
}

void ScaleQuantizer::loadScale(const _NT_sclNote* notes, uint32_t numNotes)
{
    numNotes_ = numNotes;
    if (numNotes == 0) return;

    // .scl format: notes[0..numNotes-1] define intervals from 1/1
    // The last note is typically the octave (e.g., 2/1)
    // We store the ratios directly for lookup
    for (uint32_t i = 0; i < numNotes && i < 128; ++i) {
        ratios_[i] = noteToRatio(notes[i]);
    }
}

float ScaleQuantizer::quantize(int scaleDegree, int octave, int root) const
{
    if (numNotes_ == 0) {
        // Fallback: 12-TET
        return (float)(root + octave * 12 + scaleDegree) / 12.0f;
    }

    // The scale has numNotes_ intervals. The last one defines the "period" (usually 2/1).
    // Scale degrees wrap around at numNotes_.
    int period = (int)numNotes_;
    int octaveOffset = 0;

    // Normalize scale degree into [0, period)
    if (scaleDegree < 0) {
        octaveOffset = -(-scaleDegree / period + 1);
        scaleDegree = scaleDegree % period;
        if (scaleDegree < 0) scaleDegree += period;
    } else if (scaleDegree >= period) {
        octaveOffset = scaleDegree / period;
        scaleDegree = scaleDegree % period;
    }

    // Get the ratio for this scale degree
    // Degree 0 = 1/1 (unison), degree 1 = first interval, etc.
    double ratio;
    if (scaleDegree == 0) {
        ratio = 1.0;
    } else {
        ratio = ratios_[scaleDegree - 1];
    }

    // Apply octave from period wrapping
    double periodRatio = ratios_[period - 1]; // Usually 2.0 for octave
    if (octaveOffset > 0) {
        for (int i = 0; i < octaveOffset; ++i)
            ratio *= periodRatio;
    } else if (octaveOffset < 0) {
        for (int i = 0; i < -octaveOffset; ++i)
            ratio /= periodRatio;
    }

    // Convert ratio to V/oct: V = log2_impl(ratio) + base
    double vOct = log2_impl(ratio);

    // Add root note (semitones) and octave as V/oct offset
    vOct += (double)root / 12.0 + (double)octave;

    return (float)vOct;
}

float ScaleQuantizer::midiNoteToVOct(uint8_t midiNote, int root) const
{
    // Convert MIDI note to V/oct (middle C = note 60 = 0V)
    if (numNotes_ == 0) {
        return (float)(midiNote - 60) / 12.0f;
    }

    // Map MIDI note relative to root into scale degrees
    int noteRelative = midiNote - root;
    int octave = noteRelative / 12;
    int remainder = noteRelative % 12;
    if (remainder < 0) {
        remainder += 12;
        octave--;
    }

    // Find the closest scale degree for this chromatic offset
    // We'll approximate by mapping 12-TET semitones to scale degrees
    int period = (int)numNotes_;
    double targetRatio = pow(2.0, (double)remainder / 12.0);

    int bestDegree = 0;
    double bestDist = 1e10;
    for (int d = 0; d < period; ++d) {
        double r = (d == 0) ? 1.0 : ratios_[d - 1];
        double dist = fabs(log2_impl(r) - log2_impl(targetRatio));
        if (dist < bestDist) {
            bestDist = dist;
            bestDegree = d;
        }
    }

    return quantize(bestDegree, octave - 5 + (root / 12), root % 12);
}

uint8_t ScaleQuantizer::scaleDegreeToMidi(int scaleDegree, int octave, int root) const
{
    float vOct = quantize(scaleDegree, octave, root);
    // V/oct to MIDI: 0V = middle C (60)
    int midiNote = (int)(vOct * 12.0f + 60.0f + 0.5f);
    if (midiNote < 0) midiNote = 0;
    if (midiNote > 127) midiNote = 127;
    return (uint8_t)midiNote;
}
