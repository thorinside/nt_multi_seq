#ifndef SCALE_QUANTIZER_H
#define SCALE_QUANTIZER_H

#include <cstddef>
#include <distingnt/microtuning.h>
#include <stdint.h>

class ScaleQuantizer {
public:
    ScaleQuantizer();

    // Load scale data from .scl note array
    void loadScale(const _NT_sclNote* notes, uint32_t numNotes);

    // Quantize a scale degree (0-based) + octave offset to V/oct pitch
    // scaleDegree: 0 to numNotes-1
    // octave: base octave offset
    // root: root note semitone offset (0-11)
    float quantize(int scaleDegree, int octave, int root) const;

    // Quantize a MIDI note number to V/oct using the loaded scale
    float midiNoteToVOct(uint8_t midiNote, int root) const;

    // Get the MIDI note number for a scale degree
    uint8_t scaleDegreeToMidi(int scaleDegree, int octave, int root) const;

    uint32_t numNotes() const { return numNotes_; }
    bool isLoaded() const { return numNotes_ > 0; }

    // Cents and ratio accessors for note weight computation
    float degreeCents(int degree) const;
    float periodCents() const;
    bool isOctaveBased() const;
    double degreeRatio(int degree) const;

    // Note weight modes
    enum WeightMode { kWeightMajor = 0, kWeightHarmonic, kWeightEqual };

    // Fills weights[] for each scale degree. Caller normalizes to probabilities.
    void computeNoteWeights(float* weights, int numDegrees, WeightMode mode) const;

    // Overload: use a custom characteristic weight instead of the default 3.0
    void computeNoteWeights(float* weights, int numDegrees, WeightMode mode, float charWeight) const;

    // Find the nearest scale degree for a V/oct pitch (root=0 assumed).
    // Returns degree (0..numNotes-1) and sets outOctave to the period offset.
    int findNearestDegree(float vOct, int& outOctave) const;

    // Warp a degree through weighted space. Returns the warped degree.
    // warpAmount: 0-100 (0=identity, 100=maximum warp toward characteristic notes)
    int warpDegree(int degree, WeightMode mode, int warpAmount) const;

private:
    // Convert a single _NT_sclNote to a ratio (relative to 1/1)
    double noteToRatio(const _NT_sclNote& note) const;

    double ratios_[128];      // Precomputed ratios for each scale degree
    uint32_t numNotes_;
};

#endif // SCALE_QUANTIZER_H
