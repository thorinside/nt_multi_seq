#ifndef QUANTUM_ENGINE_H
#define QUANTUM_ENGINE_H

#include "SequencerEngine.h"
#include "../scale/ScaleQuantizer.h"

class QuantumEngine : public SequencerEngine {
public:
    QuantumEngine();

    void init(uint32_t sampleRate) override;
    EngineOutput clockTick(const ScaleQuantizer* scale) override;
    void reset() override;
    void parameterChanged(int localIndex, int16_t value) override;
    int getParameterDefs(_NT_parameter* defs) const override;
    const char* name() const override { return "Quantum"; }
    void getFocusDetail(FocusDetail& detail) const override;
    void getFocusBarInfo(FocusBarInfo& info) const override;
    int currentStep() const override;
    int sequenceLength() const override;
    int getStatusText(char* buf, int maxLen) const override;
    bool usesTimedGate() const override { return true; }
    int gateLengthPercent() const override { return gateLen_; }

    enum Param {
        kQMotifLen = 0,     // Short phrase length (4-16 steps)
        kQGateDensity,      // % of steps that have notes (25-100)
        kQRange,            // Melodic span in scale degrees (2-12)
        kQContour,          // Motif shape (Arch/Descend/Ascend/Wave)
        kQMEvery,           // M transforms after this many motif repeats (1-8)
        kQMTransform,       // M transformation type
        kQLEvery,           // L acts after this many M cycles (2-16)
        kQLAction,          // L action type
        kQGateLen,          // Timed gate duration %
        kQVelocity,         // Base velocity (0-100)
        kNumQuantumParams
    };

    enum Contour {
        kContourArch = 0,   // Rise then fall
        kContourDescend,    // Fall then rise
        kContourAscend,     // Steady climb
        kContourWave,       // Sinusoidal
        kNumContours
    };

    enum MTransform {
        kMTranspose = 0,    // Shift up/down by interval
        kMInvert,           // Mirror intervals
        kMRotate,           // Rotate pattern positions
        kMPermute,          // Swap adjacent notes
        kNumMTransforms
    };

    enum LAction {
        kLNewMotif = 0,     // Generate fresh motif
        kLOctaveShift,      // Shift up or down an octave
        kLExpand,           // Expand/contract range
        kLReverse,          // Reverse motif order
        kNumLActions
    };

private:
    static constexpr int kMaxMotifLen = 16;
    static constexpr int kMaxWeights = 32;

    // Motif generation and transformation
    void generateMotif(const ScaleQuantizer* scale);
    void regenerateGates();
    void applyMTransform();
    void applyLAction(const ScaleQuantizer* scale);
    int pickWeightedDegree(int range);

    // PRNG
    uint32_t rng();
    float rngFloat();
    int rngInt(int max);  // 0..max-1

    // --- Parameters (cached) ---
    int motifLen_;
    int gateDensity_;
    int range_;
    int contour_;
    int mEvery_;
    int mTransform_;
    int lEvery_;
    int lAction_;
    int gateLen_;
    int velocity_;

    // --- Motif state ---
    int motif_[kMaxMotifLen];        // Scale degrees relative to 0
    bool gates_[kMaxMotifLen];       // Which steps have notes
    int stepInMotif_;                // Current playback position
    int motifCycles_;                // How many times motif has looped
    int mCycleCount_;                // Counts M transformations for L

    // --- Pitch state ---
    int transpose_;                  // Current transposition offset (from M/L)
    int octaveShift_;                // Current octave shift (from L)
    int numDegrees_;                 // Scale size (cached)
    bool scaleLoaded_;

    // --- Note weights ---
    float noteWeights_[kMaxWeights];

    // --- PRNG ---
    uint32_t rngState_;

    // --- Display ---
    int lastNote_;                   // Last output degree for display
};

#endif // QUANTUM_ENGINE_H
