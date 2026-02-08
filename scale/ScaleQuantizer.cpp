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

double ScaleQuantizer::degreeRatio(int degree) const
{
    if (degree <= 0) return 1.0;
    if (degree > (int)numNotes_) degree = (int)numNotes_;
    return ratios_[degree - 1];
}

float ScaleQuantizer::degreeCents(int degree) const
{
    if (degree <= 0) return 0.0f;
    return (float)(log2_impl(degreeRatio(degree)) * 1200.0);
}

float ScaleQuantizer::periodCents() const
{
    if (numNotes_ == 0) return 1200.0f;
    return (float)(log2_impl(ratios_[numNotes_ - 1]) * 1200.0);
}

bool ScaleQuantizer::isOctaveBased() const
{
    float pc = periodCents();
    return pc >= 1150.0f && pc <= 1250.0f;
}

void ScaleQuantizer::computeNoteWeights(float* weights, int numDegrees, WeightMode mode) const
{
    computeNoteWeights(weights, numDegrees, mode, 3.0f);
}

void ScaleQuantizer::computeNoteWeights(float* weights, int numDegrees, WeightMode mode, float charWeight) const
{
    if (mode == kWeightEqual || numDegrees <= 0) {
        for (int i = 0; i < numDegrees; ++i)
            weights[i] = 1.0f;
        return;
    }

    if (mode == kWeightMajor) {
        if (!isOctaveBased()) {
            // Non-octave scales: fall through to equal
            for (int i = 0; i < numDegrees; ++i)
                weights[i] = 1.0f;
            return;
        }
        // Major scale semitone set
        static const int majorSemitones[] = { 0, 2, 4, 5, 7, 9, 11 };
        for (int i = 0; i < numDegrees; ++i) {
            float cents = degreeCents(i);
            // Round to nearest semitone mod 12
            int semitone = ((int)(cents / 100.0f + 0.5f)) % 12;
            if (semitone < 0) semitone += 12;
            bool isMajor = false;
            for (int m = 0; m < 7; ++m) {
                if (semitone == majorSemitones[m]) {
                    isMajor = true;
                    break;
                }
            }
            weights[i] = isMajor ? 1.0f : charWeight;
        }
        return;
    }

    if (mode == kWeightHarmonic) {
        // For each degree, find the nearest simple fraction p/q (p >= q, p,q in [1,12])
        // and check if it's within 20 cents and p*q <= 18 (consonant)
        for (int i = 0; i < numDegrees; ++i) {
            double r = degreeRatio(i);
            double bestDist = 1e10;
            int bestPQ = 999;
            // Search all fractions p/q where p >= q, p,q in [1,12]
            for (int q = 1; q <= 12; ++q) {
                for (int p = q; p <= 12; ++p) {
                    double frac = (double)p / (double)q;
                    double distCents = fabs(log2_impl(r / frac) * 1200.0);
                    if (distCents < bestDist) {
                        bestDist = distCents;
                        bestPQ = p * q;
                    }
                }
            }
            bool consonant = (bestDist <= 20.0 && bestPQ <= 18);
            weights[i] = consonant ? 1.0f : charWeight;
        }
        return;
    }

    // Fallback
    for (int i = 0; i < numDegrees; ++i)
        weights[i] = 1.0f;
}

int ScaleQuantizer::findNearestDegree(float vOct, int& outOctave) const
{
    if (numNotes_ == 0) {
        // Fallback: 12-TET, degree = semitone within octave
        int semitone = (int)(vOct * 12.0f + 0.5f);
        outOctave = 0;
        if (semitone < 0) {
            outOctave = -((-semitone + 11) / 12);
            semitone = semitone % 12;
            if (semitone < 0) semitone += 12;
        } else if (semitone >= 12) {
            outOctave = semitone / 12;
            semitone = semitone % 12;
        }
        return semitone;
    }

    int period = (int)numNotes_;
    double periodRatio = ratios_[period - 1];
    double logPeriod = log2_impl(periodRatio);

    // Convert V/oct to ratio (root=0 assumed, so vOct is pure pitch)
    double ratio = pow(2.0, (double)vOct);

    // Normalize ratio into one period, tracking octave offset
    outOctave = 0;
    if (ratio < 1.0) {
        while (ratio < 1.0) {
            ratio *= periodRatio;
            outOctave--;
        }
    } else if (ratio >= periodRatio) {
        while (ratio >= periodRatio) {
            ratio /= periodRatio;
            outOctave++;
        }
    }

    // Search all degrees for closest match in log space
    int bestDegree = 0;
    double bestDist = 1e10;
    for (int d = 0; d < period; ++d) {
        double r = (d == 0) ? 1.0 : ratios_[d - 1];
        double dist = fabs(log2_impl(ratio) - log2_impl(r));
        if (dist < bestDist) {
            bestDist = dist;
            bestDegree = d;
        }
    }

    // Also check the period boundary (degree 0 of next period)
    double distToPeriod = fabs(log2_impl(ratio) - logPeriod);
    if (distToPeriod < bestDist) {
        bestDegree = 0;
        outOctave++;
    }

    return bestDegree;
}

int ScaleQuantizer::warpDegree(int degree, WeightMode mode, int warpAmount) const
{
    if (warpAmount <= 0) return degree;
    if (numNotes_ == 0) return degree;

    int nd = (int)numNotes_;

    // Compute weights with custom characteristic weight
    float charWeight = 1.0f + (float)warpAmount / 100.0f * 4.0f;
    float weights[128];
    computeNoteWeights(weights, nd, mode, charWeight);

    // Build cumulative weight table
    float cumulative[128];
    cumulative[0] = weights[0];
    for (int i = 1; i < nd; ++i)
        cumulative[i] = cumulative[i - 1] + weights[i];
    float totalWeight = cumulative[nd - 1];

    // Map degree to position in weighted space
    float pos = ((float)degree + 0.5f) / (float)nd * totalWeight;

    // Find which zone pos falls in
    for (int i = 0; i < nd; ++i) {
        if (pos <= cumulative[i])
            return i;
    }

    return nd - 1;
}
